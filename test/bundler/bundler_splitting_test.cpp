#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"


namespace bundler::test {

Suite splitting_suite{"splitting"};

TEST(BundlerSplitting, SplittingSharedES6IntoES6) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import {foo} from "./shared.js"
                console.log(foo)
            )"},
            {"/b.js", R"(
                import {foo} from "./shared.js"
                console.log(foo)
            )"},
            {"/shared.js", R"(export let foo = 123)"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingSharedCommonJSIntoES6) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                const {foo} = require("./shared.js")
                console.log(foo)
            )"},
            {"/b.js", R"(
                const {foo} = require("./shared.js")
                console.log(foo)
            )"},
            {"/shared.js", R"(exports.foo = 123)"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingDynamicES6IntoES6) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import("./foo.js").then(({bar}) => console.log(bar))
            )"},
            {"/foo.js", R"(
                export let bar = 123
            )"},
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

TEST(BundlerSplitting, SplittingDynamicCommonJSIntoES6) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import("./foo.js").then(({default: {bar}}) => console.log(bar))
            )"},
            {"/foo.js", R"(
                exports.bar = 123
            )"},
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

TEST(BundlerSplitting, SplittingDynamicAndNotDynamicES6IntoES6) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import {bar as a} from "./foo.js"
                import("./foo.js").then(({bar: b}) => console.log(a, b))
            )"},
            {"/foo.js", R"(
                export let bar = 123
            )"},
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

TEST(BundlerSplitting, SplittingDynamicAndNotDynamicCommonJSIntoES6) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import {bar as a} from "./foo.js"
                import("./foo.js").then(({default: {bar: b}}) => console.log(a, b))
            )"},
            {"/foo.js", R"(
                exports.bar = 123
            )"},
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

TEST(BundlerSplitting, SplittingAssignToLocal) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import {foo, setFoo} from "./shared.js"
                setFoo(123)
                console.log(foo)
            )"},
            {"/b.js", R"(
                import {foo} from "./shared.js"
                console.log(foo)
            )"},
            {"/shared.js", R"(
                export let foo
                export function setFoo(value) {
                    foo = value
                }
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingSideEffectsWithoutDependencies) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import {a} from "./shared.js"
                console.log(a)
            )"},
            {"/b.js", R"(
                import {b} from "./shared.js"
                console.log(b)
            )"},
            {"/shared.js", R"(
                export let a = 1
                export let b = 2
                console.log('side effect')
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingNestedDirectories) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/pages/pageA/page.js", R"(
                import x from "../shared.js"
                console.log(x)
            )"},
            {"/Users/user/project/src/pages/pageB/page.js", R"(
                import x from "../shared.js"
                console.log(-x)
            )"},
            {"/Users/user/project/src/pages/shared.js", R"(
                export default 123
            )"},
        },
        .entry_paths = {
            "/Users/user/project/src/pages/pageA/page.js",
            "/Users/user/project/src/pages/pageB/page.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerSplitting, SplittingCircularReferenceIssue251) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                export * from './b.js';
                export var p = 5;
            )"},
            {"/b.js", R"(
                export * from './a.js';
                export var q = 6;
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingMissingLazyExport) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import {foo} from './common.js'
                console.log(foo())
            )"},
            {"/b.js", R"(
                import {bar} from './common.js'
                console.log(bar())
            )"},
            {"/common.js", R"(
                import * as ns from './empty.js'
                export function foo() { return [ns, ns.missing] }
                export function bar() { return [ns.missing] }
            )"},
            {"/empty.js", R"(
                // This forces the module into ES6 mode without importing or exporting anything
                import.meta
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
        .expected_compile_log = R"(common.js: WARNING: Import "missing" will always be undefined because the file "empty.js" has no exports
)",
    });
}

TEST(BundlerSplitting, SplittingReExportIssue273) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                export const a = 1
            )"},
            {"/b.js", R"(
                export { a } from './a'
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingDynamicImportIssue272) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import('./b')
            )"},
            {"/b.js", R"(
                export default 1
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingDynamicImportOutsideSourceTreeIssue264) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry1.js", R"(
                import('package')
            )"},
            {"/Users/user/project/src/entry2.js", R"(
                import('package')
            )"},
            {"/Users/user/project/node_modules/package/index.js", R"(
                console.log('imported')
            )"},
        },
        .entry_paths = {
            "/Users/user/project/src/entry1.js",
            "/Users/user/project/src/entry2.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingCrossChunkAssignmentDependencies) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import {setValue} from './shared'
                setValue(123)
            )"},
            {"/b.js", R"(
                import './shared'
            )"},
            {"/shared.js", R"(
                var observer;
                var value;
                export function setObserver(cb) {
                    observer = cb;
                }
                export function getValue() {
                    return value;
                }
                export function setValue(next) {
                    value = next;
                    if (observer) observer();
                }
                sideEffects(getValue);
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingCrossChunkAssignmentDependenciesRecursive) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import { setX } from './x'
                setX()
            )"},
            {"/b.js", R"(
                import { setZ } from './z'
                setZ()
            )"},
            {"/c.js", R"(
                import { setX2 } from './x'
                import { setY2 } from './y'
                import { setZ2 } from './z'
                setX2();
                setY2();
                setZ2();
            )"},
            {"/x.js", R"(
                let _x
                export function setX(v) { _x = v }
                export function setX2(v) { _x = v }
            )"},
            {"/y.js", R"(
                import { setX } from './x'
                let _y
                export function setY(v) { _y = v }
                export function setY2(v) { setX(v); _y = v }
            )"},
            {"/z.js", R"(
                import { setY } from './y'
                let _z
                export function setZ(v) { _z = v }
                export function setZ2(v) { setY(v); _z = v }
            )"},
        },
        .entry_paths = {"/a.js", "/b.js", "/c.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingDuplicateChunkCollision) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import "./ab"
            )"},
            {"/b.js", R"(
                import "./ab"
            )"},
            {"/c.js", R"(
                import "./cd"
            )"},
            {"/d.js", R"(
                import "./cd"
            )"},
            {"/ab.js", R"(
                console.log(123)
            )"},
            {"/cd.js", R"(
                console.log(123)
            )"},
        },
        .entry_paths = {"/a.js", "/b.js", "/c.js", "/d.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
            .MinifyWhitespace = true,
        },
    });
}

TEST(BundlerSplitting, SplittingMinifyIdentifiersCrashIssue437) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import {foo} from "./shared"
                console.log(foo)
            )"},
            {"/b.js", R"(
                import {foo} from "./shared"
                console.log(foo)
            )"},
            {"/c.js", R"(
                import "./shared"
            )"},
            {"/shared.js", R"(
                export function foo(bar) {}
            )"},
        },
        .entry_paths = {"/a.js", "/b.js", "/c.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
            .MinifyIdentifiers = true,
            
        },
    });
}

TEST(BundlerSplitting, SplittingHybridESMAndCJSIssue617) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                export let foo
            )"},
            {"/b.js", R"(
                export let bar = require('./a')
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, SplittingPublicPathEntryName) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import("./b")
            )"},
            {"/b.js", R"(
                console.log('b')
            )"},
        },
        .entry_paths = {"/a.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
            .PublicPath = "/www/",
        },
    });
}

TEST(BundlerSplitting, SplittingChunkPathDirPlaceholderImplicitOutbase) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/project/entry.js", R"(
                console.log(import('./output-path/should-contain/this-text/file'))
            )"},
            {"/project/output-path/should-contain/this-text/file.js", R"(
                console.log('file.js')
            )"},
        },
        .entry_paths = {"/project/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
            .ChunkPathTemplate = {
                guchho::config::PathTemplate{.Data = "./", .Placeholder = guchho::config::PathPlaceholder::kDir},
                guchho::config::PathTemplate{.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
                guchho::config::PathTemplate{.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
            },
        },
    });
}

TEST(BundlerSplitting, EdgeCaseIssue2793WithSplitting) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/src/a.js", R"(
                export const A = 42;
            )"},
            {"/src/b.js", R"(
                export const B = async () => (await import(".")).A
            )"},
            {"/src/index.js", R"(
                export * from "./a"
                export * from "./b"
            )"},
        },
        .entry_paths = {"/src/index.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerSplitting, EdgeCaseIssue2793WithoutSplitting) {
    splitting_suite.ExpectBundled(Bundled{
        .files = {
            {"/src/a.js", R"(
                export const A = 42;
            )"},
            {"/src/b.js", R"(
                export const B = async () => (await import(".")).A
            )"},
            {"/src/index.js", R"(
                export * from "./a"
                export * from "./b"
            )"},
        },
        .entry_paths = {"/src/index.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputDir = "/out",
        },
    });
}

} // namespace bundler::test
