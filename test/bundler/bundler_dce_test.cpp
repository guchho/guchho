#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

static Suite dce_suite{"dce"};

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseKeepNamedImportES6) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log(foo)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseKeepNamedImportCommonJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log(foo)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				exports.foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseKeepStarImportES6) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import * as ns from "demo-pkg"
				console.log(ns)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseKeepStarImportCommonJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import * as ns from "demo-pkg"
				console.log(ns)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				exports.foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsTrueKeepES6) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": true
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsTrueKeepCommonJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				exports.foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": true
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseKeepBareImportAndRequireES6) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg"
				require('demo-pkg')
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: WARNING: Ignoring this import because "Users/user/project/node_modules/demo-pkg/index.js" was marked as having no side effects
Users/user/project/node_modules/demo-pkg/package.json: NOTE: "sideEffects" is false in the enclosing "package.json" file:
)scan",
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseKeepBareImportAndRequireCommonJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg"
				require('demo-pkg')
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				exports.foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: WARNING: Ignoring this import because "Users/user/project/node_modules/demo-pkg/index.js" was marked as having no side effects
Users/user/project/node_modules/demo-pkg/package.json: NOTE: "sideEffects" is false in the enclosing "package.json" file:
)scan",
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseRemoveBareImportES6) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: WARNING: Ignoring this import because "Users/user/project/node_modules/demo-pkg/index.js" was marked as having no side effects
Users/user/project/node_modules/demo-pkg/package.json: NOTE: "sideEffects" is false in the enclosing "package.json" file:
)scan",
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseRemoveBareImportCommonJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				exports.foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: WARNING: Ignoring this import because "Users/user/project/node_modules/demo-pkg/index.js" was marked as having no side effects
Users/user/project/node_modules/demo-pkg/package.json: NOTE: "sideEffects" is false in the enclosing "package.json" file:
)scan",
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseRemoveNamedImportES6) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseRemoveNamedImportCommonJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				exports.foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseRemoveStarImportES6) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import * as ns from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseRemoveStarImportCommonJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import * as ns from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				exports.foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayRemove) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": []
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeep) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": ["./index.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepMainUseModule) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-main.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"module"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepMainUseMain) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('this should be kept')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-main.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"main"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepMainImplicitModule) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-main.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepMainImplicitMain) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				import "./require-demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/src/require-demo-pkg.js", R"test(

				// This causes "index-main.js" to be selected
				require('demo-pkg')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('this should be kept')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-main.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepModuleUseModule) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('this should be kept')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-module.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"module"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepModuleUseMain) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-module.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"main"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepModuleImplicitModule) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('this should be kept')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-module.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayKeepModuleImplicitMain) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				import "./require-demo-pkg"
				console.log('unused import')
			)test"},
			{"/Users/user/project/src/require-demo-pkg.js", R"test(

				// This causes "index-main.js" to be selected
				require('demo-pkg')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-main.js", R"test(

				export const foo = 123
				console.log('this should be kept')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index-module.js", R"test(

				export const foo = 123
				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "index-main.js",
					"module": "index-module.js",
					"sideEffects": ["./index-module.js"]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsArrayGlob) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg/keep/this/file"
				import "demo-pkg/remove/this/file"
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/keep/this/file.js", R"test(

				console.log('this should be kept')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/remove/this/file.js", R"test(

				console.log('TEST FAILED')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": [
						"./ke?p/*/file.js",
						"./remove/this/file.j",
						"./re?ve/this/file.js"
					]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: WARNING: Ignoring this import because "Users/user/project/node_modules/demo-pkg/remove/this/file.js" was marked as having no side effects
Users/user/project/node_modules/demo-pkg/package.json: NOTE: It was excluded from the "sideEffects" array in the enclosing "package.json" file:
)scan",
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsNestedDirectoryRemove) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg/a/b/c"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/a/b/c/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsKeepExportDefaultExpr) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import foo from "demo-pkg"
				console.log(foo)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export default exprWithSideEffects()
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseNoWarningInNodeModulesIssue999) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg"
				console.log('used import')
				)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				import "demo-pkg2"
				console.log('unused import')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg2/index.js", R"test(

				export const foo = 123
				console.log('hello')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg2/package.json", R"test(

				{
					"sideEffects": false
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseIntermediateFilesUnused) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export {foo} from "./foo.js"
				throw 'REMOVE THIS'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/foo.js", R"test(

				export const foo = 123
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{ "sideEffects": false }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseIntermediateFilesUsed) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "demo-pkg"
				console.log(foo)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				export {foo} from "./foo.js"
				throw 'keep this'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/foo.js", R"test(

				export const foo = 123
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{ "sideEffects": false }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseIntermediateFilesChainAll) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "a"
				console.log(foo)
			)test"},
			{"/Users/user/project/node_modules/a/index.js", R"test(

				export {foo} from "b"
			)test"},
			{"/Users/user/project/node_modules/a/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/b/index.js", R"test(

				export {foo} from "c"
				throw 'keep this'
			)test"},
			{"/Users/user/project/node_modules/b/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/c/index.js", R"test(

				export {foo} from "d"
			)test"},
			{"/Users/user/project/node_modules/c/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/d/index.js", R"test(

				export const foo = 123
			)test"},
			{"/Users/user/project/node_modules/d/package.json", R"test(

				{ "sideEffects": false }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseIntermediateFilesChainOne) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "a"
				console.log(foo)
			)test"},
			{"/Users/user/project/node_modules/a/index.js", R"test(

				export {foo} from "b"
			)test"},
			{"/Users/user/project/node_modules/b/index.js", R"test(

				export {foo} from "c"
				throw 'keep this'
			)test"},
			{"/Users/user/project/node_modules/b/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/c/index.js", R"test(

				export {foo} from "d"
			)test"},
			{"/Users/user/project/node_modules/d/index.js", R"test(

				export const foo = 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseIntermediateFilesDiamond) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {foo} from "a"
				console.log(foo)
			)test"},
			{"/Users/user/project/node_modules/a/index.js", R"test(

				export * from "b1"
				export * from "b2"
			)test"},
			{"/Users/user/project/node_modules/b1/index.js", R"test(

				export {foo} from "c"
				throw 'keep this 1'
			)test"},
			{"/Users/user/project/node_modules/b1/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/b2/index.js", R"test(

				export {foo} from "c"
				throw 'keep this 2'
			)test"},
			{"/Users/user/project/node_modules/b2/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/c/index.js", R"test(

				export {foo} from "d"
			)test"},
			{"/Users/user/project/node_modules/d/index.js", R"test(

				export const foo = 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseOneFork) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import("a").then(x => assert(x.foo === "foo"))
			)test"},
			{"/Users/user/project/node_modules/a/index.js", R"test(

				export {foo} from "b"
			)test"},
			{"/Users/user/project/node_modules/b/index.js", R"test(

				export {foo, bar} from "c"
				export {baz} from "d"
			)test"},
			{"/Users/user/project/node_modules/b/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/c/index.js", R"test(

				export let foo = "foo"
				export let bar = "bar"
			)test"},
			{"/Users/user/project/node_modules/d/index.js", R"test(

				export let baz = "baz"
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseAllFork) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import("a").then(x => assert(x.foo === "foo"))
			)test"},
			{"/Users/user/project/node_modules/a/index.js", R"test(

				export {foo} from "b"
			)test"},
			{"/Users/user/project/node_modules/b/index.js", R"test(

				export {foo, bar} from "c"
				export {baz} from "d"
			)test"},
			{"/Users/user/project/node_modules/b/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/c/index.js", R"test(

				export let foo = "foo"
				export let bar = "bar"
			)test"},
			{"/Users/user/project/node_modules/c/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/d/index.js", R"test(

				export let baz = "baz"
			)test"},
			{"/Users/user/project/node_modules/d/package.json", R"test(

				{ "sideEffects": false }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestJSONLoaderRemoveUnused) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import unused from "./example.json"
				console.log('unused import')
			)test"},
			{"/example.json", R"test({"data": true})test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestTextLoaderRemoveUnused) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import unused from "./example.txt"
				console.log('unused import')
			)test"},
			{"/example.txt", "some data"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestBase64LoaderRemoveUnused) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import unused from "./example.data"
				console.log('unused import')
			)test"},
			{"/example.data", "some data"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".data", guchho::config::Loader::kBase64},
			},
		},
	});
}

TEST(BundlerDCE, TestDataURLLoaderRemoveUnused) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import unused from "./example.data"
				console.log('unused import')
			)test"},
			{"/example.data", "some data"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".data", guchho::config::Loader::kDataURL},
			},
		},
	});
}

TEST(BundlerDCE, TestFileLoaderRemoveUnused) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import unused from "./example.data"
				console.log('unused import')
			)test"},
			{"/example.data", "some data"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".data", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerDCE, TestRemoveUnusedImportMeta) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function foo() {
					console.log(import.meta.url, import.meta.path)
				}
				console.log('foo is unused')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestRemoveUnusedPureCommentCalls) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function bar() {}
				let bare = foo(bar);

				let at_yes = /* @__PURE__ */ foo(bar);
				let at_no = /* @__PURE__ */ foo(bar());
				let new_at_yes = /* @__PURE__ */ new foo(bar);
				let new_at_no = /* @__PURE__ */ new foo(bar());

				let nospace_at_yes = /*@__PURE__*/ foo(bar);
				let nospace_at_no = /*@__PURE__*/ foo(bar());
				let nospace_new_at_yes = /*@__PURE__*/ new foo(bar);
				let nospace_new_at_no = /*@__PURE__*/ new foo(bar());

				let num_yes = /* #__PURE__ */ foo(bar);
				let num_no = /* #__PURE__ */ foo(bar());
				let new_num_yes = /* #__PURE__ */ new foo(bar);
				let new_num_no = /* #__PURE__ */ new foo(bar());

				let nospace_num_yes = /*#__PURE__*/ foo(bar);
				let nospace_num_no = /*#__PURE__*/ foo(bar());
				let nospace_new_num_yes = /*#__PURE__*/ new foo(bar);
				let nospace_new_num_no = /*#__PURE__*/ new foo(bar());

				let dot_yes = /* @__PURE__ */ foo(sideEffect()).dot(bar);
				let dot_no = /* @__PURE__ */ foo(sideEffect()).dot(bar());
				let new_dot_yes = /* @__PURE__ */ new foo(sideEffect()).dot(bar);
				let new_dot_no = /* @__PURE__ */ new foo(sideEffect()).dot(bar());

				let nested_yes = [1, /* @__PURE__ */ foo(bar), 2];
				let nested_no = [1, /* @__PURE__ */ foo(bar()), 2];
				let new_nested_yes = [1, /* @__PURE__ */ new foo(bar), 2];
				let new_nested_no = [1, /* @__PURE__ */ new foo(bar()), 2];

				let single_at_yes = // @__PURE__
					foo(bar);
				let single_at_no = // @__PURE__
					foo(bar());
				let new_single_at_yes = // @__PURE__
					new foo(bar);
				let new_single_at_no = // @__PURE__
					new foo(bar());

				let single_num_yes = // #__PURE__
					foo(bar);
				let single_num_no = // #__PURE__
					foo(bar());
				let new_single_num_yes = // #__PURE__
					new foo(bar);
				let new_single_num_no = // #__PURE__
					new foo(bar());

				let bad_no = /* __PURE__ */ foo(bar);
				let new_bad_no = /* __PURE__ */ new foo(bar);

				let parens_no = (/* @__PURE__ */ foo)(bar);
				let new_parens_no = new (/* @__PURE__ */ foo)(bar);

				let exp_no = /* @__PURE__ */ foo() ** foo();
				let new_exp_no = /* @__PURE__ */ new foo() ** foo();
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestRemoveUnusedNoSideEffectsTaggedTemplates) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// @__NO_SIDE_EFFECTS__
				function foo() {}

				foo`remove`;
				foo`remove${null}`;
				foo`remove${123}`;

				use(foo`keep`);
				foo`remove this part ${keep} and this ${alsoKeep}`;
				`remove this part ${keep} and this ${alsoKeep}`;
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

TEST(BundlerDCE, TestTreeShakingReactElements) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				function Foo() {}

				let a = <div/>
				let b = <Foo>{a}</Foo>
				let c = <>{b}</>

				let d = <div/>
				let e = <Foo>{d}</Foo>
				let f = <>{e}</>
				console.log(f)
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestDisableTreeShaking) {
auto defines = guchho::config::ProcessDefines({
	{.KeyParts = {"pure"}, .DefineExprData={}, .Flags = guchho::config::DefineFlags::kCallCanBeUnwrappedIfUnused},
	{.KeyParts = {"some", "fn"}, .DefineExprData={}, .Flags = guchho::config::DefineFlags::kCallCanBeUnwrappedIfUnused},
});

	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				import './remove-me'
				function RemoveMe1() {}
				let removeMe2 = 0
				class RemoveMe3 {}

				import './keep-me'
				function KeepMe1() {}
				let keepMe2 = <KeepMe1/>
				function keepMe3() { console.log('side effects') }
				let keepMe4 = /* @__PURE__ */ keepMe3()
				let keepMe5 = pure()
				let keepMe6 = some.fn()
			)test"},
			{"/remove-me.js", R"test(

				export default 'unused'
			)test"},
			{"/keep-me/index.js", R"test(

				console.log('side effects')
			)test"},
			{"/keep-me/package.json", R"test(

				{ "sideEffects": false }
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.IgnoreDCEAnnotations = true,
		},
	});
}

TEST(BundlerDCE, TestDeadCodeFollowingJump) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function testReturn() {
					if (true) return y + z()
					if (FAIL) return FAIL
					if (x) { var y }
					function z() { KEEP_ME() }
					return FAIL
				}

				function testThrow() {
					if (true) throw y + z()
					if (FAIL) return FAIL
					if (x) { var y }
					function z() { KEEP_ME() }
					return FAIL
				}

				function testBreak() {
					while (true) {
						if (true) {
							y + z()
							break
						}
						if (FAIL) return FAIL
						if (x) { var y }
						function z() { KEEP_ME() }
						return FAIL
					}
				}

				function testContinue() {
					while (true) {
						if (true) {
							y + z()
							continue
						}
						if (FAIL) return FAIL
						if (x) { var y }
						function z() { KEEP_ME() }
						return FAIL
					}
				}

				function testStmts() {
					return [a, b, c, d, e, f, g, h, i]

					while (x) { var a }
					while (FAIL) { let FAIL }

					do { var b } while (x)
					do { let FAIL } while (FAIL)

					for (var c; ;) ;
					for (let FAIL; ;) ;

					for (var d in x) ;
					for (let FAIL in FAIL) ;

					for (var e of x) ;
					for (let FAIL of FAIL) ;

					if (x) { var f }
					if (FAIL) { let FAIL }

					if (x) ; else { var g }
					if (FAIL) ; else { let FAIL }

					{ var h }
					{ let FAIL }

					x: { var i }
					x: { let FAIL }
				}

				testReturn()
				testThrow()
				testBreak()
				testContinue()
				testStmts()
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

TEST(BundlerDCE, TestDeadCodeInsideEmptyTry) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				try { foo() }
				catch { require('./a') }
				finally { require('./b') }

				try {}
				catch { require('./c') }
				finally { require('./d') }
			)test"},
			{"/a.js", ""},
			{"/b.js", ""},
			{"/c.js", "TEST FAILED"},
			{"/d.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestDeadCodeInsideUnusedCases) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Unknown test value
				switch (x) {
					case 0: _ = require('./a'); break
					case 1: _ = require('./b'); break
				}

				// Known test value
				switch (1) {
					case 0: _ = require('./FAIL-known-0'); break
					case 1: _ = require('./a'); break
					case 1: _ = require('./FAIL-known-1'); break
					case 2: _ = require('./FAIL-known-2'); break
				}

				// Check for "default"
				switch (0) {
					case 1: _ = require('./FAIL-default-1'); break
					default: _ = require('./a'); break
				}
				switch (1) {
					case 1: _ = require('./a'); break
					default: _ = require('./FAIL-default'); break
				}
				switch (0) {
					case 1: _ = require('./FAIL-default-1'); break
					default: _ = require('./FAIL-default'); break
					case 0: _ = require('./a'); break
				}

				// Check for non-constant cases
				switch (1) {
					case x: _ = require('./a'); break
					case 1: _ = require('./b'); break
					case x: _ = require('./FAIL-x'); break
					default: _ = require('./FAIL-x-default'); break
				}

				// Check for other kinds of jumps
				for (const x of y)
					switch (1) {
						case 0: _ = require('./FAIL-continue-0'); continue
						case 1: _ = require('./a'); continue
						case 2: _ = require('./FAIL-continue-2'); continue
					}
				x = () => {
					switch (1) {
						case 0: _ = require('./FAIL-return-0'); return
						case 1: _ = require('./a'); return
						case 2: _ = require('./FAIL-return-2'); return
					}
				}

				// Check for fall-through
				switch ('b') {
					case 'a': _ = require('./FAIL-fallthrough-a')
					case 'b': _ = require('./a')
					case 'c': _ = require('./b'); break
					case 'd': _ = require('./FAIL-fallthrough-d')
				}
				switch ('b') {
					case 'a': _ = require('./FAIL-fallthrough-a')
					case 'b':
					case 'c': _ = require('./a')
					case 'd': _ = require('./b'); break
					case 'e': _ = require('./FAIL-fallthrough-e')
				}
			)test"},
			{"/a.js", ""},
			{"/b.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
entry.js: WARNING: This case clause will never be evaluated because it duplicates an earlier case clause
entry.js: NOTE: The earlier case clause is here:
entry.js: WARNING: This case clause will never be evaluated because it duplicates an earlier case clause
entry.js: NOTE: The earlier case clause is here:
)scan",
	});
}

TEST(BundlerDCE, TestRemoveTrailingReturn) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function foo() {
					if (a) b()
					return
				}
				function bar() {
					if (a) b()
					return KEEP_ME
				}
				export default [
					foo,
					bar,
					function () {
						if (a) b()
						return
					},
					function () {
						if (a) b()
						return KEEP_ME
					},
					() => {
						if (a) b()
						return
					},
					() => {
						if (a) b()
						return KEEP_ME
					},
				]
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestImportReExportOfNamespaceImport) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/entry.js", R"test(

				import * as ns from 'pkg'
				console.log(ns.foo)
			)test"},
			{"/Users/user/project/node_modules/pkg/index.js", R"test(

				export { default as foo } from './foo'
				export { default as bar } from './bar'
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{ "sideEffects": false }
			)test"},
			{"/Users/user/project/node_modules/pkg/foo.js", R"test(

				module.exports = 123
			)test"},
			{"/Users/user/project/node_modules/pkg/bar.js", R"test(

				module.exports = 'abc'
			)test"},
		},
		.entry_paths = {"/Users/user/project/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingImportIdentifier) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import * as a from './a'
				new a.Keep()
			)test"},
			{"/a.js", R"test(

				import * as b from './b'
				export class Keep extends b.Base {}
				export class REMOVE extends b.Base {}
			)test"},
			{"/b.js", R"test(

				export class Base {}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingObjectProperty) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				let remove1 = { x: 'x' }
				let remove2 = { x() {} }
				let remove3 = { get x() {} }
				let remove4 = { set x(_) {} }
				let remove5 = { async x() {} }
				let remove6 = { ['x']: 'x' }
				let remove7 = { ['x']() {} }
				let remove8 = { get ['x']() {} }
				let remove9 = { set ['x'](_) {} }
				let remove10 = { async ['x']() {} }
				let remove11 = { [0]: 'x' }
				let remove12 = { [null]: 'x' }
				let remove13 = { [undefined]: 'x' }
				let remove14 = { [false]: 'x' }
				let remove15 = { [0n]: 'x' }
				let remove16 = { toString() {} }

				let keep1 = { x }
				let keep2 = { x: x }
				let keep3 = { ...x }
				let keep4 = { [x]: 'x' }
				let keep5 = { [x]() {} }
				let keep6 = { get [x]() {} }
				let keep7 = { set [x](_) {} }
				let keep8 = { async [x]() {} }
				let keep9 = { [{ toString() {} }]: 'x' }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.TreeShaking = true,
		},
	});
}

TEST(BundlerDCE, TestTreeShakingClassProperty) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				let remove1 = class { x }
				let remove2 = class { x = x }
				let remove3 = class { x() {} }
				let remove4 = class { get x() {} }
				let remove5 = class { set x(_) {} }
				let remove6 = class { async x() {} }
				let remove7 = class { ['x'] = x }
				let remove8 = class { ['x']() {} }
				let remove9 = class { get ['x']() {} }
				let remove10 = class { set ['x'](_) {} }
				let remove11 = class { async ['x']() {} }
				let remove12 = class { [0] = 'x' }
				let remove13 = class { [null] = 'x' }
				let remove14 = class { [undefined] = 'x' }
				let remove15 = class { [false] = 'x' }
				let remove16 = class { [0n] = 'x' }
				let remove17 = class { toString() {} }

				let keep1 = class { [x] = 'x' }
				let keep2 = class { [x]() {} }
				let keep3 = class { get [x]() {} }
				let keep4 = class { set [x](_) {} }
				let keep5 = class { async [x]() {} }
				let keep6 = class { [{ toString() {} }] = 'x' }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.TreeShaking = true,
		},
	});
}

TEST(BundlerDCE, TestTreeShakingClassStaticProperty) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				let remove1 = class { static x }
				let remove3 = class { static x() {} }
				let remove4 = class { static get x() {} }
				let remove5 = class { static set x(_) {} }
				let remove6 = class { static async x() {} }
				let remove8 = class { static ['x']() {} }
				let remove9 = class { static get ['x']() {} }
				let remove10 = class { static set ['x'](_) {} }
				let remove11 = class { static async ['x']() {} }
				let remove12 = class { static [0] = 'x' }
				let remove13 = class { static [null] = 'x' }
				let remove14 = class { static [undefined] = 'x' }
				let remove15 = class { static [false] = 'x' }
				let remove16 = class { static [0n] = 'x' }
				let remove17 = class { static toString() {} }

				let keep1 = class { static x = x }
				let keep2 = class { static ['x'] = x }
				let keep3 = class { static [x] = 'x' }
				let keep4 = class { static [x]() {} }
				let keep5 = class { static get [x]() {} }
				let keep6 = class { static set [x](_) {} }
				let keep7 = class { static async [x]() {} }
				let keep8 = class { static [{ toString() {} }] = 'x' }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.TreeShaking = true,
		},
	});
}

TEST(BundlerDCE, TestTreeShakingUnaryOperators) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// These operators may have side effects
				let keep;
				+keep;
				-keep;
				~keep;
				delete keep;
				++keep;
				--keep;
				keep++;
				keep--;

				// These operators never have side effects
				let REMOVE;
				!REMOVE;
				void REMOVE;
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

TEST(BundlerDCE, TestTreeShakingBinaryOperators) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// These operators may have side effects
				let keep, keep2;
				keep + keep2;
				keep - keep2;
				keep * keep2;
				keep / keep2;
				keep % keep2;
				keep ** keep2;
				keep < keep2;
				keep <= keep2;
				keep > keep2;
				keep >= keep2;
				keep in keep2;
				keep instanceof keep2;
				keep << keep2;
				keep >> keep2;
				keep >>> keep2;
				keep == keep2;
				keep != keep2;
				keep | keep2;
				keep & keep2;
				keep ^ keep2;
				keep = keep2;
				keep += keep2;
				keep -= keep2;
				keep *= keep2;
				keep /= keep2;
				keep %= keep2;
				keep **= keep2;
				keep <<= keep2;
				keep >>= keep2;
				keep >>>= keep2;
				keep |= keep2;
				keep &= keep2;
				keep ^= keep2;
				keep ??= keep2;
				keep ||= keep2;
				keep &&= keep2;

				// These operators never have side effects
				let REMOVE, REMOVE2;
				REMOVE === REMOVE2;
				REMOVE !== REMOVE2;
				REMOVE, REMOVE2;
				REMOVE ?? REMOVE2;
				REMOVE || REMOVE2;
				REMOVE && REMOVE2;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingNoBundleESM) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function keep() {}
				function unused() {}
				keep()
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

TEST(BundlerDCE, TestTreeShakingNoBundleCJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function keep() {}
				function unused() {}
				keep()
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

TEST(BundlerDCE, TestTreeShakingNoBundleIIFE) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function keep() {}
				function REMOVE() {}
				keep()
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

TEST(BundlerDCE, TestTreeShakingInESMWrapper) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import {keep1} from './lib'
				console.log(keep1(), require('./cjs'))
			)test"},
			{"/cjs.js", R"test(

				import {keep2} from './lib'
				export default keep2()
			)test"},
			{"/lib.js", R"test(

				export let keep1 = () => 'keep1'
				export let keep2 = () => 'keep2'
				export let REMOVE = () => 'REMOVE'
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

TEST(BundlerDCE, TestDCETypeOf) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// These should be removed because they have no side effects
				typeof x_REMOVE
				typeof v_REMOVE
				typeof f_REMOVE
				typeof g_REMOVE
				typeof a_REMOVE
				var v_REMOVE
				function f_REMOVE() {}
				function* g_REMOVE() {}
				async function a_REMOVE() {}

				// These technically have side effects due to TDZ, but this is not currently handled
				typeof c_remove
				typeof l_remove
				typeof s_remove
				const c_remove = 0
				let l_remove
				class s_remove {}
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

TEST(BundlerDCE, TestDCETypeOfEqualsString) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				var hasBar = typeof bar !== 'undefined'
				if (false) console.log(hasBar)
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

TEST(BundlerDCE, TestDCETypeOfEqualsStringMangle) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Everything here should be removed as dead code due to tree shaking
				var hasBar = typeof bar !== 'undefined'
				if (false) console.log(hasBar)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestDCETypeOfEqualsStringGuardCondition) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Everything here should be removed as dead code due to tree shaking
				var REMOVE_1 = typeof x !== 'undefined' ? x : null
				var REMOVE_1 = typeof x != 'undefined' ? x : null
				var REMOVE_1 = typeof x === 'undefined' ? null : x
				var REMOVE_1 = typeof x == 'undefined' ? null : x
				var REMOVE_1 = typeof x !== 'undefined' && x
				var REMOVE_1 = typeof x != 'undefined' && x
				var REMOVE_1 = typeof x === 'undefined' || x
				var REMOVE_1 = typeof x == 'undefined' || x
				var REMOVE_1 = 'undefined' !== typeof x ? x : null
				var REMOVE_1 = 'undefined' != typeof x ? x : null
				var REMOVE_1 = 'undefined' === typeof x ? null : x
				var REMOVE_1 = 'undefined' == typeof x ? null : x
				var REMOVE_1 = 'undefined' !== typeof x && x
				var REMOVE_1 = 'undefined' != typeof x && x
				var REMOVE_1 = 'undefined' === typeof x || x
				var REMOVE_1 = 'undefined' == typeof x || x

				// Everything here should be removed as dead code due to tree shaking
				var REMOVE_2 = typeof x === 'object' ? x : null
				var REMOVE_2 = typeof x == 'object' ? x : null
				var REMOVE_2 = typeof x !== 'object' ? null : x
				var REMOVE_2 = typeof x != 'object' ? null : x
				var REMOVE_2 = typeof x === 'object' && x
				var REMOVE_2 = typeof x == 'object' && x
				var REMOVE_2 = typeof x !== 'object' || x
				var REMOVE_2 = typeof x != 'object' || x
				var REMOVE_2 = 'object' === typeof x ? x : null
				var REMOVE_2 = 'object' == typeof x ? x : null
				var REMOVE_2 = 'object' !== typeof x ? null : x
				var REMOVE_2 = 'object' != typeof x ? null : x
				var REMOVE_2 = 'object' === typeof x && x
				var REMOVE_2 = 'object' == typeof x && x
				var REMOVE_2 = 'object' !== typeof x || x
				var REMOVE_2 = 'object' != typeof x || x

				// Everything here should be kept as live code because it has side effects
				var keep_1 = typeof x !== 'object' ? x : null
				var keep_1 = typeof x != 'object' ? x : null
				var keep_1 = typeof x === 'object' ? null : x
				var keep_1 = typeof x == 'object' ? null : x
				var keep_1 = typeof x !== 'object' && x
				var keep_1 = typeof x != 'object' && x
				var keep_1 = typeof x === 'object' || x
				var keep_1 = typeof x == 'object' || x
				var keep_1 = 'object' !== typeof x ? x : null
				var keep_1 = 'object' != typeof x ? x : null
				var keep_1 = 'object' === typeof x ? null : x
				var keep_1 = 'object' == typeof x ? null : x
				var keep_1 = 'object' !== typeof x && x
				var keep_1 = 'object' != typeof x && x
				var keep_1 = 'object' === typeof x || x
				var keep_1 = 'object' == typeof x || x

				// Everything here should be kept as live code because it has side effects
				var keep_2 = typeof x !== 'undefined' ? y : null
				var keep_2 = typeof x != 'undefined' ? y : null
				var keep_2 = typeof x === 'undefined' ? null : y
				var keep_2 = typeof x == 'undefined' ? null : y
				var keep_2 = typeof x !== 'undefined' && y
				var keep_2 = typeof x != 'undefined' && y
				var keep_2 = typeof x === 'undefined' || y
				var keep_2 = typeof x == 'undefined' || y
				var keep_2 = 'undefined' !== typeof x ? y : null
				var keep_2 = 'undefined' != typeof x ? y : null
				var keep_2 = 'undefined' === typeof x ? null : y
				var keep_2 = 'undefined' == typeof x ? null : y
				var keep_2 = 'undefined' !== typeof x && y
				var keep_2 = 'undefined' != typeof x && y
				var keep_2 = 'undefined' === typeof x || y
				var keep_2 = 'undefined' == typeof x || y

				// Everything here should be kept as live code because it has side effects
				var keep_3 = typeof x !== 'undefined' ? null : x
				var keep_3 = typeof x != 'undefined' ? null : x
				var keep_3 = typeof x === 'undefined' ? x : null
				var keep_3 = typeof x == 'undefined' ? x : null
				var keep_3 = typeof x !== 'undefined' || x
				var keep_3 = typeof x != 'undefined' || x
				var keep_3 = typeof x === 'undefined' && x
				var keep_3 = typeof x == 'undefined' && x
				var keep_3 = 'undefined' !== typeof x ? null : x
				var keep_3 = 'undefined' != typeof x ? null : x
				var keep_3 = 'undefined' === typeof x ? x : null
				var keep_3 = 'undefined' == typeof x ? x : null
				var keep_3 = 'undefined' !== typeof x || x
				var keep_3 = 'undefined' != typeof x || x
				var keep_3 = 'undefined' === typeof x && x
				var keep_3 = 'undefined' == typeof x && x
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

TEST(BundlerDCE, TestDCETypeOfCompareStringGuardCondition) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Everything here should be removed as dead code due to tree shaking
				var REMOVE_1 = typeof x <= 'u' ? x : null
				var REMOVE_1 = typeof x < 'u' ? x : null
				var REMOVE_1 = typeof x >= 'u' ? null : x
				var REMOVE_1 = typeof x > 'u' ? null : x
				var REMOVE_1 = typeof x <= 'u' && x
				var REMOVE_1 = typeof x < 'u' && x
				var REMOVE_1 = typeof x >= 'u' || x
				var REMOVE_1 = typeof x > 'u' || x
				var REMOVE_1 = 'u' >= typeof x ? x : null
				var REMOVE_1 = 'u' > typeof x ? x : null
				var REMOVE_1 = 'u' <= typeof x ? null : x
				var REMOVE_1 = 'u' < typeof x ? null : x
				var REMOVE_1 = 'u' >= typeof x && x
				var REMOVE_1 = 'u' > typeof x && x
				var REMOVE_1 = 'u' <= typeof x || x
				var REMOVE_1 = 'u' < typeof x || x

				// Everything here should be kept as live code because it has side effects
				var keep_1 = typeof x <= 'u' ? y : null
				var keep_1 = typeof x < 'u' ? y : null
				var keep_1 = typeof x >= 'u' ? null : y
				var keep_1 = typeof x > 'u' ? null : y
				var keep_1 = typeof x <= 'u' && y
				var keep_1 = typeof x < 'u' && y
				var keep_1 = typeof x >= 'u' || y
				var keep_1 = typeof x > 'u' || y
				var keep_1 = 'u' >= typeof x ? y : null
				var keep_1 = 'u' > typeof x ? y : null
				var keep_1 = 'u' <= typeof x ? null : y
				var keep_1 = 'u' < typeof x ? null : y
				var keep_1 = 'u' >= typeof x && y
				var keep_1 = 'u' > typeof x && y
				var keep_1 = 'u' <= typeof x || y
				var keep_1 = 'u' < typeof x || y

				// Everything here should be kept as live code because it has side effects
				var keep_2 = typeof x <= 'u' ? null : x
				var keep_2 = typeof x < 'u' ? null : x
				var keep_2 = typeof x >= 'u' ? x : null
				var keep_2 = typeof x > 'u' ? x : null
				var keep_2 = typeof x <= 'u' || x
				var keep_2 = typeof x < 'u' || x
				var keep_2 = typeof x >= 'u' && x
				var keep_2 = typeof x > 'u' && x
				var keep_2 = 'u' >= typeof x ? null : x
				var keep_2 = 'u' > typeof x ? null : x
				var keep_2 = 'u' <= typeof x ? x : null
				var keep_2 = 'u' < typeof x ? x : null
				var keep_2 = 'u' >= typeof x || x
				var keep_2 = 'u' > typeof x || x
				var keep_2 = 'u' <= typeof x && x
				var keep_2 = 'u' < typeof x && x
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

TEST(BundlerDCE, TestRemoveUnusedImports) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import a from 'a'
				import * as b from 'b'
				import {c} from 'c'
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

TEST(BundlerDCE, TestRemoveUnusedImportsEval) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import a from 'a'
				import * as b from 'b'
				import {c} from 'c'
				eval('foo(a, b, c)')
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

TEST(BundlerDCE, TestRemoveUnusedImportsEvalTS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				import a from 'a'
				import * as b from 'b'
				import {c} from 'c'
				eval('foo(a, b, c)')
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

TEST(BundlerDCE, TestDCEClassStaticBlocks) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				class A_REMOVE {
					static {}
				}
				class B_REMOVE {
					static { 123 }
				}
				class C_REMOVE {
					static { /* @__PURE__*/ foo() }
				}
				class D_REMOVE {
					static { try {} catch {} }
				}
				class E_REMOVE {
					static { try { /* @__PURE__*/ foo() } catch {} }
				}
				class F_REMOVE {
					static { try { 123 } catch { 123 } finally { 123 } }
				}

				class A_keep {
					static { foo }
				}
				class B_keep {
					static { this.foo }
				}
				class C_keep {
					static { try { foo } catch {} }
				}
				class D_keep {
					static { try {} finally { foo } }
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

TEST(BundlerDCE, TestDCEClassStaticBlocksMinifySyntax) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				class A_REMOVE {
					static {}
				}
				class B_REMOVE {
					static { 123 }
				}
				class C_REMOVE {
					static { /* @__PURE__*/ foo() }
				}
				class D_REMOVE {
					static { try {} catch {} }
				}
				class E_REMOVE {
					static { try { /* @__PURE__*/ foo() } catch {} }
				}
				class F_REMOVE {
					static { try { 123 } catch { 123 } finally { 123 } }
				}

				class A_keep {
					static { foo }
				}
				class B_keep {
					static { this.foo }
				}
				class C_keep {
					static { try { foo } catch {} }
				}
				class D_keep {
					static { try {} finally { foo } }
				}
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

TEST(BundlerDCE, TestDCEVarExports) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				var foo = { bar: 123 }
				module.exports = foo
			)test"},
			{"/b.js", R"test(

				var exports = { bar: 123 }
				module.exports = exports
			)test"},
			{"/c.js", R"test(

				var module = { bar: 123 }
				exports.foo = module
			)test"},
		},
		.entry_paths = {
			"/a.js",
			"/b.js",
			"/c.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCETemplateLiteral) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingLoweredClassStaticField) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class REMOVE_ME {
					static x = 'x'
					static y = 'y'
					static z = 'z'
				}
				function REMOVE_ME_TOO() {
					new REMOVE_ME()
				}
				class KeepMe1 {
					static x = 'x'
					static y = sideEffects()
					static z = 'z'
				}
				class KeepMe2 {
					static x = 'x'
					static y = 'y'
					static z = 'z'
				}
				new KeepMe2()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kClassStaticField,
		},
	});
}

TEST(BundlerDCE, TestTreeShakingLoweredClassStaticFieldMinified) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class REMOVE_ME {
					static x = 'x'
					static y = 'y'
					static z = 'z'
				}
				function REMOVE_ME_TOO() {
					new REMOVE_ME()
				}
				class KeepMe1 {
					static x = 'x'
					static y = sideEffects()
					static z = 'z'
				}
				class KeepMe2 {
					static x = 'x'
					static y = 'y'
					static z = 'z'
				}
				new KeepMe2()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kClassStaticField,
		},
	});
}

TEST(BundlerDCE, TestTreeShakingLoweredClassStaticFieldAssignment) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				class KeepMe1 {
					static x = 'x'
					static y = 'y'
					static z = 'z'
				}
				class KeepMe2 {
					static x = 'x'
					static y = sideEffects()
					static z = 'z'
				}
				class KeepMe3 {
					static x = 'x'
					static y = 'y'
					static z = 'z'
				}
				new KeepMe3()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kClassStaticField,
			.TS = guchho::config::TSOptions{.Config = guchho::config::TSConfig{.UseDefineForClassFields = guchho::config::MaybeBool::kFalse}},
		},
	});
}

TEST(BundlerDCE, TestInlineIdentityFunctionCalls) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/identity.js", R"test(

				function DROP(x) { return x }
				console.log(DROP(1))
				DROP(foo())
				DROP(1)
			)test"},
			{"/identity-last.js", R"test(

				function DROP(x) { return [x] }
				function DROP(x) { return x }
				console.log(DROP(1))
				DROP(foo())
				DROP(1)
			)test"},
			{"/identity-cross-module.js", R"test(

				import { DROP } from './identity-cross-module-def'
				console.log(DROP(1))
				DROP(foo())
				DROP(1)
			)test"},
			{"/identity-cross-module-def.js", R"test(

				export function DROP(x) { return x }
			)test"},
			{"/identity-no-args.js", R"test(

				function keep(x) { return x }
				console.log(keep())
				keep()
			)test"},
			{"/identity-two-args.js", R"test(

				function keep(x) { return x }
				console.log(keep(1, 2))
				keep(1, 2)
			)test"},
			{"/identity-first.js", R"test(

				function keep(x) { return x }
				function keep(x) { return [x] }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/identity-generator.js", R"test(

				function* keep(x) { return x }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/identity-async.js", R"test(

				async function keep(x) { return x }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign.js", R"test(

				function keep(x) { return x }
				keep = reassigned
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-inc.js", R"test(

				function keep(x) { return x }
				keep++
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-div.js", R"test(

				function keep(x) { return x }
				keep /= reassigned
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-array.js", R"test(

				function keep(x) { return x }
				[keep] = reassigned
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-object.js", R"test(

				function keep(x) { return x }
				({keep} = reassigned)
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/not-identity-two-args.js", R"test(

				function keep(x, y) { return x }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/not-identity-default.js", R"test(

				function keep(x = foo()) { return x }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/not-identity-array.js", R"test(

				function keep([x]) { return x }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/not-identity-object.js", R"test(

				function keep({x}) { return x }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/not-identity-rest.js", R"test(

				function keep(...x) { return x }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/not-identity-return.js", R"test(

				function keep(x) { return [x] }
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/identity-simplify-unused-issue-4287.js", R"test(

				function id(x) { return x }
				id({ x: id([123, foo()]) })
				id({ x: id(123) })
			)test"},
		},
		.entry_paths = {
			"/identity.js",
			"/identity-last.js",
			"/identity-first.js",
			"/identity-generator.js",
			"/identity-async.js",
			"/identity-cross-module.js",
			"/identity-no-args.js",
			"/identity-two-args.js",
			"/reassign.js",
			"/reassign-inc.js",
			"/reassign-div.js",
			"/reassign-array.js",
			"/reassign-object.js",
			"/not-identity-two-args.js",
			"/not-identity-default.js",
			"/not-identity-array.js",
			"/not-identity-object.js",
			"/not-identity-rest.js",
			"/not-identity-return.js",
			"/identity-simplify-unused-issue-4287.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestInlineEmptyFunctionCalls) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/empty.js", R"test(

				function DROP() {}
				console.log(DROP(foo(), bar()))
				console.log(DROP(foo(), 1))
				console.log(DROP(1, foo()))
				console.log(DROP(1))
				console.log(DROP())
				DROP(foo(), bar())
				DROP(foo(), 1)
				DROP(1, foo())
				DROP(1)
				DROP()
			)test"},
			{"/empty-comma.js", R"test(

				function DROP() {}
				console.log((DROP(), DROP(), foo()))
				console.log((DROP(), foo(), DROP()))
				console.log((foo(), DROP(), DROP()))
				for (DROP(); DROP(); DROP()) DROP();
				DROP(), DROP(), foo();
				DROP(), foo(), DROP();
				foo(), DROP(), DROP();
			)test"},
			{"/empty-if-else.js", R"test(

				function DROP() {}
				if (foo) { let bar = baz(); bar(); bar() } else DROP();
			)test"},
			{"/empty-last.js", R"test(

				function DROP() { return x }
				function DROP() { return }
				console.log(DROP())
				DROP()
			)test"},
			{"/empty-cross-module.js", R"test(

				import { DROP } from './empty-cross-module-def'
				console.log(DROP())
				DROP()
			)test"},
			{"/empty-cross-module-def.js", R"test(

				export function DROP() {}
			)test"},
			{"/empty-first.js", R"test(

				function keep() { return }
				function keep() { return x }
				console.log(keep())
				keep(foo())
				keep(1)
			)test"},
			{"/empty-generator.js", R"test(

				function* keep() {}
				console.log(keep())
				keep(foo())
				keep(1)
			)test"},
			{"/empty-async.js", R"test(

				async function keep() {}
				console.log(keep())
				keep(foo())
				keep(1)
			)test"},
			{"/reassign.js", R"test(

				function keep() {}
				keep = reassigned
				console.log(keep())
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-inc.js", R"test(

				function keep() {}
				keep++
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-div.js", R"test(

				function keep() {}
				keep /= reassigned
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-array.js", R"test(

				function keep() {}
				[keep] = reassigned
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
			{"/reassign-object.js", R"test(

				function keep() {}
				({keep} = reassigned)
				console.log(keep(1))
				keep(foo())
				keep(1)
			)test"},
		},
		.entry_paths = {
			"/empty.js",
			"/empty-comma.js",
			"/empty-if-else.js",
			"/empty-last.js",
			"/empty-cross-module.js",
			"/empty-first.js",
			"/empty-generator.js",
			"/empty-async.js",
			"/reassign.js",
			"/reassign-inc.js",
			"/reassign-div.js",
			"/reassign-array.js",
			"/reassign-object.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestInlineFunctionCallBehaviorChanges) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function empty() {}
				function id(x) { return x }

				export let shouldBeWrapped = [
					id(foo.bar)(),
					id(foo[bar])(),
					id(foo?.bar)(),
					id(foo?.[bar])(),

					(empty(), foo.bar)(),
					(empty(), foo[bar])(),
					(empty(), foo?.bar)(),
					(empty(), foo?.[bar])(),

					id(eval)(),
					id(eval)?.(),
					(empty(), eval)(),
					(empty(), eval)?.(),

					id(foo.bar)``,
					id(foo[bar])``,
					id(foo?.bar)``,
					id(foo?.[bar])``,

					(empty(), foo.bar)``,
					(empty(), foo[bar])``,
					(empty(), foo?.bar)``,
					(empty(), foo?.[bar])``,

					delete id(foo),
					delete id(foo.bar),
					delete id(foo[bar]),
					delete id(foo?.bar),
					delete id(foo?.[bar]),

					delete (empty(), foo),
					delete (empty(), foo.bar),
					delete (empty(), foo[bar]),
					delete (empty(), foo?.bar),
					delete (empty(), foo?.[bar]),

					delete empty(),
				]

				export let shouldNotBeWrapped = [
					id(foo)(),
					(empty(), foo)(),

					id(foo)``,
					(empty(), foo)``,
				]

				export let shouldNotBeDoubleWrapped = [
					delete (empty(), foo(), bar()),
					delete id((foo(), bar())),
				]
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

TEST(BundlerDCE, TestInlineFunctionCallForInitDecl) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function empty() {}
				function id(x) { return x }

				for (var y = empty(); false; ) ;
				for (var z = id(123); false; ) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestConstValueInliningNoBundle) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/top-level.js", R"test(

				// These should be kept because they are top-level and tree shaking is not enabled
				const n_keep = null
				const u_keep = undefined
				const i_keep = 1234567
				const f_keep = 123.456
				const s_keep = 'abc'

				// Values should still be inlined
				console.log(
					// These are doubled to avoid the "inline const/let into next statement if used once" optimization
					n_keep, n_keep,
					u_keep, u_keep,
					i_keep, i_keep,
					f_keep, f_keep,
					s_keep, s_keep,
				)
			)test"},
			{"/nested-block.js", R"test(

				{
					const REMOVE_n = null
					const REMOVE_u = undefined
					const REMOVE_i = 1234567
					const REMOVE_f = 123.456
					const REMOVE_s = 'abc' // String inlining is intentionally not supported right now
					const s_keep = 'Long strings are not inlined as constants'
					console.log(
						// These are doubled to avoid the "inline const/let into next statement if used once" optimization
						REMOVE_n, REMOVE_n,
						REMOVE_u, REMOVE_u,
						REMOVE_i, REMOVE_i,
						REMOVE_f, REMOVE_f,
						REMOVE_s, REMOVE_s,
						s_keep, s_keep,
					)
				}
			)test"},
			{"/nested-function.js", R"test(

				function nested() {
					const REMOVE_n = null
					const REMOVE_u = undefined
					const REMOVE_i = 1234567
					const REMOVE_f = 123.456
					const REMOVE_s = 'abc' // String inlining is intentionally not supported right now
					const s_keep = 'Long strings are not inlined as constants'
					console.log(
						// These are doubled to avoid the "inline const/let into next statement if used once" optimization
						REMOVE_n, REMOVE_n,
						REMOVE_u, REMOVE_u,
						REMOVE_i, REMOVE_i,
						REMOVE_f, REMOVE_f,
						REMOVE_s, REMOVE_s,
						s_keep, s_keep,
					)
				}
			)test"},
			{"/namespace-export.ts", R"test(

				namespace ns {
					const x_REMOVE = 1
					export const y_keep = 2
					console.log(
						x_REMOVE, x_REMOVE,
						y_keep, y_keep,
					)
				}
			)test"},
			{"/comment-before.js", R"test(

				{
					//! comment
					const REMOVE = 1
					x = [REMOVE, REMOVE]
				}
			)test"},
			{"/directive-before.js", R"test(

				function nested() {
					'directive'
					const REMOVE = 1
					x = [REMOVE, REMOVE]
				}
			)test"},
			{"/semicolon-before.js", R"test(

				{
					;
					const REMOVE = 1
					x = [REMOVE, REMOVE]
				}
			)test"},
			{"/debugger-before.js", R"test(

				{
					debugger
					const REMOVE = 1
					x = [REMOVE, REMOVE]
				}
			)test"},
			{"/type-before.ts", R"test(

				{
					declare let x
					const REMOVE = 1
					x = [REMOVE, REMOVE]
				}
			)test"},
			{"/exprs-before.js", R"test(

				function nested() {
					const x = [, '', {}, 0n, /./, function() {}, () => {}]
					const y_REMOVE = 1
					function foo() {
						return y_REMOVE
					}
				}
			)test"},
			{"/disabled-tdz.js", R"test(

				foo()
				const x_keep = 1
				function foo() {
					return x_keep
				}
			)test"},
			{"/backwards-reference-top-level.js", R"test(

				const x = y
				const y = 1
				console.log(
					x, x,
					y, y,
				)
			)test"},
			{"/backwards-reference-nested-function.js", R"test(

				function foo() {
					const x = y
					const y = 1
					console.log(
						x, x,
						y, y,
					)
				}
			)test"},
			{"/issue-3125.js", R"test(

				function foo() {
					const f = () => x
					const x = 0
					return f()
				}
			)test"},
		},
		.entry_paths = {
			"/top-level.js",
			"/nested-block.js",
			"/nested-function.js",
			"/namespace-export.ts",
			"/comment-before.js",
			"/directive-before.js",
			"/semicolon-before.js",
			"/debugger-before.js",
			"/type-before.ts",
			"/exprs-before.js",
			"/disabled-tdz.js",
			"/backwards-reference-top-level.js",
			"/backwards-reference-nested-function.js",
			"/issue-3125.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestConstValueInliningBundle) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/exported-entry.js", R"test(

				const x_REMOVE = 1
				export const y_keep = 2
				console.log(
					x_REMOVE,
					y_keep,
				)
			)test"},
			{"/re-exported-entry.js", R"test(

				import { x_REMOVE, y_keep } from './re-exported-constants'
				console.log(x_REMOVE, y_keep)
				export { y_keep }
			)test"},
			{"/re-exported-constants.js", R"test(

				export const x_REMOVE = 1
				export const y_keep = 2
			)test"},
			{"/re-exported-2-entry.js", R"test(

				export { y_keep } from './re-exported-2-constants'
			)test"},
			{"/re-exported-2-constants.js", R"test(

				export const x_REMOVE = 1
				export const y_keep = 2
			)test"},
			{"/re-exported-star-entry.js", R"test(

				export * from './re-exported-star-constants'
			)test"},
			{"/re-exported-star-constants.js", R"test(

				export const x_keep = 1
				export const y_keep = 2
			)test"},
			{"/cross-module-entry.js", R"test(

				import { x_REMOVE, y_keep } from './cross-module-constants'
				console.log(x_REMOVE, y_keep)
			)test"},
			{"/cross-module-constants.js", R"test(

				export const x_REMOVE = 1
				foo()
				export const y_keep = 1
				export function foo() {
					return [x_REMOVE, y_keep]
				}
			)test"},
			{"/print-shorthand-entry.js", R"test(

				import { foo, _bar } from './print-shorthand-constants'
				// The inlined constants must still be present in the output! We don't
				// want the printer to use the shorthand syntax here to refer to the
				// name of the constant itself because the constant declaration is omitted.
				console.log({ foo, _bar })
			)test"},
			{"/print-shorthand-constants.js", R"test(

				export const foo = 123
				export const _bar = -321
			)test"},
			{"/circular-import-entry.js", R"test(

				import './circular-import-constants'
			)test"},
			{"/circular-import-constants.js", R"test(

				export const foo = 123 // Inlining should be prevented by the cycle
				export function bar() {
					return foo
				}
				import './circular-import-cycle'
			)test"},
			{"/circular-import-cycle.js", R"test(

				import { bar } from './circular-import-constants'
				console.log(bar()) // This accesses "foo" before it's initialized
			)test"},
			{"/circular-re-export-entry.js", R"test(

				import { baz } from './circular-re-export-constants'
				console.log(baz)
			)test"},
			{"/circular-re-export-constants.js", R"test(

				export const foo = 123 // Inlining should be prevented by the cycle
				export function bar() {
					return foo
				}
				export { baz } from './circular-re-export-cycle'
			)test"},
			{"/circular-re-export-cycle.js", R"test(

				export const baz = 0
				import { bar } from './circular-re-export-constants'
				console.log(bar()) // This accesses "foo" before it's initialized
			)test"},
			{"/circular-re-export-star-entry.js", R"test(

				import './circular-re-export-star-constants'
			)test"},
			{"/circular-re-export-star-constants.js", R"test(

				export const foo = 123 // Inlining should be prevented by the cycle
				export function bar() {
					return foo
				}
				export * from './circular-re-export-star-cycle'
			)test"},
			{"/circular-re-export-star-cycle.js", R"test(

				import { bar } from './circular-re-export-star-constants'
				console.log(bar()) // This accesses "foo" before it's initialized
			)test"},
			{"/non-circular-export-entry.js", R"test(

				import { foo, bar } from './non-circular-export-constants'
				console.log(foo, bar())
			)test"},
			{"/non-circular-export-constants.js", R"test(

				const foo = 123 // Inlining should be prevented by the cycle
				function bar() {
					return foo
				}
				export { foo, bar }
			)test"},
		},
		.entry_paths = {
			"/exported-entry.js",
			"/re-exported-entry.js",
			"/re-exported-2-entry.js",
			"/re-exported-star-entry.js",
			"/cross-module-entry.js",
			"/print-shorthand-entry.js",
			"/circular-import-entry.js",
			"/circular-re-export-entry.js",
			"/circular-re-export-star-entry.js",
			"/non-circular-export-entry.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestConstValueInliningAssign) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/const-assign.js", R"test(

				const x = 1
				x = 2
			)test"},
			{"/const-update.js", R"test(

				const x = 1
				x += 2
			)test"},
		},
		.entry_paths = {
			"/const-assign.js",
			"/const-update.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
		.expected_scan_log = R"scan(
const-assign.js: ERROR: Cannot assign to "x" because it is a constant
const-assign.js: NOTE: The symbol "x" was declared a constant here:
const-update.js: ERROR: Cannot assign to "x" because it is a constant
const-update.js: NOTE: The symbol "x" was declared a constant here:
)scan",
	});
}

TEST(BundlerDCE, TestConstValueInliningDirectEval) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/top-level-no-eval.js", R"test(

				const x = 1
				console.log(x, evil('x'))
			)test"},
			{"/top-level-eval.js", R"test(

				const x = 1
				console.log(x, eval('x'))
			)test"},
			{"/nested-no-eval.js", R"test(

				(() => {
					const x = 1
					console.log(x, evil('x'))
				})()
			)test"},
			{"/nested-eval.js", R"test(

				(() => {
					const x = 1
					console.log(x, eval('x'))
				})()
			)test"},
			{"/ts-namespace-no-eval.ts", R"test(

				namespace y {
					export const x = 1
					console.log(x, evil('x'))
				}
			)test"},
			{"/ts-namespace-eval.ts", R"test(

				namespace z {
					export const x = 1
					console.log(x, eval('x'))
				}
			)test"},
			{"/issue-4055.ts", R"test(

				const variable = false
				;(function () {
					eval("var variable = true")
					console.log(variable)
				})()
			)test"},
		},
		.entry_paths = {
			"/top-level-no-eval.js",
			"/top-level-eval.js",
			"/nested-no-eval.js",
			"/nested-eval.js",
			"/ts-namespace-no-eval.ts",
			"/ts-namespace-eval.ts",
			"/issue-4055.ts",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestCrossModuleConstantFoldingNumber) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/enum-constants.ts", R"test(

				export enum x {
					a = 3,
					b = 6,
				}
			)test"},
			{"/enum-entry.ts", R"test(

				import { x } from './enum-constants'
				console.log([
					+x.b,
					-x.b,
					~x.b,
					!x.b,
					typeof x.b,
				], [
					x.a + x.b,
					x.a - x.b,
					x.a * x.b,
					x.a / x.b,
					x.a % x.b,
					x.a ** x.b,
				], [
					x.a < x.b,
					x.a > x.b,
					x.a <= x.b,
					x.a >= x.b,
					x.a == x.b,
					x.a != x.b,
					x.a === x.b,
					x.a !== x.b,
				], [
					x.b << 1,
					x.b >> 1,
					x.b >>> 1,
				], [
					x.a & x.b,
					x.a | x.b,
					x.a ^ x.b,
				], [
					x.a && x.b,
					x.a || x.b,
					x.a ?? x.b,
					x.a ? 'y' : 'n',
					!x.b ? 'y' : 'n',
				])
			)test"},
			{"/const-constants.js", R"test(

				export const a = 3
				export const b = 6
			)test"},
			{"/const-entry.js", R"test(

				import { a, b } from './const-constants'
				console.log([
					+b,
					-b,
					~b,
					!b,
					typeof b,
				], [
					a + b,
					a - b,
					a * b,
					a / b,
					a % b,
					a ** b,
				], [
					a < b,
					a > b,
					a <= b,
					a >= b,
					a == b,
					a != b,
					a === b,
					a !== b,
				], [
					b << 1,
					b >> 1,
					b >>> 1,
				], [
					a & b,
					a | b,
					a ^ b,
				], [
					a && b,
					a || b,
					a ?? b,
					a ? 'y' : 'n',
					!b ? 'y' : 'n',
				])
			)test"},
			{"/nested-constants.ts", R"test(

				export const a = 2
				export const b = 4
				export const c = 8
				export enum x {
					a = 16,
					b = 32,
					c = 64,
				}
			)test"},
			{"/nested-entry.ts", R"test(

				import { a, b, c, x } from './nested-constants'
				console.log({
					'should be 4': ~(~a & ~b) & (b | c),
					'should be 32': ~(~x.a & ~x.b) & (x.b | x.c),
				})
			)test"},
		},
		.entry_paths = {
			"/enum-entry.ts",
			"/const-entry.js",
			"/nested-entry.ts",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestCrossModuleConstantFoldingString) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/enum-constants.ts", R"test(

				export enum x {
					a = 'foo',
					b = 'bar',
				}
			)test"},
			{"/enum-entry.ts", R"test(

				import { x } from './enum-constants'
				console.log([
					typeof x.b,
				], [
					x.a + x.b,
				], [
					x.a < x.b,
					x.a > x.b,
					x.a <= x.b,
					x.a >= x.b,
					x.a == x.b,
					x.a != x.b,
					x.a === x.b,
					x.a !== x.b,
				], [
					x.a && x.b,
					x.a || x.b,
					x.a ?? x.b,
					x.a ? 'y' : 'n',
					!x.b ? 'y' : 'n',
				])
			)test"},
			{"/const-constants.js", R"test(

				export const a = 'foo'
				export const b = 'bar'
			)test"},
			{"/const-entry.js", R"test(

				import { a, b } from './const-constants'
				console.log([
					typeof b,
				], [
					a + b,
				], [
					a < b,
					a > b,
					a <= b,
					a >= b,
					a == b,
					a != b,
					a === b,
					a !== b,
				], [
					a && b,
					a || b,
					a ?? b,
					a ? 'y' : 'n',
					!b ? 'y' : 'n',
				])
			)test"},
			{"/nested-constants.ts", R"test(

				export const a = 'foo'
				export const b = 'bar'
				export const c = 'baz'
				export enum x {
					a = 'FOO',
					b = 'BAR',
					c = 'BAZ',
				}
			)test"},
			{"/nested-entry.ts", R"test(

				import { a, b, c, x } from './nested-constants'
				console.log({
					'should be foobarbaz': a + b + c,
					'should be FOOBARBAZ': x.a + x.b + x.c,
				})
			)test"},
		},
		.entry_paths = {
			"/enum-entry.ts",
			"/const-entry.js",
			"/nested-entry.ts",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestCrossModuleConstantFoldingComputedPropertyName) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/enum-constants.ts", R"test(

				export enum x {
					a = 123,
					b = 'abc',
					proto = '__proto__',
					ptype = 'prototype',
					ctor = 'constructor',
				}
			)test"},
			{"/enum-entry.ts", R"test(

				import { x } from './enum-constants'
				console.log({
					[x.a]: x.a,
					[x.b]: x.b,
				})
				class Foo {
					[x.proto] = {};
					[x.ptype] = {};
					[x.ctor]() {};
				}
			)test"},
			{"/const-constants.js", R"test(

				export const a = 456
				export const b = 'xyz'
				export const proto = '__proto__'
				export const ptype = 'prototype'
				export const ctor = 'constructor'
			)test"},
			{"/const-entry.js", R"test(

				import { a, b, proto, ptype, ctor } from './const-constants'
				console.log({
					[a]: a,
					[b]: b,
				})
				class Foo {
					[proto] = {};
					[ptype] = {};
					[ctor]() {};
				}
			)test"},
		},
		.entry_paths = {
			"/enum-entry.ts",
			"/const-entry.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestMultipleDeclarationTreeShaking) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/var2.js", R"test(

				var x = 1
				console.log(x)
				var x = 2
			)test"},
			{"/var3.js", R"test(

				var x = 1
				console.log(x)
				var x = 2
				console.log(x)
				var x = 3
			)test"},
			{"/function2.js", R"test(

				function x() { return 1 }
				console.log(x())
				function x() { return 2 }
			)test"},
			{"/function3.js", R"test(

				function x() { return 1 }
				console.log(x())
				function x() { return 2 }
				console.log(x())
				function x() { return 3 }
			)test"},
		},
		.entry_paths = {
			"/var2.js",
			"/var3.js",
			"/function2.js",
			"/function3.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = false,
		},
	});
}

TEST(BundlerDCE, TestMultipleDeclarationTreeShakingMinifySyntax) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/var2.js", R"test(

				var x = 1
				console.log(x)
				var x = 2
			)test"},
			{"/var3.js", R"test(

				var x = 1
				console.log(x)
				var x = 2
				console.log(x)
				var x = 3
			)test"},
			{"/function2.js", R"test(

				function x() { return 1 }
				console.log(x())
				function x() { return 2 }
			)test"},
			{"/function3.js", R"test(

				function x() { return 1 }
				console.log(x())
				function x() { return 2 }
				console.log(x())
				function x() { return 3 }
			)test"},
		},
		.entry_paths = {
			"/var2.js",
			"/var3.js",
			"/function2.js",
			"/function3.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestPureCallsWithSpread) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				/* @__PURE__ */ foo(...args);
				/* @__PURE__ */ new foo(...args);
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

TEST(BundlerDCE, TestTopLevelFunctionInliningWithSpread) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function empty1() {}
				function empty2() {}
				function empty3() {}

				function identity1(x) { return x }
				function identity2(x) { return x }
				function identity3(x) { return x }

				empty1()
				empty2(args)
				empty3(...args)

				identity1()
				identity2(args)
				identity3(...args)
			)test"},
			{"/inner.js", R"test(

				export function empty1() {}
				export function empty2() {}
				export function empty3() {}

				export function identity1(x) { return x }
				export function identity2(x) { return x }
				export function identity3(x) { return x }
			)test"},
			{"/entry-outer.js", R"test(

				import {
					empty1,
					empty2,
					empty3,

					identity1,
					identity2,
					identity3,
				} from './inner.js'

				empty1()
				empty2(args)
				empty3(...args)

				identity1()
				identity2(args)
				identity3(...args)
			)test"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry-outer.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestNestedFunctionInliningWithSpread) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function empty1() {}
				function empty2() {}
				function empty3() {}

				function identity1(x) { return x }
				function identity2(x) { return x }
				function identity3(x) { return x }

				check(
					empty1(),
					empty2(args),
					empty3(...args),

					identity1(),
					identity2(args),
					identity3(...args),
				)
			)test"},
			{"/inner.js", R"test(

				export function empty1() {}
				export function empty2() {}
				export function empty3() {}

				export function identity1(x) { return x }
				export function identity2(x) { return x }
				export function identity3(x) { return x }
			)test"},
			{"/entry-outer.js", R"test(

				import {
					empty1,
					empty2,
					empty3,

					identity1,
					identity2,
					identity3,
				} from './inner.js'

				check(
					empty1(),
					empty2(args),
					empty3(...args),

					identity1(),
					identity2(args),
					identity3(...args),
				)
			)test"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry-outer.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestPackageJsonSideEffectsFalseCrossPlatformSlash) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import "demo-pkg/foo"
				import "demo-pkg/bar"
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/foo.js", R"test(

				console.log('foo')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/bar/index.js", R"test(

				console.log('bar')
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"sideEffects": [
						"**/foo.js",
						"bar/index.js"
					]
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingJSWithAssociatedCSS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/test.jsx", R"test(

				import { Button } from 'pkg/button'
				import { Menu } from 'pkg/menu'
				render(<Button/>)
			)test"},
			{"/project/node_modules/pkg/button.js", R"test(

				import './button.css'
				export let Button
			)test"},
			{"/project/node_modules/pkg/button.css", R"test(

				button { color: red }
			)test"},
			{"/project/node_modules/pkg/menu.js", R"test(

				import './menu.css'
				export let Menu
			)test"},
			{"/project/node_modules/pkg/menu.css", R"test(

				menu { color: red }
			)test"},
		},
		.entry_paths = {"/project/test.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingJSWithAssociatedCSSReExportSideEffectsFalse) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/test.jsx", R"test(

				import { Button } from 'pkg'
				render(<Button/>)
			)test"},
			{"/project/node_modules/pkg/entry.js", R"test(

				export { Button } from './components'
			)test"},
			{"/project/node_modules/pkg/package.json", R"test(
{
				"main": "./entry.js",
				"sideEffects": false
			})test"},
			{"/project/node_modules/pkg/components.jsx", R"test(

				require('./button.css')
				export const Button = () => <button/>
			)test"},
			{"/project/node_modules/pkg/button.css", R"test(

				button { color: red }
			)test"},
		},
		.entry_paths = {"/project/test.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingJSWithAssociatedCSSReExportSideEffectsFalseOnlyJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/test.jsx", R"test(

				import { Button } from 'pkg'
				render(<Button/>)
			)test"},
			{"/project/node_modules/pkg/entry.js", R"test(

				export { Button } from './components'
			)test"},
			{"/project/node_modules/pkg/package.json", R"test(
{
				"main": "./entry.js",
				"sideEffects": ["*.css"]
			})test"},
			{"/project/node_modules/pkg/components.jsx", R"test(

				require('./button.css')
				export const Button = () => <button/>
			)test"},
			{"/project/node_modules/pkg/button.css", R"test(

				button { color: red }
			)test"},
		},
		.entry_paths = {"/project/test.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingJSWithAssociatedCSSExportStarSideEffectsFalse) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/test.jsx", R"test(

				import { Button } from 'pkg'
				render(<Button/>)
			)test"},
			{"/project/node_modules/pkg/entry.js", R"test(

				export * from './components'
			)test"},
			{"/project/node_modules/pkg/package.json", R"test(
{
				"main": "./entry.js",
				"sideEffects": false
			})test"},
			{"/project/node_modules/pkg/components.jsx", R"test(

				require('./button.css')
				export const Button = () => <button/>
			)test"},
			{"/project/node_modules/pkg/button.css", R"test(

				button { color: red }
			)test"},
		},
		.entry_paths = {"/project/test.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingJSWithAssociatedCSSExportStarSideEffectsFalseOnlyJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/test.jsx", R"test(

				import { Button } from 'pkg'
				render(<Button/>)
			)test"},
			{"/project/node_modules/pkg/entry.js", R"test(

				export * from './components'
			)test"},
			{"/project/node_modules/pkg/package.json", R"test(
{
				"main": "./entry.js",
				"sideEffects": ["*.css"]
			})test"},
			{"/project/node_modules/pkg/components.jsx", R"test(

				require('./button.css')
				export const Button = () => <button/>
			)test"},
			{"/project/node_modules/pkg/button.css", R"test(

				button { color: red }
			)test"},
		},
		.entry_paths = {"/project/test.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingJSWithAssociatedCSSUnusedNestedImportSideEffectsFalse) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/test.jsx", R"test(

				import { Button } from 'pkg/button'
				render(<Button/>)
			)test"},
			{"/project/node_modules/pkg/package.json", R"test(
{
				"sideEffects": false
			})test"},
			{"/project/node_modules/pkg/button.jsx", R"test(

				import styles from './styles'
				export const Button = () => <button/>
			)test"},
			{"/project/node_modules/pkg/styles.js", R"test(

				import './styles.css'
				export default {}
			)test"},
			{"/project/node_modules/pkg/styles.css", R"test(

				button { color: red }
			)test"},
		},
		.entry_paths = {"/project/test.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestTreeShakingJSWithAssociatedCSSUnusedNestedImportSideEffectsFalseOnlyJS) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/test.jsx", R"test(

				import { Button } from 'pkg/button'
				render(<Button/>)
			)test"},
			{"/project/node_modules/pkg/package.json", R"test(
{
				"sideEffects": ["*.css"]
			})test"},
			{"/project/node_modules/pkg/button.jsx", R"test(

				import styles from './styles'
				export const Button = () => <button/>
			)test"},
			{"/project/node_modules/pkg/styles.js", R"test(

				import './styles.css'
				export default {}
			)test"},
			{"/project/node_modules/pkg/styles.css", R"test(

				button { color: red }
			)test"},
		},
		.entry_paths = {"/project/test.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestPreserveDirectivesMinifyPassThrough) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				//! 1
				'use 1'
				//! 2
				'use 2'
				//! 3
				'use 3'
				entry()
				//! 4
				'use 4'
				//! 5
				'use 5'
				//! 6
				'use 6'
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

TEST(BundlerDCE, TestPreserveDirectivesMinifyIIFE) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				//! 1
				'use 1'
				//! 2
				'use 2'
				//! 3
				'use 3'
				entry()
				//! 4
				'use 4'
				//! 5
				'use 5'
				//! 6
				'use 6'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestPreserveDirectivesMinifyBundle) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				//! 1
				'use 1'
				//! 2
				'use 2'
				//! 3
				'use 3'
				entry()
				//! 4
				'use 4'
				//! 5
				'use 5'
				//! 6
				'use 6'
				import "./nested.js"
			)test"},
			{"/nested.js", R"test(

				//! A
				'use A'
				//! B
				'use B'
				//! C
				'use C'
				nested()
				//! D
				'use D'
				//! E
				'use E'
				//! F
				'use F'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestNoSideEffectsComment) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/expr-fn.js", R"test(

				//! These should all have "no side effects"
				x([
					/* #__NO_SIDE_EFFECTS__ */ function() {},
					/* #__NO_SIDE_EFFECTS__ */ function y() {},
					/* #__NO_SIDE_EFFECTS__ */ function*() {},
					/* #__NO_SIDE_EFFECTS__ */ function* y() {},
					/* #__NO_SIDE_EFFECTS__ */ async function() {},
					/* #__NO_SIDE_EFFECTS__ */ async function y() {},
					/* #__NO_SIDE_EFFECTS__ */ async function*() {},
					/* #__NO_SIDE_EFFECTS__ */ async function* y() {},
				])
			)test"},
			{"/expr-arrow.js", R"test(

				//! These should all have "no side effects"
				x([
					/* #__NO_SIDE_EFFECTS__ */ y => y,
					/* #__NO_SIDE_EFFECTS__ */ () => {},
					/* #__NO_SIDE_EFFECTS__ */ (y) => (y),
					/* #__NO_SIDE_EFFECTS__ */ async y => y,
					/* #__NO_SIDE_EFFECTS__ */ async () => {},
					/* #__NO_SIDE_EFFECTS__ */ async (y) => (y),
				])
			)test"},
			{"/stmt-fn.js", R"test(

				//! These should all have "no side effects"
				// #__NO_SIDE_EFFECTS__
				function a() {}
				// #__NO_SIDE_EFFECTS__
				function* b() {}
				// #__NO_SIDE_EFFECTS__
				async function c() {}
				// #__NO_SIDE_EFFECTS__
				async function* d() {}
			)test"},
			{"/stmt-export-fn.js", R"test(

				//! These should all have "no side effects"
				/* @__NO_SIDE_EFFECTS__ */ export function a() {}
				/* @__NO_SIDE_EFFECTS__ */ export function* b() {}
				/* @__NO_SIDE_EFFECTS__ */ export async function c() {}
				/* @__NO_SIDE_EFFECTS__ */ export async function* d() {}
			)test"},
			{"/stmt-local.js", R"test(

				//! Only "c0" and "c2" should have "no side effects" (Rollup only respects "const" and only for the first one)
				/* #__NO_SIDE_EFFECTS__ */ var v0 = function() {}, v1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ let l0 = function() {}, l1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ const c0 = function() {}, c1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ var v2 = () => {}, v3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ let l2 = () => {}, l3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ const c2 = () => {}, c3 = () => {}
			)test"},
			{"/stmt-export-local.js", R"test(

				//! Only "c0" and "c2" should have "no side effects" (Rollup only respects "const" and only for the first one)
				/* #__NO_SIDE_EFFECTS__ */ export var v0 = function() {}, v1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export let l0 = function() {}, l1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export const c0 = function() {}, c1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export var v2 = () => {}, v3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ export let l2 = () => {}, l3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ export const c2 = () => {}, c3 = () => {}
			)test"},
			{"/ns-export-fn.ts", R"test(

				namespace ns {
					//! These should all have "no side effects"
					/* @__NO_SIDE_EFFECTS__ */ export function a() {}
					/* @__NO_SIDE_EFFECTS__ */ export function* b() {}
					/* @__NO_SIDE_EFFECTS__ */ export async function c() {}
					/* @__NO_SIDE_EFFECTS__ */ export async function* d() {}
				}
			)test"},
			{"/ns-export-local.ts", R"test(

				namespace ns {
					//! Only "c0" and "c2" should have "no side effects" (Rollup only respects "const" and only for the first one)
					/* #__NO_SIDE_EFFECTS__ */ export var v0 = function() {}, v1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export let l0 = function() {}, l1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export const c0 = function() {}, c1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export var v2 = () => {}, v3 = () => {}
					/* #__NO_SIDE_EFFECTS__ */ export let l2 = () => {}, l3 = () => {}
					/* #__NO_SIDE_EFFECTS__ */ export const c2 = () => {}, c3 = () => {}
				}
			)test"},
			{"/stmt-export-default-before-fn-anon.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default function() {})test"},
			{"/stmt-export-default-before-fn-name.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default function f() {})test"},
			{"/stmt-export-default-before-gen-fn-anon.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default function*() {})test"},
			{"/stmt-export-default-before-gen-fn-name.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default function* f() {})test"},
			{"/stmt-export-default-before-async-fn-anon.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default async function() {})test"},
			{"/stmt-export-default-before-async-fn-name.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default async function f() {})test"},
			{"/stmt-export-default-before-async-gen-fn-anon.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default async function*() {})test"},
			{"/stmt-export-default-before-async-gen-fn-name.js", R"test(/*! This should have "no side effects" */ /* #__NO_SIDE_EFFECTS__ */ export default async function* f() {})test"},
			{"/stmt-export-default-after-fn-anon.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ function() {})test"},
			{"/stmt-export-default-after-fn-name.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ function f() {})test"},
			{"/stmt-export-default-after-gen-fn-anon.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ function*() {})test"},
			{"/stmt-export-default-after-gen-fn-name.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ function* f() {})test"},
			{"/stmt-export-default-after-async-fn-anon.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ async function() {})test"},
			{"/stmt-export-default-after-async-fn-name.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ async function f() {})test"},
			{"/stmt-export-default-after-async-gen-fn-anon.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ async function*() {})test"},
			{"/stmt-export-default-after-async-gen-fn-name.js", R"test(/*! This should have "no side effects" */ export default /* @__NO_SIDE_EFFECTS__ */ async function* f() {})test"},
		},
		.entry_paths = {
			"/expr-fn.js",
			"/expr-arrow.js",
			"/stmt-fn.js",
			"/stmt-export-fn.js",
			"/stmt-local.js",
			"/stmt-export-local.js",
			"/ns-export-fn.ts",
			"/ns-export-local.ts",
			"/stmt-export-default-before-fn-anon.js",
			"/stmt-export-default-before-fn-name.js",
			"/stmt-export-default-before-gen-fn-anon.js",
			"/stmt-export-default-before-gen-fn-name.js",
			"/stmt-export-default-before-async-fn-anon.js",
			"/stmt-export-default-before-async-fn-name.js",
			"/stmt-export-default-before-async-gen-fn-anon.js",
			"/stmt-export-default-before-async-gen-fn-name.js",
			"/stmt-export-default-after-fn-anon.js",
			"/stmt-export-default-after-fn-name.js",
			"/stmt-export-default-after-gen-fn-anon.js",
			"/stmt-export-default-after-gen-fn-name.js",
			"/stmt-export-default-after-async-fn-anon.js",
			"/stmt-export-default-after-async-fn-name.js",
			"/stmt-export-default-after-async-gen-fn-anon.js",
			"/stmt-export-default-after-async-gen-fn-name.js",
		},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestNoSideEffectsCommentIgnoreAnnotations) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/expr-fn.js", R"test(

				x([
					/* #__NO_SIDE_EFFECTS__ */ function() {},
					/* #__NO_SIDE_EFFECTS__ */ function y() {},
					/* #__NO_SIDE_EFFECTS__ */ function*() {},
					/* #__NO_SIDE_EFFECTS__ */ function* y() {},
					/* #__NO_SIDE_EFFECTS__ */ async function() {},
					/* #__NO_SIDE_EFFECTS__ */ async function y() {},
					/* #__NO_SIDE_EFFECTS__ */ async function*() {},
					/* #__NO_SIDE_EFFECTS__ */ async function* y() {},
				])
			)test"},
			{"/expr-arrow.js", R"test(

				x([
					/* #__NO_SIDE_EFFECTS__ */ y => y,
					/* #__NO_SIDE_EFFECTS__ */ () => {},
					/* #__NO_SIDE_EFFECTS__ */ (y) => (y),
					/* #__NO_SIDE_EFFECTS__ */ async y => y,
					/* #__NO_SIDE_EFFECTS__ */ async () => {},
					/* #__NO_SIDE_EFFECTS__ */ async (y) => (y),
				])
			)test"},
			{"/stmt-fn.js", R"test(

				// #__NO_SIDE_EFFECTS__
				function a() {}
				// #__NO_SIDE_EFFECTS__
				function* b() {}
				// #__NO_SIDE_EFFECTS__
				async function c() {}
				// #__NO_SIDE_EFFECTS__
				async function* d() {}
			)test"},
			{"/stmt-export-fn.js", R"test(

				/* @__NO_SIDE_EFFECTS__ */ export function a() {}
				/* @__NO_SIDE_EFFECTS__ */ export function* b() {}
				/* @__NO_SIDE_EFFECTS__ */ export async function c() {}
				/* @__NO_SIDE_EFFECTS__ */ export async function* d() {}
			)test"},
			{"/stmt-local.js", R"test(

				/* #__NO_SIDE_EFFECTS__ */ var v0 = function() {}, v1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ let l0 = function() {}, l1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ const c0 = function() {}, c1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ var v2 = () => {}, v3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ let l2 = () => {}, l3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ const c2 = () => {}, c3 = () => {}
			)test"},
			{"/stmt-export-local.js", R"test(

				/* #__NO_SIDE_EFFECTS__ */ export var v0 = function() {}, v1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export let l0 = function() {}, l1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export const c0 = function() {}, c1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export var v2 = () => {}, v3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ export let l2 = () => {}, l3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ export const c2 = () => {}, c3 = () => {}
			)test"},
			{"/ns-export-fn.ts", R"test(

				namespace ns {
					/* @__NO_SIDE_EFFECTS__ */ export function a() {}
					/* @__NO_SIDE_EFFECTS__ */ export function* b() {}
					/* @__NO_SIDE_EFFECTS__ */ export async function c() {}
					/* @__NO_SIDE_EFFECTS__ */ export async function* d() {}
				}
			)test"},
			{"/ns-export-local.ts", R"test(

				namespace ns {
					/* #__NO_SIDE_EFFECTS__ */ export var v0 = function() {}, v1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export let l0 = function() {}, l1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export const c0 = function() {}, c1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export var v2 = () => {}, v3 = () => {}
					/* #__NO_SIDE_EFFECTS__ */ export let l2 = () => {}, l3 = () => {}
					/* #__NO_SIDE_EFFECTS__ */ export const c2 = () => {}, c3 = () => {}
				}
			)test"},
			{"/stmt-export-default-before-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default function() {}"},
			{"/stmt-export-default-before-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default function f() {}"},
			{"/stmt-export-default-before-gen-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default function*() {}"},
			{"/stmt-export-default-before-gen-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default function* f() {}"},
			{"/stmt-export-default-before-async-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function() {}"},
			{"/stmt-export-default-before-async-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function f() {}"},
			{"/stmt-export-default-before-async-gen-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function*() {}"},
			{"/stmt-export-default-before-async-gen-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function* f() {}"},
			{"/stmt-export-default-after-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ function() {}"},
			{"/stmt-export-default-after-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ function f() {}"},
			{"/stmt-export-default-after-gen-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ function*() {}"},
			{"/stmt-export-default-after-gen-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ function* f() {}"},
			{"/stmt-export-default-after-async-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function() {}"},
			{"/stmt-export-default-after-async-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function f() {}"},
			{"/stmt-export-default-after-async-gen-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function*() {}"},
			{"/stmt-export-default-after-async-gen-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function* f() {}"},
		},
		.entry_paths = {
			"/expr-fn.js",
			"/expr-arrow.js",
			"/stmt-fn.js",
			"/stmt-export-fn.js",
			"/stmt-local.js",
			"/stmt-export-local.js",
			"/ns-export-fn.ts",
			"/ns-export-local.ts",
			"/stmt-export-default-before-fn-anon.js",
			"/stmt-export-default-before-fn-name.js",
			"/stmt-export-default-before-gen-fn-anon.js",
			"/stmt-export-default-before-gen-fn-name.js",
			"/stmt-export-default-before-async-fn-anon.js",
			"/stmt-export-default-before-async-fn-name.js",
			"/stmt-export-default-before-async-gen-fn-anon.js",
			"/stmt-export-default-before-async-gen-fn-name.js",
			"/stmt-export-default-after-fn-anon.js",
			"/stmt-export-default-after-fn-name.js",
			"/stmt-export-default-after-gen-fn-anon.js",
			"/stmt-export-default-after-gen-fn-name.js",
			"/stmt-export-default-after-async-fn-anon.js",
			"/stmt-export-default-after-async-fn-name.js",
			"/stmt-export-default-after-async-gen-fn-anon.js",
			"/stmt-export-default-after-async-gen-fn-name.js",
		},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.IgnoreDCEAnnotations = true,
		},
	});
}

TEST(BundlerDCE, TestNoSideEffectsCommentMinifyWhitespace) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/expr-fn.js", R"test(

				x([
					/* #__NO_SIDE_EFFECTS__ */ function() {},
					/* #__NO_SIDE_EFFECTS__ */ function y() {},
					/* #__NO_SIDE_EFFECTS__ */ function*() {},
					/* #__NO_SIDE_EFFECTS__ */ function* y() {},
					/* #__NO_SIDE_EFFECTS__ */ async function() {},
					/* #__NO_SIDE_EFFECTS__ */ async function y() {},
					/* #__NO_SIDE_EFFECTS__ */ async function*() {},
					/* #__NO_SIDE_EFFECTS__ */ async function* y() {},
				])
			)test"},
			{"/expr-arrow.js", R"test(

				x([
					/* #__NO_SIDE_EFFECTS__ */ y => y,
					/* #__NO_SIDE_EFFECTS__ */ () => {},
					/* #__NO_SIDE_EFFECTS__ */ (y) => (y),
					/* #__NO_SIDE_EFFECTS__ */ async y => y,
					/* #__NO_SIDE_EFFECTS__ */ async () => {},
					/* #__NO_SIDE_EFFECTS__ */ async (y) => (y),
				])
			)test"},
			{"/stmt-fn.js", R"test(

				// #__NO_SIDE_EFFECTS__
				function a() {}
				// #__NO_SIDE_EFFECTS__
				function* b() {}
				// #__NO_SIDE_EFFECTS__
				async function c() {}
				// #__NO_SIDE_EFFECTS__
				async function* d() {}
			)test"},
			{"/stmt-export-fn.js", R"test(

				/* @__NO_SIDE_EFFECTS__ */ export function a() {}
				/* @__NO_SIDE_EFFECTS__ */ export function* b() {}
				/* @__NO_SIDE_EFFECTS__ */ export async function c() {}
				/* @__NO_SIDE_EFFECTS__ */ export async function* d() {}
			)test"},
			{"/stmt-local.js", R"test(

				/* #__NO_SIDE_EFFECTS__ */ var v0 = function() {}, v1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ let l0 = function() {}, l1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ const c0 = function() {}, c1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ var v2 = () => {}, v3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ let l2 = () => {}, l3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ const c2 = () => {}, c3 = () => {}
			)test"},
			{"/stmt-export-local.js", R"test(

				/* #__NO_SIDE_EFFECTS__ */ export var v0 = function() {}, v1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export let l0 = function() {}, l1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export const c0 = function() {}, c1 = function() {}
				/* #__NO_SIDE_EFFECTS__ */ export var v2 = () => {}, v3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ export let l2 = () => {}, l3 = () => {}
				/* #__NO_SIDE_EFFECTS__ */ export const c2 = () => {}, c3 = () => {}
			)test"},
			{"/ns-export-fn.ts", R"test(

				namespace ns {
					/* @__NO_SIDE_EFFECTS__ */ export function a() {}
					/* @__NO_SIDE_EFFECTS__ */ export function* b() {}
					/* @__NO_SIDE_EFFECTS__ */ export async function c() {}
					/* @__NO_SIDE_EFFECTS__ */ export async function* d() {}
				}
			)test"},
			{"/ns-export-local.ts", R"test(

				namespace ns {
					/* #__NO_SIDE_EFFECTS__ */ export var v0 = function() {}, v1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export let l0 = function() {}, l1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export const c0 = function() {}, c1 = function() {}
					/* #__NO_SIDE_EFFECTS__ */ export var v2 = () => {}, v3 = () => {}
					/* #__NO_SIDE_EFFECTS__ */ export let l2 = () => {}, l3 = () => {}
					/* #__NO_SIDE_EFFECTS__ */ export const c2 = () => {}, c3 = () => {}
				}
			)test"},
			{"/stmt-export-default-before-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default function() {}"},
			{"/stmt-export-default-before-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default function f() {}"},
			{"/stmt-export-default-before-gen-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default function*() {}"},
			{"/stmt-export-default-before-gen-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default function* f() {}"},
			{"/stmt-export-default-before-async-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function() {}"},
			{"/stmt-export-default-before-async-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function f() {}"},
			{"/stmt-export-default-before-async-gen-fn-anon.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function*() {}"},
			{"/stmt-export-default-before-async-gen-fn-name.js", "/* #__NO_SIDE_EFFECTS__ */ export default async function* f() {}"},
			{"/stmt-export-default-after-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ function() {}"},
			{"/stmt-export-default-after-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ function f() {}"},
			{"/stmt-export-default-after-gen-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ function*() {}"},
			{"/stmt-export-default-after-gen-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ function* f() {}"},
			{"/stmt-export-default-after-async-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function() {}"},
			{"/stmt-export-default-after-async-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function f() {}"},
			{"/stmt-export-default-after-async-gen-fn-anon.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function*() {}"},
			{"/stmt-export-default-after-async-gen-fn-name.js", "export default /* @__NO_SIDE_EFFECTS__ */ async function* f() {}"},
		},
		.entry_paths = {
			"/expr-fn.js",
			"/expr-arrow.js",
			"/stmt-fn.js",
			"/stmt-export-fn.js",
			"/stmt-local.js",
			"/stmt-export-local.js",
			"/ns-export-fn.ts",
			"/ns-export-local.ts",
			"/stmt-export-default-before-fn-anon.js",
			"/stmt-export-default-before-fn-name.js",
			"/stmt-export-default-before-gen-fn-anon.js",
			"/stmt-export-default-before-gen-fn-name.js",
			"/stmt-export-default-before-async-fn-anon.js",
			"/stmt-export-default-before-async-fn-name.js",
			"/stmt-export-default-before-async-gen-fn-anon.js",
			"/stmt-export-default-before-async-gen-fn-name.js",
			"/stmt-export-default-after-fn-anon.js",
			"/stmt-export-default-after-fn-name.js",
			"/stmt-export-default-after-gen-fn-anon.js",
			"/stmt-export-default-after-gen-fn-name.js",
			"/stmt-export-default-after-async-fn-anon.js",
			"/stmt-export-default-after-async-fn-name.js",
			"/stmt-export-default-after-async-gen-fn-anon.js",
			"/stmt-export-default-after-async-gen-fn-name.js",
		},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
		},
	});
}

TEST(BundlerDCE, TestNoSideEffectsCommentUnusedCalls) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/stmt-fn.js", R"test(

				/* @__NO_SIDE_EFFECTS__ */ function f(y) { sideEffect(y) }
				/* @__NO_SIDE_EFFECTS__ */ function* g(y) { sideEffect(y) }
				f('removeThisCall')
				g('removeThisCall')
				f(onlyKeepThisIdentifier)
				g(onlyKeepThisIdentifier)
				x(f('keepThisCall'))
				x(g('keepThisCall'))
			)test"},
			{"/stmt-local.js", R"test(

				/* @__NO_SIDE_EFFECTS__ */ const f = function (y) { sideEffect(y) }
				/* @__NO_SIDE_EFFECTS__ */ const g = function* (y) { sideEffect(y) }
				f('removeThisCall')
				g('removeThisCall')
				f(onlyKeepThisIdentifier)
				g(onlyKeepThisIdentifier)
				x(f('keepThisCall'))
				x(g('keepThisCall'))
			)test"},
			{"/expr-fn.js", R"test(

				const f = /* @__NO_SIDE_EFFECTS__ */ function (y) { sideEffect(y) }
				const g = /* @__NO_SIDE_EFFECTS__ */ function* (y) { sideEffect(y) }
				f('removeThisCall')
				g('removeThisCall')
				f(onlyKeepThisIdentifier)
				g(onlyKeepThisIdentifier)
				x(f('keepThisCall'))
				x(g('keepThisCall'))
			)test"},
			{"/stmt-export-default-fn.js", R"test(

				/* @__NO_SIDE_EFFECTS__ */ export default function f(y) { sideEffect(y) }
				f('removeThisCall')
				f(onlyKeepThisIdentifier)
				x(f('keepThisCall'))
			)test"},
		},
		.entry_paths = {
			"/stmt-fn.js",
			"/stmt-local.js",
			"/expr-fn.js",
			"/stmt-export-default-fn.js",
		},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			.TreeShaking = true,
		},
	});
}

TEST(BundlerDCE, TestNoSideEffectsCommentTypeScriptDeclare) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				// These should not cause us to crash
				/* @__NO_SIDE_EFFECTS__ */ declare function f1(y) { sideEffect(y) }
				/* @__NO_SIDE_EFFECTS__ */ declare const f2 = function (y) { sideEffect(y) }
				/* @__NO_SIDE_EFFECTS__ */ declare const f3 = (y) => { sideEffect(y) }
				declare const f4 = /* @__NO_SIDE_EFFECTS__ */ function (y) { sideEffect(y) }
				declare const f5 = /* @__NO_SIDE_EFFECTS__ */ (y) => { sideEffect(y) }
				namespace ns {
					/* @__NO_SIDE_EFFECTS__ */ export declare function f1(y) { sideEffect(y) }
					/* @__NO_SIDE_EFFECTS__ */ export declare const f2 = function (y) { sideEffect(y) }
					/* @__NO_SIDE_EFFECTS__ */ export declare const f3 = (y) => { sideEffect(y) }
					export declare const f4 = /* @__NO_SIDE_EFFECTS__ */ function (y) { sideEffect(y) }
					export declare const f5 = /* @__NO_SIDE_EFFECTS__ */ (y) => { sideEffect(y) }
				}
			)test"},
		},
		.entry_paths = {"/entry.ts"},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCEOfIIFE) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/remove-these.js", R"test(

				(() => {})();
				(() => {})(keepThisButRemoveTheIIFE);
				(() => { /* @__PURE__ */ removeMe() })();
				var someVar;
				(x => {})(someVar);
				var removeThis = /* @__PURE__ */ (() => stuff())();
				var removeThis2 = (() => 123)();
			)test"},
			{"/keep-these.js", R"test(

				undef = (() => {})();
				(() => { keepMe() })();
				((x = keepMe()) => {})();
				var someVar;
				(([y]) => {})(someVar);
				(({z}) => {})(someVar);
				var keepThis = /* @__PURE__ */ (() => stuff())();
				keepThis();
				((_ = keepMe()) => {})();
				var isPure = ((x, y) => 123)();
				use(isPure);
				var isNotPure = ((x = foo, y = bar) => 123)();
				use(isNotPure);
				(async () => ({ get then() { notPure() } }))();
				(async function() { return { get then() { notPure() } }; })();
			)test"},
		},
		.entry_paths = {
			"/remove-these.js",
			"/keep-these.js",
		},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			.TreeShaking = true,
		},
	});
}

TEST(BundlerDCE, TestDCEOfDestructuring) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Identifier bindings
				var remove1
				var remove2 = null
				var KEEP1 = x

				// Array patterns
				var [remove3] = []
				var [remove4, ...remove5] = [...[1, 2], 3]
				var [, , remove6] = [, , 3]
				var [KEEP2] = [x]
				var [KEEP3] = [...{}]

				// Object patterns (not handled right now)
				var { KEEP4 } = {}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCEOfDecorators) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/keep-these.js", R"test(

				import { fn } from './decorator'
				@fn class Class {}
				class Field { @fn field }
				class Method { @fn method() {} }
				class Accessor { @fn accessor accessor }
				class StaticField { @fn static field }
				class StaticMethod { @fn static method() {} }
				class StaticAccessor { @fn static accessor accessor }
			)test"},
			{"/decorator.js", R"test(

				export const fn = () => {
					console.log('side effect')
				}
			)test"},
		},
		.entry_paths = {"/keep-these.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCEOfExperimentalDecorators) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/keep-these.ts", R"test(

				import { fn } from './decorator'
				@fn class Class {}
				class Field { @fn field }
				class Method { @fn method() {} }
				class Accessor { @fn accessor accessor }
				class Parameter { foo(@fn bar) {} }
				class StaticField { @fn static field }
				class StaticMethod { @fn static method() {} }
				class StaticAccessor { @fn static accessor accessor }
				class StaticParameter { static foo(@fn bar) {} }
			)test"},
			{"/decorator.ts", R"test(

				export const fn = () => {
					console.log('side effect')
				}
			)test"},
			{"/tsconfig.json", R"test(
{
				"compilerOptions": {
					"experimentalDecorators": true
				}
			})test"},
		},
		.entry_paths = {"/keep-these.ts"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCEOfUsingDeclarations) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Note: Only remove "using" if it's null or undefined and not awaited

				using null_remove = null
				using null_keep = null
				await using await_null_keep = null

				// This has a side effect: throwing an error
				using throw_keep = {}

				using dispose_keep = { [Symbol.dispose]() { console.log('side effect') } }
				await using await_asyncDispose_keep = { [Symbol.asyncDispose]() { console.log('side effect') } }

				using undef_remove = undefined
				using undef_keep = undefined
				await using await_undef_keep = undefined

				// Assume these have no side effects
				const Symbol_dispose_remove = Symbol.dispose
				const Symbol_asyncDispose_remove = Symbol.asyncDispose

				console.log(
					null_keep,
					undef_keep,
				)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.TreeShaking = true,
		},
	});
}

TEST(BundlerDCE, TestDCEOfExprAfterKeepNamesIssue3195) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				(() => {
					function f() {}
					firstImportantSideEffect(f());
				})();
				(() => {
					function g() {}
					debugger;
					secondImportantSideEffect(g());
				})();
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
			.KeepNames = true,
		},
	});
}

TEST(BundlerDCE, TestDropLabels) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				keep_1: require('foo1')
				DROP_1: require('bar1')
				exports.bar = function() {
					if (x) DROP_2: require('foo2')
					if (y) keep_2: require('bar2')
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.DropLabels = {"DROP_1", "DROP_2"},
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"foo1", true},
						{"bar2", true},
					},
				},
			},
		},
	});
}

TEST(BundlerDCE, TestRemoveCodeAfterLabelWithReturn) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function earlyReturn() {
					// This comes up when doing conditional compilation with "DropLabels"
					keep: {
						onlyWithKeep()
						return
					}
					onlyWithoutKeep()
				}
				function loop() {
					if (foo()) {
						keep: {
							bar()
							return;
						}
					}
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerDCE, TestDropLabelTreeShakingBugIssue3311) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const myFunc = ()=> {
					DROP: {console.log("drop")}
					console.log("keep")
				}
				export default myFunc
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.DropLabels = {},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestDCEOfSymbolInstances) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/class.js", R"test(

				class Remove1 {}
				class Remove2 { *[Symbol.iterator]() {} }
				class Remove3 { *[Symbol['iterator']]() {} }

				class Keep1 { *[Symbol.iterator]() {} [keep] }
				class Keep2 { [keep]; *[Symbol.iterator]() {} }
				class Keep3 { *[Symbol.wtf]() {} }
			)test"},
			{"/object.js", R"test(

				let remove1 = {}
				let remove2 = { *[Symbol.iterator]() {} }
				let remove3 = { *[Symbol['iterator']]() {} }

				let keep1 = { *[Symbol.iterator]() {}, [keep]: null }
				let keep2 = { [keep]: null, *[Symbol.iterator]() {} }
				let keep3 = { *[Symbol.wtf]() {} }
			)test"},
		},
		.entry_paths = {
			"/class.js",
			"/object.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCEOfNegatedBigints) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				let a = 1
				let b = -1
				let c = 1n
				let d = -1n
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCEOfIteratorSuperclassIssue4310) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Keep extends NotIterator {}
				class Remove extends Iterator {}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerDCE, TestDCEOfSymbolCtorCall) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const y0 = Symbol()
				const y1 = Symbol(undefined)
				const y2 = Symbol(null)
				const y3 = Symbol(true)
				const y4 = Symbol(123)
				const y5 = Symbol(123n)
				const y6 = Symbol('abc')
				const y7 = Symbol(/* @__PURE__ */ (() => Math.random() < 0.5)() ? 'x' : 'y')

				const n0 = Symbol({})
				const n1 = Symbol(/./)
				const n2 = Symbol(() => 0)
				const n3 = Symbol(x)
				const n4 = new Symbol('abc')
				const n5 = Symbol(1, 2, 3)
				const n6 = Symbol((() => Math.random() < 0.5)() ? 'x' : 'y')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDCE, TestDCEOfSymbolForCall) {
	dce_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const y0 = Symbol.for(undefined)
				const y1 = Symbol.for(null)
				const y2 = Symbol.for(true)
				const y3 = Symbol.for(123)
				const y4 = Symbol.for(123n)
				const y5 = Symbol.for('abc')
				const y6 = Symbol.for(/* @__PURE__ */ (() => Math.random() < 0.5)() ? 'x' : 'y')

				const n0 = Symbol.for()
				const n1 = Symbol.for({})
				const n2 = Symbol.for(/./)
				const n3 = Symbol.for(() => 0)
				const n4 = Symbol.for(x)
				const n5 = new Symbol.for('abc')
				const n6 = Symbol.for(1, 2, 3)
				const n7 = Symbol.for((() => Math.random() < 0.5)() ? 'x' : 'y')
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
