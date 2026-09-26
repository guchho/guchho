#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

Suite importstar_ts_suite{"importstar_ts"};

TEST(BundlerImportStarTS, TSImportStarUnused) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarNoCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportImportStarUnused) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportImportStarNoCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportImportStarCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportStarAsUnused) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportStarAsNoCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportStarAsCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportStarUnused) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './bar'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				export * from './foo'
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportStarNoCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './bar'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				export * from './foo'
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarExportStarCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './bar'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
            {"/bar.ts", R"(
				export * from './foo'
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarCommonJSUnused) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.ts", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarCommonJSCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarCommonJSNoCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.ts", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarAndCommonJS) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				const ns2 = require('./foo')
				console.log(ns.foo, ns2.foo)
			)"},
            {"/foo.ts", R"(
				export const foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarNoBundleUnused) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarNoBundleCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarNoBundleNoCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarMangleNoBundleUnused) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarMangleNoBundleCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerImportStarTS, TSImportStarMangleNoBundleNoCapture) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerImportStarTS, TSReExportTypeOnlyFileES6) {
    importstar_ts_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"(
				import * as ns from './re-export'
				console.log(ns.foo)
			)"},
            {"/re-export.ts", R"(
				export * from './types1'
				export * from './types2'
				export * from './types3'
				export * from './values'
			)"},
            {"/types1.ts", R"(
				export interface Foo {}
				export type Bar = number
				console.log('some code')
			)"},
            {"/types2.ts", R"(
				import {Foo} from "./type"
				export {Foo}
				console.log('some code')
			)"},
            {"/types3.ts", R"(
				export {Foo} from "./type"
				console.log('some code')
			)"},
            {"/values.ts", R"(
				export let foo = 123
			)"},
            {"/type.ts", R"(
				export type Foo = number
			)"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}


} // namespace bundler::test
