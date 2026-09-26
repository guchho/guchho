#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

static Suite ts_suite{"ts"};

TEST(BundlerTS, TSDeclareConst) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare const require: any
				declare const exports: any;
				declare const module: any

				declare const foo: any
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSDeclareLet) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare let require: any
				declare let exports: any;
				declare let module: any

				declare let foo: any
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSDeclareVar) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare var require: any
				declare var exports: any;
				declare var module: any

				declare var foo: any
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSDeclareClass) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare class require {}
				declare class exports {};
				declare class module {}

				declare class foo {}
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSDeclareClassFields) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import './define-false'
				import './define-true'
			)"},
            {"/define-false/index.ts", R"(
				class Foo {
					a
					declare b
					[(() => null, c)]
					declare [(() => null, d)]

					static A
					static declare B
					static [(() => null, C)]
					static declare [(() => null, D)]
				}
				(() => new Foo())()
			)"},
            {"/define-true/index.ts", R"(
				class Bar {
					a
					declare b
					[(() => null, c)]
					declare [(() => null, d)]

					static A
					static declare B
					static [(() => null, C)]
					static declare [(() => null, D)]
				}
				(() => new Bar())()
			)"},
            {"/define-false/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": false
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassField,
        },
    });
}

TEST(BundlerTS, TSDeclareFunction) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare function require(): void
				declare function exports(): void;
				declare function module(): void

				declare function foo() {}
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSDeclareNamespace) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare namespace require {}
				declare namespace exports {};
				declare namespace module {}

				declare namespace foo {}
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSDeclareEnum) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare enum require {}
				declare enum exports {};
				declare enum module {}

				declare enum foo {}
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSDeclareConstEnum) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare const enum require {}
				declare const enum exports {};
				declare const enum module {}

				declare const enum foo {}
				let foo = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSConstEnumComments) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/bar.ts", R"(
				export const enum Foo {
					"%/*" = 1,
					"*/%" = 2,
				}
			)"},
            {"/foo.ts", R"(
				import { Foo } from "./bar";
				const enum Bar {
					"%/*" = 1,
					"*/%" = 2,
				}
				console.log({
					'should have comments': [
						Foo["%/*"],
						Bar["%/*"],
					],
					'should not have comments': [
						Foo["*/%"],
						Bar["*/%"],
					],
				});
			)"},
        },
        .entry_paths = {"/foo.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImportEmptyNamespace) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {ns} from './ns.ts'
				function foo(): ns.type {}
				foo();
			)"},
            {"/ns.ts", R"(
				export namespace ns {}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImportMissingES6) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import fn, {x as a, y as b} from './foo'
				console.log(fn(a, b))
			)"},
            {"/foo.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(entry.ts: ERROR: No matching export in "foo.js" for import "default"
entry.ts: ERROR: No matching export in "foo.js" for import "y"
)",
    });
}

TEST(BundlerTS, TSImportMissingUnusedES6) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import fn, {x as a, y as b} from './foo'
			)"},
            {"/foo.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExportMissingES6) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns)
			)"},
            {"/foo.ts", R"(
				export {nope} from './bar'
			)"},
            {"/bar.js", R"(
				export const yep = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImportMissingFile) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {Something} from './doesNotExist.ts'
				let foo = new Something
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.ts: ERROR: Could not resolve "./doesNotExist.ts"
)",
    });
}

TEST(BundlerTS, TSImportTypeOnlyFile) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {SomeType1} from './doesNotExist1.ts'
				import {SomeType2} from './doesNotExist2.ts'
				let foo: SomeType1 = bar()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExportEquals) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				import b from './b.ts'
				console.log(b)
			)"},
            {"/b.ts", R"(
				export = [123, foo]
				function foo() {}
			)"},
        },
        .entry_paths = {"/a.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExportNamespace) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				import {Foo} from './b.ts'
				console.log(new Foo)
			)"},
            {"/b.ts", R"(
				export class Foo {}
				export namespace Foo {
					export let foo = 1
				}
				export namespace Foo {
					export let bar = 2
				}
			)"},
        },
        .entry_paths = {"/a.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSNamespaceKeepNames) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				namespace ns {
					export let foo = () => {}
					export function bar() {}
					export class Baz {}
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .KeepNames = true,
        },
    });
}

TEST(BundlerTS, TSNamespaceKeepNamesTargetES2015) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				namespace ns {
					export let foo = () => {}
					export function bar() {}
					export class Baz {}
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
            .KeepNames = true,
            
        },
    });
}

TEST(BundlerTS, TSMinifyEnum) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				enum Foo { A, B, C = Foo }
			)"},
            {"/b.ts", R"(
				export enum Foo { X, Y, Z = Foo }
			)"},
        },
        .entry_paths = {"/a.ts", "/b.ts"},
        .options = guchho::config::Options{
			.AbsOutputDir = "/",
            .MinifyWhitespace = true,
			.MinifyIdentifiers = true,
            .MinifySyntax = true,           
        },
    });
}

TEST(BundlerTS, TSMinifyNestedEnum) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				function foo() { enum Foo { A, B, C = Foo } return Foo }
			)"},
            {"/b.ts", R"(
				export function foo() { enum Foo { X, Y, Z = Foo } return Foo }
			)"},
        },
        .entry_paths = {"/a.ts", "/b.ts"},
        .options = guchho::config::Options{
            .AbsOutputDir = "/",
            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,

        },
    });
}

TEST(BundlerTS, TSMinifyNestedEnumNoLogicalAssignment) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				function foo() { enum Foo { A, B, C = Foo } return Foo }
			)"},
            {"/b.ts", R"(
				export function foo() { enum Foo { X, Y, Z = Foo } return Foo }
			)"},
        },
        .entry_paths = {"/a.ts", "/b.ts"},
        .options = guchho::config::Options{
            .AbsOutputDir = "/",
            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kLogicalAssignment,
        },
    });
}

TEST(BundlerTS, TSMinifyNestedEnumNoArrow) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				function foo() { enum Foo { A, B, C = Foo } return Foo }
			)"},
            {"/b.ts", R"(
				export function foo() { enum Foo { X, Y, Z = Foo } return Foo }
			)"},
        },
        .entry_paths = {"/a.ts", "/b.ts"},
        .options = guchho::config::Options{
            .AbsOutputDir = "/",

            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,

            .UnsupportedJSFeatures = guchho::compat::JSFeature::kArrow,
        },
    });
}

TEST(BundlerTS, TSMinifyNamespace) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				namespace Foo {
					export namespace Bar {
						foo(Foo, Bar)
					}
				}
			)"},
            {"/b.ts", R"(
				export namespace Foo {
					export namespace Bar {
						foo(Foo, Bar)
					}
				}
			)"},
        },
        .entry_paths = {"/a.ts", "/b.ts"},
        .options = guchho::config::Options{
            .AbsOutputDir = "/",

            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,

        },
    });
}

TEST(BundlerTS, TSMinifyNamespaceNoLogicalAssignment) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				namespace Foo {
					export namespace Bar {
						foo(Foo, Bar)
					}
				}
			)"},
            {"/b.ts", R"(
				export namespace Foo {
					export namespace Bar {
						foo(Foo, Bar)
					}
				}
			)"},
        },
        .entry_paths = {"/a.ts", "/b.ts"},
        .options = guchho::config::Options{
            .AbsOutputDir = "/",

            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,

            .UnsupportedJSFeatures = guchho::compat::JSFeature::kLogicalAssignment,
        },
    });
}

TEST(BundlerTS, TSMinifyNamespaceNoArrow) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.ts", R"(
				namespace Foo {
					export namespace Bar {
						foo(Foo, Bar)
					}
				}
			)"},
            {"/b.ts", R"(
				export namespace Foo {
					export namespace Bar {
						foo(Foo, Bar)
					}
				}
			)"},
        },
        .entry_paths = {"/a.ts", "/b.ts"},
        .options = guchho::config::Options{
            .AbsOutputDir = "/",

            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,

            .UnsupportedJSFeatures = guchho::compat::JSFeature::kArrow,
        },
    });
}

TEST(BundlerTS, TSMinifyDerivedClass) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo extends Bar {
					foo = 1;
					bar = 2;
					constructor() {
						super();
						foo();
						bar();
					}
				}
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": false
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",

            .MinifySyntax = true,
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
        },
    });
}

TEST(BundlerTS, TSMinifyEnumPropertyNames) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import { CrossFileGood, CrossFileBad } from './cross-file'
				const enum SameFileGood {
					STR = 'str 1',
					NUM = 123,
				}
				const enum SameFileBad {
					PROTO = '__proto__',
					CONSTRUCTOR = 'constructor',
					PROTOTYPE = 'prototype',
				}
				class Foo {
					[100] = 100;
					'200' = 200;
					['300'] = 300;
					[SameFileGood.STR] = SameFileGood.STR;
					[SameFileGood.NUM] = SameFileGood.NUM;
					[CrossFileGood.STR] = CrossFileGood.STR;
					[CrossFileGood.NUM] = CrossFileGood.NUM;
				}
				shouldNotBeComputed(
					class {
						[100] = 100;
						'200' = 200;
						['300'] = 300;
						[SameFileGood.STR] = SameFileGood.STR;
						[SameFileGood.NUM] = SameFileGood.NUM;
						[CrossFileGood.STR] = CrossFileGood.STR;
						[CrossFileGood.NUM] = CrossFileGood.NUM;
					},
					{
						[100]: 100,
						'200': 200,
						['300']: 300,
						[SameFileGood.STR]: SameFileGood.STR,
						[SameFileGood.NUM]: SameFileGood.NUM,
						[CrossFileGood.STR]: CrossFileGood.STR,
						[CrossFileGood.NUM]: CrossFileGood.NUM,
					},
				)
				mustBeComputed(
					{ [SameFileBad.PROTO]: null },
					{ [CrossFileBad.PROTO]: null },
					class { [SameFileBad.CONSTRUCTOR]() {} },
					class { [CrossFileBad.CONSTRUCTOR]() {} },
					class { static [SameFileBad.PROTOTYPE]() {} },
					class { static [CrossFileBad.PROTOTYPE]() {} },
				)
			)"},
            {"/cross-file.ts", R"(
				export const enum CrossFileGood {
					STR = 'str 2',
					NUM = 321,
				}
				export const enum CrossFileBad {
					PROTO = '__proto__',
					CONSTRUCTOR = 'constructor',
					PROTOTYPE = 'prototype',
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
			
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",

            .MinifySyntax = true,
        },
    });
}

TEST(BundlerTS, TSMinifyEnumCrossFileInlineStringsIntoTemplates) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import { CrossFile } from './cross-file'
				enum SameFile {
					STR = 'str 1',
					NUM = 123,
				}
				console.log(`
					SameFile.STR = ${SameFile.STR}
					SameFile.NUM = ${SameFile.NUM}
					CrossFile.STR = ${CrossFile.STR}
					CrossFile.NUM = ${CrossFile.NUM}
				`)
			)"},
            {"/cross-file.ts", R"(
				export enum CrossFile {
					STR = 'str 2',
					NUM = 321,
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,

        },
    });
}

TEST(BundlerTS, TSImportVsLocalCollisionAllTypes) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {a, b, c, d, e} from './other.ts'
				let a
				const b = 0
				var c
				function d() {}
				class e {}
				console.log(a, b, c, d, e)
			)"},
            {"/other.ts", R"(
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImportVsLocalCollisionMixed) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {a, b, c, d, e, real} from './other.ts'
				let a
				const b = 0
				var c
				function d() {}
				class e {}
				console.log(a, b, c, d, e, real)
			)"},
            {"/other.ts", R"(
				export let real = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImportEqualsEliminationTest) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import a = foo.a
				import b = a.b
				import c = b.c

				import x = foo.x
				import y = x.y
				import z = y.z

				export let bar = c
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImportEqualsTreeShakingFalse) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import { foo } from 'pkg'
				import used = foo.used
				import unused = foo.unused
				export { used }
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .TreeShaking = false,
        },
    });
}

TEST(BundlerTS, TSImportEqualsTreeShakingTrue) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import { foo } from 'pkg'
				import used = foo.used
				import unused = foo.unused
				export { used }
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .TreeShaking = true,
        },
    });
}

TEST(BundlerTS, TSImportEqualsBundle) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import { foo } from 'pkg'
				import used = foo.used
				import unused = foo.unused
				export { used }
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"pkg", true}},
                },
            },
        },
    });
}

TEST(BundlerTS, TSImportEqualsUndefinedImport) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './import.ts'
				import value_copy = ns.value
				import Type_copy = ns.Type
				let foo: Type_copy = value_copy
				console.log(foo)
			)"},
            {"/import.ts", R"(
				export let value = 123
				export type Type = number
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"pkg", true}},
                },
            },
        },
    });
}

TEST(BundlerTS, TSMinifiedBundleES6) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {foo} from './a'
				console.log(foo())
			)"},
            {"/a.ts", R"(
				export function foo() {
					return 123
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",

            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,

        },
    });
}

TEST(BundlerTS, TSMinifiedBundleCommonJS) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				const {foo} = require('./a')
				console.log(foo(), require('./j.json'))
			)"},
            {"/a.ts", R"(
				exports.foo = function() {
					return 123
				}
			)"},
            {"/j.json", R"(
				{"test": true}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",

            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,

        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsNoConfig) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				declare let x: any, y: any
				@x.y()
				@(new y.x)
				export default class Foo {
					@x @y mUndef: any
					@x @y mDef = 1
					@x @y method() { return new Foo }
					@x @y accessor aUndef: any
					@x @y accessor aDef = 1

					@x @y static sUndef: any
					@x @y static sDef = new Foo
					@x @y static sMethod() { return new Foo }
					@x @y static accessor asUndef: any
					@x @y static accessor asDef = 1

					@x @y #mUndef: any
					@x @y #mDef = 1
					@x @y #method() { return new Foo }
					@x @y accessor #aUndef: any
					@x @y accessor #aDef = 1

					@x @y static #sUndef: any
					@x @y static #sDef = 1
					@x @y static #sMethod() { return new Foo }
					@x @y static accessor #asUndef: any
					@x @y static accessor #asDef = 1
				}
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"experimentalDecorators": false
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecorators) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import all from './all'
				import all_computed from './all_computed'
				import {a} from './a'
				import {b} from './b'
				import {c} from './c'
				import {d} from './d'
				import e from './e'
				import f from './f'
				import g from './g'
				import h from './h'
				import {i} from './i'
				import {j} from './j'
				import k from './k'
				import {fn} from './arguments'
				console.log(all, all_computed, a, b, c, d, e, f, g, h, i, j, k, fn)
			)"},
            {"/all.ts", R"(
				@x.y()
				@new y.x()
				export default class Foo {
					@x @y mUndef
					@x @y mDef = 1
					@x @y method(@x0 @y0 arg0, @x1 @y1 arg1) { return new Foo }
					@x @y declare mDecl
					@x @y abstract mAbst
					constructor(@x0 @y0 arg0, @x1 @y1 arg1) {}

					@x @y static sUndef
					@x @y static sDef = new Foo
					@x @y static sMethod(@x0 @y0 arg0, @x1 @y1 arg1) { return new Foo }
					@x @y static declare mDecl
				}
			)"},
            {"/all_computed.ts", R"(
				@x?.[_ + 'y']()
				@new y()?.[_ + 'x']()
				export default class Foo {
					@x @y [mUndef()]
					@x @y [mDef()] = 1
					@x @y [method()](@x0 @y0 arg0, @x1 @y1 arg1) { return new Foo }
					@x @y declare [mDecl()]
					@x @y abstract [mAbst()]

					// Side effect order must be preserved even for fields without decorators
					[xUndef()]
					[xDef()] = 2
					static [yUndef()]
					static [yDef()] = 3

					@x @y static [sUndef()]
					@x @y static [sDef()] = new Foo
					@x @y static [sMethod()](@x0 @y0 arg0, @x1 @y1 arg1) { return new Foo }
					@x @y static declare [mDecl()]
				}
			)"},
            {"/a.ts", R"(
				@x(() => 0) @y(() => 1)
				class a_class {
					fn() { return new a_class }
					static z = new a_class
				}
				export let a = a_class
			)"},
            {"/b.ts", R"(
				@x(() => 0) @y(() => 1)
				abstract class b_class {
					fn() { return new b_class }
					static z = new b_class
				}
				export let b = b_class
			)"},
            {"/c.ts", R"(
				@x(() => 0) @y(() => 1)
				export class c {
					fn() { return new c }
					static z = new c
				}
			)"},
            {"/d.ts", R"(
				@x(() => 0) @y(() => 1)
				export abstract class d {
					fn() { return new d }
					static z = new d
				}
			)"},
            {"/e.ts", R"(
				@x(() => 0) @y(() => 1)
				export default class {}
			)"},
            {"/f.ts", R"(
				@x(() => 0) @y(() => 1)
				export default class f {
					fn() { return new f }
					static z = new f
				}
			)"},
            {"/g.ts", R"(
				@x(() => 0) @y(() => 1)
				export default abstract class {}
			)"},
            {"/h.ts", R"(
				@x(() => 0) @y(() => 1)
				export default abstract class h {
					fn() { return new h }
					static z = new h
				}
			)"},
            {"/i.ts", R"(
				class i_class {
					@x(() => 0) @y(() => 1)
					foo
				}
				export let i = i_class
			)"},
            {"/j.ts", R"(
				export class j {
					@x(() => 0) @y(() => 1)
					foo() {}
				}
			)"},
            {"/k.ts", R"(
				export default class {
					foo(@x(() => 0) @y(() => 1) x) {}
				}
			)"},
            {"/arguments.ts", R"(
				function dec(x: any): any {}
				export function fn(x: string): any {
					class Foo {
						@dec(arguments[0])
						[arguments[0]]() {}
					}
					return Foo;
				}
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": false,
					"experimentalDecorators": true
				}
			})"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsKeepNames) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				@decoratorMustComeAfterName
				export class Foo {}
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"experimentalDecorators": true
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .KeepNames = true,
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorScopeIssue2147) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				let foo = 1
				class Foo {
					method1(@dec(foo) foo = 2) {}
					method2(@dec(() => foo) foo = 3) {}
				}

				class Bar {
					static x = class {
						static y = () => {
							let bar = 1
							@dec(bar)
							@dec(() => bar)
							class Baz {
								@dec(bar) method1() {}
								@dec(() => bar) method2() {}
								method3(@dec(() => bar) bar) {}
								method4(@dec(() => bar) bar) {}
							}
							return Baz
						}
					}
				}
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": false,
					"experimentalDecorators": true
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExportDefaultTypeIssue316) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import dc_def, { bar as dc } from './keep/declare-class'
				import dl_def, { bar as dl } from './keep/declare-let'
				import im_def, { bar as im } from './keep/interface-merged'
				import in_def, { bar as _in } from './keep/interface-nested'
				import tn_def, { bar as tn } from './keep/type-nested'
				import vn_def, { bar as vn } from './keep/value-namespace'
				import vnm_def, { bar as vnm } from './keep/value-namespace-merged'

				import i_def, { bar as i } from './remove/interface'
				import ie_def, { bar as ie } from './remove/interface-exported'
				import t_def, { bar as t } from './remove/type'
				import te_def, { bar as te } from './remove/type-exported'
				import ton_def, { bar as ton } from './remove/type-only-namespace'
				import tone_def, { bar as tone } from './remove/type-only-namespace-exported'

				export default [
					dc_def, dc,
					dl_def, dl,
					im_def, im,
					in_def, _in,
					tn_def, tn,
					vn_def, vn,
					vnm_def, vnm,

					i,
					ie,
					t,
					te,
					ton,
					tone,
				]
			)"},
            {"/keep/declare-class.ts", R"(
				declare class foo {}
				export default foo
				export let bar = 123
			)"},
            {"/keep/declare-let.ts", R"(
				declare let foo: number
				export default foo
				export let bar = 123
			)"},
            {"/keep/interface-merged.ts", R"(
				class foo {
					static x = new foo
				}
				interface foo {}
				export default foo
				export let bar = 123
			)"},
            {"/keep/interface-nested.ts", R"(
				if (true) {
					interface foo {}
				}
				export default foo
				export let bar = 123
			)"},
            {"/keep/type-nested.ts", R"(
				if (true) {
					type foo = number
				}
				export default foo
				export let bar = 123
			)"},
            {"/keep/value-namespace.ts", R"(
				namespace foo {
					export let num = 0
				}
				export default foo
				export let bar = 123
			)"},
            {"/keep/value-namespace-merged.ts", R"(
				namespace foo {
					export type num = number
				}
				namespace foo {
					export let num = 0
				}
				export default foo
				export let bar = 123
			)"},
            {"/remove/interface.ts", R"(
				interface foo { }
				export default foo
				export let bar = 123
			)"},
            {"/remove/interface-exported.ts", R"(
				export interface foo { }
				export default foo
				export let bar = 123
			)"},
            {"/remove/type.ts", R"(
				type foo = number
				export default foo
				export let bar = 123
			)"},
            {"/remove/type-exported.ts", R"(
				export type foo = number
				export default foo
				export let bar = 123
			)"},
            {"/remove/type-only-namespace.ts", R"(
				namespace foo {
					export type num = number
				}
				export default foo
				export let bar = 123
			)"},
            {"/remove/type-only-namespace-exported.ts", R"(
				export namespace foo {
					export type num = number
				}
				export default foo
				export let bar = 123
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": false
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImplicitExtensions) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import './pick-js.js'
				import './pick-ts.js'
				import './pick-jsx.jsx'
				import './pick-tsx.jsx'
				import './order-js.js'
				import './order-jsx.jsx'

				import 'pkg/foo-js.js'
				import 'pkg/foo-jsx.jsx'
				import 'pkg-exports/xyz-js'
				import 'pkg-exports/xyz-jsx'
				import 'pkg-exports/foo-js.js'
				import 'pkg-exports/foo-jsx.jsx'
				import 'pkg-imports'
			)"},

            {"/pick-js.js", R"(console.log("correct"))"},
            {"/pick-js.ts", R"(console.log("wrong"))"},

            {"/pick-ts.jsx", R"(console.log("wrong"))"},
            {"/pick-ts.ts", R"(console.log("correct"))"},

            {"/pick-jsx.jsx", R"(console.log("correct"))"},
            {"/pick-jsx.tsx", R"(console.log("wrong"))"},

            {"/pick-tsx.js", R"(console.log("wrong"))"},
            {"/pick-tsx.tsx", R"(console.log("correct"))"},

            {"/order-js.ts", R"(console.log("correct"))"},
            {"/order-js.tsx", R"(console.log("wrong"))"},

            {"/order-jsx.ts", R"(console.log("correct"))"},
            {"/order-jsx.tsx", R"(console.log("wrong"))"},

            {"/node_modules/pkg/foo-js.ts", R"(console.log("correct"))"},
            {"/node_modules/pkg/foo-jsx.tsx", R"(console.log("correct"))"},

            {"/node_modules/pkg-exports/package.json", R"({
				"exports": {
					"./xyz-js": "./abc-js.js",
					"./xyz-jsx": "./abc-jsx.jsx",
					"./*": "./lib/*"
				}
			})"},
            {"/node_modules/pkg-exports/abc-js.ts", R"(console.log("correct"))"},
            {"/node_modules/pkg-exports/abc-jsx.tsx", R"(console.log("correct"))"},
            {"/node_modules/pkg-exports/lib/foo-js.ts", R"(console.log("correct"))"},
            {"/node_modules/pkg-exports/lib/foo-jsx.tsx", R"(console.log("correct"))"},

            {"/node_modules/pkg-imports/package.json", R"({
				"imports": {
					"#xyz-js": "./abc-js.js",
					"#xyz-jsx": "./abc-jsx.jsx",
					"#bar/*": "./lib/*"
				}
			})"},
            {"/node_modules/pkg-imports/index.js", R"(
				import "#xyz-js"
				import "#xyz-jsx"
				import "#bar/foo-js.js"
				import "#bar/foo-jsx.jsx"
			)"},
            {"/node_modules/pkg-imports/abc-js.ts", R"(console.log("correct"))"},
            {"/node_modules/pkg-imports/abc-jsx.tsx", R"(console.log("correct"))"},
            {"/node_modules/pkg-imports/lib/foo-js.ts", R"(console.log("correct"))"},
            {"/node_modules/pkg-imports/lib/foo-jsx.tsx", R"(console.log("correct"))"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImplicitExtensionsMissing) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import './mjs.mjs'
				import './cjs.cjs'
				import './js.js'
				import './jsx.jsx'
			)"},
            {"/mjs.ts", R"()"},
            {"/mjs.tsx", R"()"},
            {"/cjs.ts", R"()"},
            {"/cjs.tsx", R"()"},
            {"/js.ts.js", R"()"},
            {"/jsx.tsx.jsx", R"()"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.ts: ERROR: Could not resolve "./mjs.mjs"
entry.ts: ERROR: Could not resolve "./cjs.cjs"
entry.ts: ERROR: Could not resolve "./js.js"
entry.ts: ERROR: Could not resolve "./jsx.jsx"
)",
    });
}

TEST(BundlerTS, ExportTypeIssue379) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as A from './a'
				import * as B from './b'
				import * as C from './c'
				import * as D from './d'
				console.log(A, B, C, D)
			)"},
            {"/a.ts", R"(
				type Test = Element
				let foo = 123
				export { Test, foo }
			)"},
            {"/b.ts", R"(
				export type Test = Element
				export let foo = 123
			)"},
            {"/c.ts", R"(
				import { Test } from './test'
				let foo = 123
				export { Test }
				export { foo }
			)"},
            {"/d.ts", R"(
				export { Test }
				export { foo }
				import { Test } from './test'
				let foo = 123
			)"},
            {"/test.ts", R"(
				export type Test = Element
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kFalse,
                },
            },
        },
    });
}

TEST(BundlerTS, ThisInsideFunctionTS) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
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
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kFalse,
                },
            },
        },
    });
}

TEST(BundlerTS, ThisInsideFunctionTSUseDefineForClassFields) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
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
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kTrue,
                },
            },
        },
    });
}

TEST(BundlerTS, ThisInsideFunctionTSNoBundle) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
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
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": false
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, ThisInsideFunctionTSNoBundleUseDefineForClassFields) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
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
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kTrue,
                },
            },
        },
    });
}

TEST(BundlerTS, TSComputedClassFieldUseDefineFalse) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					[q];
					[r] = s;
					@dec
					[x];
					@dec
					[y] = z;
				}
				new Foo()
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": false,
					"experimentalDecorators": true
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSComputedClassFieldUseDefineTrue) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					[q];
					[r] = s;
					@dec
					[x];
					@dec
					[y] = z;
				}
				new Foo()
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": true,
					"experimentalDecorators": true
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSComputedClassFieldUseDefineTrueLower) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					[q];
					[r] = s;
					@dec
					[x];
					@dec
					[y] = z;
				}
				new Foo()
			)"},
            {"/tsconfig.json", R"({
				"compilerOptions": {
					"useDefineForClassFields": true,
					"experimentalDecorators": true
				}
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassField,
        },
    });
}

TEST(BundlerTS, TSAbstractClassFieldUseAssign) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				const keepThis = Symbol('keepThis')
				declare const AND_REMOVE_THIS: unique symbol
				abstract class Foo {
					REMOVE_THIS: any
					[keepThis]: any
					abstract REMOVE_THIS_TOO: any
					abstract [AND_REMOVE_THIS]: any
					abstract [(x => y => x + y)('nested')('scopes')]: any
				}
				(() => new Foo())()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kFalse,
                },
            },
        },
    });
}

TEST(BundlerTS, TSAbstractClassFieldUseDefine) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				const keepThisToo = Symbol('keepThisToo')
				declare const REMOVE_THIS_TOO: unique symbol
				abstract class Foo {
					keepThis: any
					[keepThisToo]: any
					abstract REMOVE_THIS: any
					abstract [REMOVE_THIS_TOO]: any
					abstract [(x => y => x + y)('nested')('scopes')]: any
				}
				(() => new Foo())()
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kTrue,
                },
            },
        },
    });
}

TEST(BundlerTS, TSImportMTS) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import './imported.mjs'
			)"},
            {"/imported.mts", R"(
				console.log('works')
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",

        },
    });
}

TEST(BundlerTS, TSImportCTS) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				require('./required.cjs')
			)"},
            {"/required.cjs", R"(
				console.log('works')
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",

        },
    });
}

TEST(BundlerTS, TSSideEffectsFalseWarningTypeDeclarations) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import "some-js"
				import "some-ts"
				import "empty-js"
				import "empty-ts"
				import "empty-dts"
			)"},

            {"/node_modules/some-js/package.json", R"({ "main": "./foo.js", "sideEffects": false })"},
            {"/node_modules/some-js/foo.js", R"(console.log('foo'))"},

            {"/node_modules/some-ts/package.json", R"({ "main": "./foo.ts", "sideEffects": false })"},
            {"/node_modules/some-ts/foo.ts", R"(console.log('foo' as string))"},

            {"/node_modules/empty-js/package.json", R"({ "main": "./foo.js", "sideEffects": false })"},
            {"/node_modules/empty-js/foo.js", R"()"},

            {"/node_modules/empty-ts/package.json", R"({ "main": "./foo.ts", "sideEffects": false })"},
            {"/node_modules/empty-ts/foo.ts", R"(export type Foo = number)"},

            {"/node_modules/empty-dts/package.json", R"({ "main": "./foo.d.ts", "sideEffects": false })"},
            {"/node_modules/empty-dts/foo.d.ts", R"(export type Foo = number)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.ts: WARNING: Ignoring this import because "node_modules/some-js/foo.js" was marked as having no side effects
node_modules/some-js/package.json: NOTE: "sideEffects" is false in the enclosing "package.json" file:
entry.ts: WARNING: Ignoring this import because "node_modules/some-ts/foo.ts" was marked as having no side effects
node_modules/some-ts/package.json: NOTE: "sideEffects" is false in the enclosing "package.json" file:
)",
    });
}

TEST(BundlerTS, TSSiblingNamespace) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/let.ts", R"(
				export namespace x { export let y = 123 }
				export namespace x { export let z = y }
			)"},
            {"/function.ts", R"(
				export namespace x { export function y() {} }
				export namespace x { export let z = y }
			)"},
            {"/class.ts", R"(
				export namespace x { export class y {} }
				export namespace x { export let z = y }
			)"},
            {"/namespace.ts", R"(
				export namespace x { export namespace y { 0 } }
				export namespace x { export let z = y }
			)"},
            {"/enum.ts", R"(
				export namespace x { export enum y {} }
				export namespace x { export let z = y }
			)"},
        },
        .entry_paths = {
            "/let.ts",
            "/function.ts",
            "/class.ts",
            "/namespace.ts",
            "/enum.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSSiblingEnum) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/number.ts", R"(
				export enum x { y, yy = y }
				export enum x { z = y + 1 }

				declare let y: any, z: any
				export namespace x { console.log(y, z) }
				console.log(x.y, x.z)
			)"},
            {"/string.ts", R"(
				export enum x { y = 'a', yy = y }
				export enum x { z = y }

				declare let y: any, z: any
				export namespace x { console.log(y, z) }
				console.log(x.y, x.z)
			)"},
            {"/propagation.ts", R"(
				export enum a { b = 100 }
				export enum x {
					c = a.b,
					d = c * 2,
					e = x.d ** 2,
					f = x['e'] / 4,
				}
				export enum x { g = f >> 4 }
				console.log(a.b, a['b'], x.g, x['g'])
			)"},
            {"/nested-number.ts", R"(
				export namespace foo { export enum x { y, yy = y } }
				export namespace foo { export enum x { z = y + 1 } }

				declare let y: any, z: any
				export namespace foo.x {
					console.log(y, z)
					console.log(x.y, x.z)
				}
			)"},
            {"/nested-string.ts", R"(
				export namespace foo { export enum x { y = 'a', yy = y } }
				export namespace foo { export enum x { z = y } }

				declare let y: any, z: any
				export namespace foo.x {
					console.log(y, z)
					console.log(x.y, x.z)
				}
			)"},
            {"/nested-propagation.ts", R"(
				export namespace n { export enum a { b = 100 } }
				export namespace n {
					export enum x {
						c = n.a.b,
						d = c * 2,
						e = x.d ** 2,
						f = x['e'] / 4,
					}
				}
				export namespace n {
					export enum x { g = f >> 4 }
					console.log(a.b, n.a.b, n['a']['b'], x.g, n.x.g, n['x']['g'])
				}
			)"},
        },
        .entry_paths = {
            "/number.ts",
            "/string.ts",
            "/propagation.ts",
            "/nested-number.ts",
            "/nested-string.ts",
            "/nested-propagation.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumTreeShaking) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/simple-member.ts", R"(
				enum x { y = 123 }
				console.log(x.y)
			)"},
            {"/simple-enum.ts", R"(
				enum x { y = 123 }
				console.log(x)
			)"},
            {"/sibling-member.ts", R"(
				enum x { y = 123 }
				enum x { z = y * 2 }
				console.log(x.y, x.z)
			)"},
            {"/sibling-enum-before.ts", R"(
				console.log(x)
				enum x { y = 123 }
				enum x { z = y * 2 }
			)"},
            {"/sibling-enum-middle.ts", R"(
				enum x { y = 123 }
				console.log(x)
				enum x { z = y * 2 }
			)"},
            {"/sibling-enum-after.ts", R"(
				enum x { y = 123 }
				enum x { z = y * 2 }
				console.log(x)
			)"},
            {"/namespace-before.ts", R"(
				namespace x { console.log(x, y) }
				enum x { y = 123 }
			)"},
            {"/namespace-after.ts", R"(
				enum x { y = 123 }
				namespace x { console.log(x, y) }
			)"},
        },
        .entry_paths = {
            "/simple-member.ts",
            "/simple-enum.ts",
            "/sibling-member.ts",
            "/sibling-enum-before.ts",
            "/sibling-enum-middle.ts",
            "/sibling-enum-after.ts",
            "/namespace-before.ts",
            "/namespace-after.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumJSX) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/element.tsx", R"(
				export enum Foo { Div = 'div' }
				console.log(<Foo.Div />)
			)"},
            {"/fragment.tsx", R"(
				export enum React { Fragment = 'div' }
				console.log(<>test</>)
			)"},
            {"/nested-element.tsx", R"(
				namespace x.y { export enum Foo { Div = 'div' } }
				namespace x.y { console.log(<x.y.Foo.Div />) }
			)"},
            {"/nested-fragment.tsx", R"(
				namespace x.y { export enum React { Fragment = 'div' } }
				namespace x.y { console.log(<>test</>) }
			)"},
        },
        .entry_paths = {
            "/element.tsx",
            "/fragment.tsx",
            "/nested-element.tsx",
            "/nested-fragment.tsx",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumDefine) {
    guchho::config::ProcessedDefines test_defines;
    test_defines.IdentifierDefines["d"] = guchho::config::DefineData{
        .DefineExprData = std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
            .Parts = {"b"},
        }),
    };

    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				enum a { b = 123, c = d }
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .Defines = &test_defines,
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumSameModuleInliningAccess) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				enum a_num { x = 123 }
				enum b_num { x = 123 }
				enum c_num { x = 123 }
				enum d_num { x = 123 }
				enum e_num { x = 123 }

				enum a_str { x = 'abc' }
				enum b_str { x = 'abc' }
				enum c_str { x = 'abc' }
				enum d_str { x = 'abc' }
				enum e_str { x = 'abc' }

				inlined = [
					a_num.x,
					b_num['x'],

					a_str.x,
					b_str['x'],
				]

				not_inlined = [
					c_num?.x,
					d_num?.['x'],
					e_num,

					c_str?.x,
					d_str?.['x'],
					e_str,
				]
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumCrossModuleInliningAccess) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {
					a_num, b_num, c_num, d_num, e_num,
					a_str, b_str, c_str, d_str, e_str,
				} from './enums'

				inlined = [
					a_num.x,
					b_num['x'],

					a_str.x,
					b_str['x'],
				]

				not_inlined = [
					c_num?.x,
					d_num?.['x'],
					e_num,

					c_str?.x,
					d_str?.['x'],
					e_str,
				]
			)"},
            {"/enums.ts", R"(
				export enum a_num { x = 123 }
				export enum b_num { x = 123 }
				export enum c_num { x = 123 }
				export enum d_num { x = 123 }
				export enum e_num { x = 123 }

				export enum a_str { x = 'abc' }
				export enum b_str { x = 'abc' }
				export enum c_str { x = 'abc' }
				export enum d_str { x = 'abc' }
				export enum e_str { x = 'abc' }
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumCrossModuleInliningDefinitions) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import { a } from './enums'
				console.log([
					a.implicit_number,
					a.explicit_number,
					a.explicit_string,
					a.non_constant,
				])
			)"},
            {"/enums.ts", R"(
				export enum a {
					implicit_number,
					explicit_number = 123,
					explicit_string = 'xyz',
					non_constant = foo,
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumCrossModuleInliningReExport) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import { a } from './re-export'
				import { b } from './re-export-star'
				import * as ns from './enums'
				console.log([
					a.x,
					b.x,
					ns.c.x,
				])
			)"},
            {"/re-export.js", R"(
				export { a } from './enums'
			)"},
            {"/re-export-star.js", R"(
				export * from './enums'
			)"},
            {"/enums.ts", R"(
				export enum a { x = 'a' }
				export enum b { x = 'b' }
				export enum c { x = 'c' }
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumCrossModuleInliningMinifyIndexIntoDot) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				const enum Foo {
					foo1 = 'abc',
					foo2 = 'a b c',
				}
				import { Bar } from './lib'
				inlined = [
					obj[Foo.foo1],
					obj[Bar.bar1],
					obj?.[Foo.foo1],
					obj?.[Bar.bar1],
					obj?.prop[Foo.foo1],
					obj?.prop[Bar.bar1],
				]
				notInlined = [
					obj[Foo.foo2],
					obj[Bar.bar2],
					obj?.[Foo.foo2],
					obj?.[Bar.bar2],
					obj?.prop[Foo.foo2],
					obj?.prop[Bar.bar2],
				]
			)"},
            {"/lib.ts", R"(
				export const enum Bar {
					bar1 = 'xyz',
					bar2 = 'x y z',
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerTS, TSEnumCrossModuleTreeShaking) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {
					a_DROP,
					b_DROP,
					c_DROP,
				} from './enums'

				console.log([
					a_DROP.x,
					b_DROP['x'],
					c_DROP.x,
				])

				import {
					a_keep,
					b_keep,
					c_keep,
					d_keep,
					e_keep,
				} from './enums'

				console.log([
					a_keep.x,
					b_keep.x,
					c_keep,
					d_keep.y,
					e_keep.x,
				])
			)"},
            {"/enums.ts", R"(
				export enum a_DROP { x = 1 }  // test a dot access
				export enum b_DROP { x = 2 }  // test an index access
				export enum c_DROP { x = '' } // test a string enum

				export enum a_keep { x = false } // false is not inlinable
				export enum b_keep { x = foo }   // foo has side effects
				export enum c_keep { x = 3 }     // this enum object is captured
				export enum d_keep { x = 4 }     // we access "y" on this object
				export let e_keep = {}           // non-enum properties should be kept
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumExportClause) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {
					A,
					B,
					C as c,
					d as dd,
				} from './enums'

				console.log([
					A.A,
					B.B,
					c.C,
					dd.D,
				])
			)"},
            {"/enums.ts", R"(
					export enum A { A = 1 }
					enum B { B = 2 }
					export enum C { C = 3 }
					enum D { D = 4 }
					export { B, D as d }
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSThisIsUndefinedWarning) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/warning1.ts", R"(export var foo = this)"},
            {"/warning2.ts", R"(export var foo = this || this.foo)"},
            {"/warning3.ts", R"(export var foo = this ? this.foo : null)"},

            {"/silent1.ts", R"(export var foo = this && this.foo)"},
            {"/silent2.ts", R"(export var foo = this && (() => this.foo))"},
        },
        .entry_paths = {
            "/warning1.ts",
            "/warning2.ts",
            "/warning3.ts",

            "/silent1.ts",
            "/silent2.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        
        .expected_scan_log = R"(warning1.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
warning1.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
warning2.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
warning2.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
warning3.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
warning3.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)",
.debug_logs = true,
    });
}

TEST(BundlerTS, TSCommonJSVariableInESMTypeModule) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(module.exports = null)"},
            {"/package.json", R"({ "type": "module" })"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.ts: WARNING: The CommonJS "module" variable is treated as a global variable in an ECMAScript module and may not work as expected
package.json: NOTE: This file is considered to be an ECMAScript module because the enclosing "package.json" file sets the type of this file to "module":
NOTE: Node's package format requires that CommonJS files in a "type": "module" package use the ".cjs" file extension. If you are using TypeScript, you can use the ".cts" file extension with Guchho instead.
)",
    });
}

TEST(BundlerTS, EnumRulesFrom_TypeScript_5_0) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/supported.ts", R"(
				// From https://github.com/microsoft/TypeScript/pull/50528:
				// "An expression is considered a constant expression if it is
				const enum Foo {
					// a number or string literal,
					X0 = 123,
					X1 = 'x',

					// a unary +, -, or ~ applied to a numeric constant expression,
					X2 = +1,
					X3 = -2,
					X4 = ~3,

					// a binary +, -, *, /, %, **, <<, >>, >>>, |, &, ^ applied to two numeric constant expressions,
					X5 = 1 + 2,
					X6 = 1 - 2,
					X7 = 2 * 3,
					X8 = 1 / 2,
					X9 = 3 % 2,
					X10 = 2 ** 3,
					X11 = 1 << 2,
					X12 = -9 >> 1,
					X13 = -9 >>> 1,
					X14 = 5 | 12,
					X15 = 5 & 12,
					X16 = 5 ^ 12,

					// a binary + applied to two constant expressions whereof at least one is a string,
					X17 = 'x' + 0,
					X18 = 0 + 'x',
					X19 = 'x' + 'y',
					X20 = '' + NaN,
					X21 = '' + Infinity,
					X22 = '' + -Infinity,
					X23 = '' + -0,

					// a template expression where each substitution expression is a constant expression,
					X24 = `A${0}B${'x'}C${1 + 3 - 4 / 2 * 5 ** 6}D`,

					// a parenthesized constant expression,
					X25 = (321),

					// a dotted name (e.g. x.y.z) that references a const variable with a constant expression initializer and no type annotation,
					/* (we don't implement this one) */

					// a dotted name that references an enum member with an enum literal type, or
					X26 = X0,
					X27 = X0 + 'x',
					X28 = 'x' + X0,
					X29 = `a${X0}b`,
					X30 = Foo.X0,
					X31 = Foo.X0 + 'x',
					X32 = 'x' + Foo.X0,
					X33 = `a${Foo.X0}b`,

					// a dotted name indexed by a string literal (e.g. x.y["z"]) that references an enum member with an enum literal type."
					X34 = X1,
					X35 = X1 + 'y',
					X36 = 'y' + X1,
					X37 = `a${X1}b`,
					X38 = Foo['X1'],
					X39 = Foo['X1'] + 'y',
					X40 = 'y' + Foo['X1'],
					X41 = `a${Foo['X1']}b`,
				}

				console.log(
					// a number or string literal,
					Foo.X0,
					Foo.X1,

					// a unary +, -, or ~ applied to a numeric constant expression,
					Foo.X2,
					Foo.X3,
					Foo.X4,

					// a binary +, -, *, /, %, **, <<, >>, >>>, |, &, ^ applied to two numeric constant expressions,
					Foo.X5,
					Foo.X6,
					Foo.X7,
					Foo.X8,
					Foo.X9,
					Foo.X10,
					Foo.X11,
					Foo.X12,
					Foo.X13,
					Foo.X14,
					Foo.X15,
					Foo.X16,

					// a template expression where each substitution expression is a constant expression,
					Foo.X17,
					Foo.X18,
					Foo.X19,
					Foo.X20,
					Foo.X21,
					Foo.X22,
					Foo.X23,

					// a template expression where each substitution expression is a constant expression,
					Foo.X24,

					// a parenthesized constant expression,
					Foo.X25,

					// a dotted name that references an enum member with an enum literal type, or
					Foo.X26,
					Foo.X27,
					Foo.X28,
					Foo.X29,
					Foo.X30,
					Foo.X31,
					Foo.X32,
					Foo.X33,

					// a dotted name indexed by a string literal (e.g. x.y["z"]) that references an enum member with an enum literal type."
					Foo.X34,
					Foo.X35,
					Foo.X36,
					Foo.X37,
					Foo.X38,
					Foo.X39,
					Foo.X40,
					Foo.X41,
				)
			)"},
            {"/not-supported.ts", R"(
				const enum NonIntegerNumberToString {
					SUPPORTED = '' + 1,
					UNSUPPORTED = '' + 1.5,
				}
				console.log(
					NonIntegerNumberToString.SUPPORTED,
					NonIntegerNumberToString.UNSUPPORTED,
				)

				const enum OutOfBoundsNumberToString {
					SUPPORTED = '' + 1_000_000_000,
					UNSUPPORTED = '' + 1_000_000_000_000,
				}
				console.log(
					OutOfBoundsNumberToString.SUPPORTED,
					OutOfBoundsNumberToString.UNSUPPORTED,
				)

				const enum TemplateExpressions {
					// TypeScript enums don't handle any of these
					NULL = '' + null,
					TRUE = '' + true,
					FALSE = '' + false,
					BIGINT = '' + 123n,
				}
				console.log(
					TemplateExpressions.NULL,
					TemplateExpressions.TRUE,
					TemplateExpressions.FALSE,
					TemplateExpressions.BIGINT,
				)
			)"},
        },
        .entry_paths = {
            "/supported.ts",
            "/not-supported.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSEnumUseBeforeDeclare) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				export function before() {
					console.log(Foo.FOO)
				}
				enum Foo { FOO }
				export function after() {
					console.log(Foo.FOO)
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSPreferJSOverTSInsideNodeModules) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.ts", R"(
				// Implicit extensions
				import './relative/path'
				import 'package/path'

				// Explicit extensions
				import './relative2/path.js'
				import 'package2/path.js'
			)"},

            {"/Users/user/project/src/relative/path.ts", R"(console.log('success'))"},
            {"/Users/user/project/src/relative/path.js", R"(console.log('FAILURE'))"},

            {"/Users/user/project/src/relative2/path.ts", R"(console.log('FAILURE'))"},
            {"/Users/user/project/src/relative2/path.js", R"(console.log('success'))"},

            {"/Users/user/project/node_modules/package/path.ts", R"(console.log('FAILURE'))"},
            {"/Users/user/project/node_modules/package/path.js", R"(console.log('success'))"},

            {"/Users/user/project/node_modules/package2/path.ts", R"(console.log('FAILURE'))"},
            {"/Users/user/project/node_modules/package2/path.js", R"(console.log('success'))"},
        },
        .entry_paths = {"/Users/user/project/src/main.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsManglePropsDefineSemantics) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					@dec(1) prop1 = null
					@dec(2) prop2_ = null
					@dec(3) ['prop3'] = null
					@dec(4) ['prop4_'] = null
					@dec(5) [/* @__KEY__ */ 'prop5'] = null
					@dec(6) [/* @__KEY__ */ 'prop6_'] = null
				}
			)"},
            {"/tsconfig.json", R"({
        "compilerOptions": {
          "experimentalDecorators": true,
          "useDefineForClassFields": true,
        },
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .MangleProps = std::make_shared<std::regex>("_$"),
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsManglePropsAssignSemantics) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					@dec(1) prop1 = null
					@dec(2) prop2_ = null
					@dec(3) ['prop3'] = null
					@dec(4) ['prop4_'] = null
					@dec(5) [/* @__KEY__ */ 'prop5'] = null
					@dec(6) [/* @__KEY__ */ 'prop6_'] = null
				}
			)"},
            {"/tsconfig.json", R"({
        "compilerOptions": {
          "experimentalDecorators": true,
          "useDefineForClassFields": false,
        },
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .MangleProps = std::make_shared<std::regex>("_$"),

            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsManglePropsMethods) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					@dec(1) prop1() {}
					@dec(2) prop2_() {}
					@dec(3) ['prop3']() {}
					@dec(4) ['prop4_']() {}
					@dec(5) [/* @__KEY__ */ 'prop5']() {}
					@dec(6) [/* @__KEY__ */ 'prop6_']() {}
				}
			)"},
            {"/tsconfig.json", R"({
        "compilerOptions": {
          "experimentalDecorators": true,
        },
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .MangleProps = std::make_shared<std::regex>("_$"),

            .BuildMode = guchho::config::Mode::kBundle,

            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsManglePropsStaticDefineSemantics) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					@dec(1) static prop1 = null
					@dec(2) static prop2_ = null
					@dec(3) static ['prop3'] = null
					@dec(4) static ['prop4_'] = null
					@dec(5) static [/* @__KEY__ */ 'prop5'] = null
					@dec(6) static [/* @__KEY__ */ 'prop6_'] = null
				}
			)"},
            {"/tsconfig.json", R"({
        "compilerOptions": {
          "experimentalDecorators": true,
          "useDefineForClassFields": true,
        },
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .MangleProps = std::make_shared<std::regex>("_$"),

            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsManglePropsStaticAssignSemantics) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					@dec(1) static prop1 = null
					@dec(2) static prop2_ = null
					@dec(3) static ['prop3'] = null
					@dec(4) static ['prop4_'] = null
					@dec(5) static [/* @__KEY__ */ 'prop5'] = null
					@dec(6) static [/* @__KEY__ */ 'prop6_'] = null
				}
			)"},
            {"/tsconfig.json", R"({
        "compilerOptions": {
          "experimentalDecorators": true,
          "useDefineForClassFields": false,
        },
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .MangleProps = std::make_shared<std::regex>("_$"),

            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSExperimentalDecoratorsManglePropsStaticMethods) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					@dec(1) static prop1() {}
					@dec(2) static prop2_() {}
					@dec(3) static ['prop3']() {}
					@dec(4) static ['prop4_']() {}
					@dec(5) static [/* @__KEY__ */ 'prop5']() {}
					@dec(6) static [/* @__KEY__ */ 'prop6_']() {}
				}
			)"},
            {"/tsconfig.json", R"({
        "compilerOptions": {
          "experimentalDecorators": true,
        },
			})"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .MangleProps = std::make_shared<std::regex>("_$"),

            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSPrintNonFiniteNumberInsideWith) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				// Use const enums to force inline values
				const enum Foo {
					NAN = 0 / 0,
					POS_INF = 1 / 0,
					NEG_INF = -1 / 0,
				}

				//! It's ok to use "NaN" and "Infinity" here
				console.log(
					Foo.NAN,
					Foo.POS_INF,
					Foo.NEG_INF,
				)
				checkPrecedence(
					1 / Foo.NAN,
					1 / Foo.POS_INF,
					1 / Foo.NEG_INF,
				)

				//! We must not use "NaN" or "Infinity" inside "with"
				with (x) {
					console.log(
						Foo.NAN,
						Foo.POS_INF,
						Foo.NEG_INF,
					)
					checkPrecedence(
						1 / Foo.NAN,
						1 / Foo.POS_INF,
						1 / Foo.NEG_INF,
					)
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, TSImportInNodeModulesNameCollisionWithCSS) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import "pkg"
			)"},
            {"/node_modules/pkg/index.ts", R"(
				import js_ts_css from "./js_ts_css"
				import ts_css from "./ts_css"
				import js_ts from "./js_ts"
				js_ts_css()
				ts_css()
				js_ts()
			)"},
            {"/node_modules/pkg/js_ts_css.js", R"(
				import './js_ts_css.css'
				export default function() {}
			)"},
            {"/node_modules/pkg/js_ts_css.ts", R"(
				TEST FAILED
			)"},
            {"/node_modules/pkg/js_ts_css.css", R"(
				.js_ts_css {}
			)"},
            {"/node_modules/pkg/ts_css.ts", R"(
				import './ts_css.css'
				export default function() {}
			)"},
            {"/node_modules/pkg/ts_css.css", R"(
				.ts_css {}
			)"},
            {"/node_modules/pkg/js_ts.js", R"(
				export default function() {}
			)"},
            {"/node_modules/pkg/js_ts.ts", R"(
				TEST FAILED
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTS, ParameterPropsUseDefineForClassFieldsTrue) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					static { console.log('a') }
					a = 1
					static { console.log('b') }
					constructor(public b1 = 2.1, public b2 = 2.2) {
					}
					static { console.log('c') }
					c = 3
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kTrue,
                },
            },
        },
    });
}

TEST(BundlerTS, ParameterPropsUseDefineForClassFieldsFalse) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					static { console.log('a') }
					a = 1
					static { console.log('b') }
					constructor(public b1 = 2.1, public b2 = 2.2) {
					}
					static { console.log('c') }
					c = 3
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kFalse,
                },
            },
        },
    });
}

TEST(BundlerTS, ParameterPropsUseDefineForClassFieldsTrueLowered) {
    ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				class Foo {
					static { console.log('a') }
					a = 1
					static { console.log('b') }
					constructor(public b1 = 2.1, public b2 = 2.2) {
					}
					static { console.log('c') }
					c = 3
				}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassField,

            .TS = guchho::config::TSOptions{
                .Config = guchho::config::TSConfig{
                    .UseDefineForClassFields = guchho::config::MaybeBool::kTrue,
                },
            },
        },
    });
}

} // namespace bundler::test
