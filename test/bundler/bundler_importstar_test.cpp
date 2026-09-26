#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"


namespace bundler::test {

Suite importstar_suite{"importstar"};

TEST(BundlerImportStar, ImportStarUnused) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.js", R"(
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

TEST(BundlerImportStar, ImportStarCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.js", R"(
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

TEST(BundlerImportStar, ImportStarNoCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.js", R"(
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

TEST(BundlerImportStar, ImportStarExportImportStarUnused) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportImportStarNoCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportImportStarCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportStarAsUnused) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportStarAsNoCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportStarAsCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './bar'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportStarUnused) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './bar'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				export * from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportStarNoCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './bar'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				export * from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarExportStarCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './bar'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				export const foo = 123
			)"},
            {"/bar.js", R"(
				export * from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarCommonJSUnused) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
            {"/foo.js", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarCommonJSCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarCommonJSNoCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
            {"/foo.js", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarAndCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				const ns2 = require('./foo')
				console.log(ns.foo, ns2.foo)
			)"},
            {"/foo.js", R"(
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

TEST(BundlerImportStar, ImportStarNoBundleUnused) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarNoBundleCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarNoBundleNoCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarMangleNoBundleUnused) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(foo)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerImportStar, ImportStarMangleNoBundleCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerImportStar, ImportStarMangleNoBundleNoCapture) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				let foo = 234
				console.log(ns.foo, ns.foo, foo)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",

            .MinifySyntax = true,
        },
    });
}

TEST(BundlerImportStar, ImportStarExportStarOmitAmbiguous) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './common'
				console.log(ns)
			)"},
            {"/common.js", R"(
				export * from './foo'
				export * from './bar'
			)"},
            {"/foo.js", R"(
				export const x = 1
				export const y = 2
			)"},
            {"/bar.js", R"(
				export const y = 3
				export const z = 4
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}



TEST(BundlerImportStar, ReExportStarNameCollisionNotAmbiguousImport) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {x, y} from './common'
				console.log(x, y)
			)"},
            {"/common.js", R"(
				export * from './a'
				export * from './b'
			)"},
            {"/a.js", R"(
				export * from './c'
			)"},
            {"/b.js", R"(
				export {x} from './c'
			)"},
            {"/c.js", R"(
				export let x = 1, y = 2
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportStarNameCollisionNotAmbiguousExport) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from './a'
				export * from './b'
			)"},
            {"/a.js", R"(
				export * from './c'
			)"},
            {"/b.js", R"(
				export {x} from './c'
			)"},
            {"/c.js", R"(
				export let x = 1, y = 2
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportStarNameShadowingNotAmbiguous) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {x} from './a'
				console.log(x)
			)"},
            {"/a.js", R"(
				export * from './b'
				export let x = 1
			)"},
            {"/b.js", R"(
				export let x = 2
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportStarNameShadowingNotAmbiguousReExport) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {x} from './a'
				console.log(x)
			)"},
            {"/a.js", R"(
				export * from './b'
			)"},
            {"/b.js", R"(
				export * from './c'
				export let x = 1
			)"},
            {"/c.js", R"(
				export let x = 2
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportStarOfExportStarAs) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as foo_ns from './foo'
				console.log(foo_ns)
			)"},
            {"/foo.js", R"(
				export * as bar_ns from './bar'
			)"},
            {"/bar.js", R"(
				export const bar = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportOfExportStar) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {bar} from './foo'
				console.log(bar)
			)"},
            {"/foo.js", R"(
				export * from './bar'
			)"},
            {"/bar.js", R"(
				// Add some statements to increase the part index (this reproduced a crash)
				statement()
				statement()
				statement()
				statement()
				export const bar = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportOfExportStarOfImport) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {bar} from './foo'
				console.log(bar)
			)"},
            {"/foo.js", R"(
				// Add some statements to increase the part index (this reproduced a crash)
				statement()
				statement()
				statement()
				statement()
				export * from './bar'
			)"},
            {"/bar.js", R"(
				export {value as bar} from './baz'
			)"},
            {"/baz.js", R"(
				export const value = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfIIFE) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				export * from './entry'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfIIFEWithName) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				export * from './entry'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"someName"},
        },
    });
}

TEST(BundlerImportStar, ExportSelfES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				export * from './entry'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				export * from './entry'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfCommonJSMinified) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				module.exports = {foo: 123}
				console.log(require('./entry'))
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,            
            .AbsOutputFile = "/out.js",
            .MinifyIdentifiers = true,

        },
    });
}

TEST(BundlerImportStar, ImportSelfCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				exports.foo = 123
				import {foo} from './entry'
				console.log(foo)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfAsNamespaceES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				export * as ns from './entry'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportExportSelfAsNamespaceES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				import * as ns from './entry'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportOtherFileExportSelfAsNamespaceES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from './foo'
			)"},
            {"/foo.js", R"(
				export const foo = 123
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportOtherFileImportExportSelfAsNamespaceES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from './foo'
			)"},
            {"/foo.js", R"(
				export const foo = 123
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, OtherFileExportSelfAsNamespaceUnusedES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export {foo} from './foo'
			)"},
            {"/foo.js", R"(
				export const foo = 123
				export * as ns from './foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, OtherFileImportExportSelfAsNamespaceUnusedES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export {foo} from './foo'
			)"},
            {"/foo.js", R"(
				export const foo = 123
				import * as ns from './foo'
				export {ns}
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfAsNamespaceCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				export * as ns from './entry'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfAndRequireSelfCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export const foo = 123
				console.log(require('./entry'))
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportSelfAndImportSelfCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as x from './entry'
				export const foo = 123
				console.log(x)
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportOtherAsNamespaceCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * as ns from './foo'
			)"},
            {"/foo.js", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ImportExportOtherAsNamespaceCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				export {ns}
			)"},
            {"/foo.js", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, NamespaceImportMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns, ns.foo)
			)"},
            {"/foo.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(entry.js: WARNING: Import "foo" will always be undefined because there is no matching export in "foo.js"
)",
    });
}

TEST(BundlerImportStar, ExportOtherCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export {bar} from './foo'
			)"},
            {"/foo.js", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ExportOtherNestedCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export {y} from './bar'
			)"},
            {"/bar.js", R"(
				export {x as y} from './foo'
			)"},
            {"/foo.js", R"(
				exports.foo = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, NamespaceImportUnusedMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns.foo)
			)"},
            {"/foo.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(entry.js: WARNING: Import "foo" will always be undefined because there is no matching export in "foo.js"
)",
    });
}

TEST(BundlerImportStar, NamespaceImportMissingCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns, ns.foo)
			)"},
            {"/foo.js", R"(
				exports.x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, NamespaceImportUnusedMissingCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns.foo)
			)"},
            {"/foo.js", R"(
				exports.x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportNamespaceImportMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './foo'
				console.log(ns, ns.foo)
			)"},
            {"/foo.js", R"(
				export * as ns from './bar'
			)"},
            {"/bar.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportNamespaceImportUnusedMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {ns} from './foo'
				console.log(ns.foo)
			)"},
            {"/foo.js", R"(
				export * as ns from './bar'
			)"},
            {"/bar.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, NamespaceImportReExportMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns, ns.foo)
			)"},
            {"/foo.js", R"(
				export {foo} from './bar'
			)"},
            {"/bar.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(foo.js: ERROR: No matching export in "bar.js" for import "foo"
foo.js: ERROR: No matching export in "bar.js" for import "foo"
)",
    });
}

TEST(BundlerImportStar, NamespaceImportReExportUnusedMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns.foo)
			)"},
            {"/foo.js", R"(
				export {foo} from './bar'
			)"},
            {"/bar.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(foo.js: ERROR: No matching export in "bar.js" for import "foo"
foo.js: ERROR: No matching export in "bar.js" for import "foo"
)",
    });
}

TEST(BundlerImportStar, NamespaceImportReExportStarMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns, ns.foo)
			)"},
            {"/foo.js", R"(
				export * from './bar'
			)"},
            {"/bar.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(entry.js: WARNING: Import "foo" will always be undefined because there is no matching export in "foo.js"
)",
    });
}

TEST(BundlerImportStar, NamespaceImportReExportStarUnusedMissingES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './foo'
				console.log(ns.foo)
			)"},
            {"/foo.js", R"(
				export * from './bar'
			)"},
            {"/bar.js", R"(
				export const x = 123
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(entry.js: WARNING: Import "foo" will always be undefined because there is no matching export in "foo.js"
)",
    });
}

TEST(BundlerImportStar, ExportStarDefaultExportCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from './foo'
			)"},
            {"/foo.js", R"(
				export default 'default' // This should not be picked up
				export let foo = 'foo'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, Issue176) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as things from './folders'
				console.log(JSON.stringify(things))
			)"},
            {"/folders/index.js", R"(
				export * from "./child"
			)"},
            {"/folders/child/index.js", R"(
				export { foo } from './foo'
			)"},
            {"/folders/child/foo.js", R"(
				export const foo = () => 'hi there'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportStarExternalIIFE) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"mod"},
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"foo", true}},
                },
            },
        },
    });
}

TEST(BundlerImportStar, ReExportStarExternalES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"foo", true}},
                },
            },
        },
    });
}

TEST(BundlerImportStar, ReExportStarExternalCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"foo", true}},
                },
            },
        },
    });
}

TEST(BundlerImportStar, ReExportStarIIFENoBundle) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"mod"},
        },
    });
}

TEST(BundlerImportStar, ReExportStarES6NoBundle) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportStarCommonJSNoBundle) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportStarAsExternalIIFE) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * as out from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"mod"},
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"foo", true}},
                },
            },
        },
    });
}

TEST(BundlerImportStar, ReExportStarAsExternalES6) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * as out from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"foo", true}},
                },
            },
        },
    });
}

TEST(BundlerImportStar, ReExportStarAsExternalCommonJS) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * as out from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"foo", true}},
                },
            },
        },
    });
}

TEST(BundlerImportStar, ReExportStarAsIIFENoBundle) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * as out from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"mod"},
        },
    });
}

TEST(BundlerImportStar, ReExportStarAsES6NoBundle) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * as out from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerImportStar, ReExportStarAsCommonJSNoBundle) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * as out from "foo"
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
    });
}





TEST(BundlerImportStar, ImportNamespaceUndefinedPropertyEmptyFile) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry-nope.js", R"(
				import * as js from './empty.js'
				import * as mjs from './empty.mjs'
				import * as cjs from './empty.cjs'
				console.log(
					js.nope,
					mjs.nope,
					cjs.nope,
				)
			)"},

            // Note: For CommonJS-style modules, we automatically assign the exports
            // object to the "default" property if there is no property named "default".
            // This is for compatibility with node. So this test intentionally behaves
            // differently from the test above.
            {"/entry-default.js", R"(
				import * as js from './empty.js'
				import * as mjs from './empty.mjs'
				import * as cjs from './empty.cjs'
				console.log(
					js.default,
					mjs.default,
					cjs.default,
				)
			)"},

            {"/empty.js", R"()"},
            {"/empty.mjs", R"()"},
            {"/empty.cjs", R"()"},
        },
        .entry_paths = {
            "/entry-nope.js",
            "/entry-default.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_compile_log = R"(entry-default.js: WARNING: Import "default" will always be undefined because there is no matching export in "empty.mjs"
entry-nope.js: WARNING: Import "nope" will always be undefined because the file "empty.js" has no exports
entry-nope.js: WARNING: Import "nope" will always be undefined because the file "empty.mjs" has no exports
entry-nope.js: WARNING: Import "nope" will always be undefined because the file "empty.cjs" has no exports
)",
    });
}

TEST(BundlerImportStar, ImportNamespaceUndefinedPropertySideEffectFreeFile) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry-nope.js", R"(
				import * as js from './foo/no-side-effects.js'
				import * as mjs from './foo/no-side-effects.mjs'
				import * as cjs from './foo/no-side-effects.cjs'
				console.log(
					js.nope,
					mjs.nope,
					cjs.nope,
				)
			)"},

            // Note: For CommonJS-style modules, we automatically assign the exports
            // object to the "default" property if there is no property named "default".
            // This is for compatibility with node. So this test intentionally behaves
            // differently from the test above.
            {"/entry-default.js", R"(
				import * as js from './foo/no-side-effects.js'
				import * as mjs from './foo/no-side-effects.mjs'
				import * as cjs from './foo/no-side-effects.cjs'
				console.log(
					js.default,
					mjs.default,
					cjs.default,
				)
			)"},

            {"/foo/package.json", R"({ "sideEffects": false })"},
            {"/foo/no-side-effects.js", R"(console.log('js'))"},
            {"/foo/no-side-effects.mjs", R"(console.log('mjs'))"},
            {"/foo/no-side-effects.cjs", R"(console.log('cjs'))"},
        },
        .entry_paths = {
            "/entry-nope.js",
            "/entry-default.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_compile_log = R"(entry-default.js: WARNING: Import "default" will always be undefined because there is no matching export in "foo/no-side-effects.mjs"
entry-nope.js: WARNING: Import "nope" will always be undefined because the file "foo/no-side-effects.js" has no exports
entry-nope.js: WARNING: Import "nope" will always be undefined because the file "foo/no-side-effects.mjs" has no exports
entry-nope.js: WARNING: Import "nope" will always be undefined because the file "foo/no-side-effects.cjs" has no exports
)",
    });
}

TEST(BundlerImportStar, ReExportStarEntryPointAndInnerFile) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				export * from 'a'
				import * as inner from './inner.js'
				export { inner }
			)"},
            {"/inner.js", R"(
				export * from 'b'
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputDir = "/out",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {
                        {"a", true},
                        {"b", true},
                    },
                },
            },
            
        },
    });
}

TEST(BundlerImportStar, ImportExportStarAmbiguousError) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import {x, y, z} from './common'
				console.log(x, y, z)
			)"},
            {"/common.js", R"(
				export * from './foo'
				export * from './bar'
			)"},
            {"/foo.js", R"(
				export const x = 1
				export const y = 2
			)"},
            {"/bar.js", R"(
				export const y = 3
				export const z = 4
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(entry.js: ERROR: Ambiguous import "y" has multiple matching exports
foo.js: NOTE: One matching export is here:
bar.js: NOTE: Another matching export is here:
)",
    });
}

TEST(BundlerImportStar, ImportExportStarAmbiguousWarning) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import * as ns from './common'
				console.log(ns.x, ns.y, ns.z)
			)"},
            {"/common.js", R"(
				export * from './foo'
				export * from './bar'
			)"},
            {"/foo.js", R"(
				export const x = 1
				export const y = 2
			)"},
            {"/bar.js", R"(
				export const y = 3
				export const z = 4
			)"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_compile_log = R"(entry.js: WARNING: Import "y" will always be undefined because there are multiple matching exports
foo.js: NOTE: One matching export is here:
bar.js: NOTE: Another matching export is here:
)",
    });
}




TEST(BundlerImportStar, ImportDefaultNamespaceComboNoDefault) {
    // Note: "entry-dead.js" checks that this warning doesn't happen for dead code
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry-default-ns-prop.js", R"(import def, * as ns from './foo'; console.log(def, ns, ns.default))"},
            {"/entry-default-ns.js", R"(import def, * as ns from './foo'; console.log(def, ns))"},
            {"/entry-default-prop.js", R"(import def, * as ns from './foo'; console.log(def, ns.default))"},
            {"/entry-default.js", R"(import def from './foo'; console.log(def))"},
            {"/entry-prop.js", R"(import * as ns from './foo'; console.log(ns.default))"},
            {"/entry-dead.js", R"(import * as ns from './foo'; 0 && console.log(ns.default))"},
            {"/entry-typo.js", R"(import * as ns from './foo'; console.log(ns.buton))"},
            {"/entry-typo-indirect.js", R"(import * as ns from './indirect'; console.log(ns.buton))"},
            {"/foo.js", R"(export let button = {})"},
            {"/indirect.js", R"(export * from './foo')"},
        },
        .entry_paths = {
            "/entry-default-ns-prop.js",
            "/entry-default-ns.js",
            "/entry-default-prop.js",
            "/entry-default.js",
            "/entry-prop.js",
            "/entry-dead.js",
            "/entry-typo.js",
            "/entry-typo-indirect.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_compile_log = R"(entry-default-ns-prop.js: ERROR: No matching export in "foo.js" for import "default"
entry-default-ns-prop.js: WARNING: Import "default" will always be undefined because there is no matching export in "foo.js"
entry-default-ns.js: ERROR: No matching export in "foo.js" for import "default"
entry-default-prop.js: ERROR: No matching export in "foo.js" for import "default"
entry-default-prop.js: WARNING: Import "default" will always be undefined because there is no matching export in "foo.js"
entry-default.js: ERROR: No matching export in "foo.js" for import "default"
entry-prop.js: WARNING: Import "default" will always be undefined because there is no matching export in "foo.js"
entry-typo-indirect.js: WARNING: Import "buton" will always be undefined because there is no matching export in "indirect.js"
foo.js: NOTE: Did you mean to import "button" instead?
entry-typo.js: WARNING: Import "buton" will always be undefined because there is no matching export in "foo.js"
foo.js: NOTE: Did you mean to import "button" instead?
)",
    });
}

TEST(BundlerImportStar, ImportDefaultNamespaceComboIssue446) {
    importstar_suite.ExpectBundled(Bundled{
        .files = {
            {"/external-default2.js", R"(
				import def, {default as default2} from 'external'
				console.log(def, default2)
			)"},
            {"/external-ns.js", R"(
				import def, * as ns from 'external'
				console.log(def, ns)
			)"},
            {"/external-ns-default.js", R"(
				import def, * as ns from 'external'
				console.log(def, ns, ns.default)
			)"},
            {"/external-ns-def.js", R"(
				import def, * as ns from 'external'
				console.log(def, ns, ns.def)
			)"},
            {"/external-default.js", R"(
				import def, * as ns from 'external'
				console.log(def, ns.default)
			)"},
            {"/external-def.js", R"(
				import def, * as ns from 'external'
				console.log(def, ns.def)
			)"},
            {"/internal-default2.js", R"(
				import def, {default as default2} from './internal'
				console.log(def, default2)
			)"},
            {"/internal-ns.js", R"(
				import def, * as ns from './internal'
				console.log(def, ns)
			)"},
            {"/internal-ns-default.js", R"(
				import def, * as ns from './internal'
				console.log(def, ns, ns.default)
			)"},
            {"/internal-ns-def.js", R"(
				import def, * as ns from './internal'
				console.log(def, ns, ns.def)
			)"},
            {"/internal-default.js", R"(
				import def, * as ns from './internal'
				console.log(def, ns.default)
			)"},
            {"/internal-def.js", R"(
				import def, * as ns from './internal'
				console.log(def, ns.def)
			)"},
            {"/internal.js", R"(
				export default 123
			)"},
        },
        .entry_paths = {
            "/external-default2.js",
            "/external-ns.js",
            "/external-ns-default.js",
            "/external-ns-def.js",
            "/external-default.js",
            "/external-def.js",
            "/internal-default2.js",
            "/internal-ns.js",
            "/internal-ns-default.js",
            "/internal-ns-def.js",
            "/internal-default.js",
            "/internal-def.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"external", true}},
                },
            },
        },
        .expected_compile_log = R"(internal-def.js: WARNING: Import "def" will always be undefined because there is no matching export in "internal.js"
internal-ns-def.js: WARNING: Import "def" will always be undefined because there is no matching export in "internal.js"
)",
    });
}


} // namespace bundler::test
