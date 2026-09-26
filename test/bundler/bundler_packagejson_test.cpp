#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

Suite packagejson_suite{"packagejson"};


TEST(BundlerPackageJSON, TestPackageJsonBrowserOverMainNode) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./main.esm.js",
					"browser": "./main.browser.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.esm.js", R"test(

				export default function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.browser.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserWithMainNode) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./main.esm.js",
					"browser": {
						"./main.js": "./main.browser.js",
						"./main.esm.js": "./main.browser.esm.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.esm.js", R"test(

				export default function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.browser.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.browser.esm.js", R"test(

				export default function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserIssue2002A) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", "require('pkg/sub')"},
			{"/Users/user/project/src/node_modules/pkg/package.json", R"test(
{
				"browser": {
					"./sub": "./sub/foo.js"
				}
			})test"},
			{"/Users/user/project/src/node_modules/pkg/sub/foo.js", "require('sub')"},
			{"/Users/user/project/src/node_modules/sub/package.json", R"test({ "main": "./bar" })test"},
			{"/Users/user/project/src/node_modules/sub/bar.js", "works()"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}


TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardImportAndRequireSeparateFiles) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import './test-main'
				import './test-module'
			)test"},
			{"/Users/user/project/src/test-main.js", R"test(

				console.log(require('demo-pkg'))
			)test"},
			{"/Users/user/project/src/test-module.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./module.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = 'main'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsEntryPointRequireOnly) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"require": "./require.js"
					},
					"module": "./module.js",
					"main": "./main.js"
				}
			)test"},
			{"/node_modules/pkg/require.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/node_modules/pkg/module.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/node_modules/pkg/main.js", R"test(

				console.log('FAILURE')
			)test"},
		},
		.entry_paths = {"pkg"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
ERROR: Could not resolve "pkg"
node_modules/pkg/package.json: NOTE: The path "." is not currently exported by package "pkg":
node_modules/pkg/package.json: NOTE: None of the conditions in the package definition ("require") match any of the currently active conditions ("browser", "default", "import"):
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonMain) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./custom-main.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/custom-main.js", R"test(

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

TEST(BundlerPackageJSON, TestPackageJsonBadMain) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./does-not-exist.js"
				}
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

TEST(BundlerPackageJSON, TestPackageJsonSyntaxErrorComment) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					// Single-line comment
					"a": 1
				}
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
        .expected_scan_log = R"scan(
Users/user/project/node_modules/demo-pkg/package.json: ERROR: JSON does not support comments
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonSyntaxErrorTrailingComma) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"a": 1,
					"b": 2,
				}
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
        .expected_scan_log = R"scan(
Users/user/project/node_modules/demo-pkg/package.json: ERROR: JSON does not support trailing commas
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonModule) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./main.esm.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.esm.js", R"test(

				export default function() {
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

TEST(BundlerPackageJSON, TestPackageJsonBrowserString) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"browser": "./browser"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/browser.js", R"test(

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

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapRelativeToRelative) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main",
					"browser": {
						"./main.js": "./main-browser",
						"./lib/util.js": "./lib/util-browser"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				const util = require('./lib/util')
				module.exports = function() {
					return ['main', util]
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main-browser.js", R"test(

				const util = require('./lib/util')
				module.exports = function() {
					return ['main-browser', util]
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/lib/util.js", R"test(

				module.exports = 'util'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/lib/util-browser.js", R"test(

				module.exports = 'util-browser'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapRelativeToModule) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main",
					"browser": {
						"./util.js": "util-browser"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				const util = require('./util')
				module.exports = function() {
					return ['main', util]
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/util.js", R"test(

				module.exports = 'util'
			)test"},
			{"/Users/user/project/node_modules/util-browser/index.js", R"test(

				module.exports = 'util-browser'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapRelativeDisabled) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main",
					"browser": {
						"./util-node.js": false
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				const util = require('./util-node')
				module.exports = function(obj) {
					return util.inspect(obj)
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/util-node.js", R"test(

				module.exports = require('util')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapModuleToRelative) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"browser": {
						"node-pkg": "./node-pkg-browser"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/node-pkg-browser.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				const fn = require('node-pkg')
				module.exports = function() {
					return fn()
				}
			)test"},
			{"/Users/user/project/node_modules/node-pkg/index.js", R"test(

				module.exports = function() {
					return 234
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

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapModuleToModule) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"browser": {
						"node-pkg": "node-pkg-browser"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/node-pkg-browser/index.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				const fn = require('node-pkg')
				module.exports = function() {
					return fn()
				}
			)test"},
			{"/Users/user/project/node_modules/node-pkg/index.js", R"test(

				module.exports = function() {
					return 234
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

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapModuleDisabled) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"browser": {
						"node-pkg": false
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				const fn = require('node-pkg')
				module.exports = function() {
					return fn()
				}
			)test"},
			{"/Users/user/project/node_modules/node-pkg/index.js", R"test(

				module.exports = function() {
					return 234
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

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapNativeModuleDisabled) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"browser": {
						"fs": false
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				const fs = require('fs')
				module.exports = function() {
					return fs.readFile()
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

TEST(BundlerPackageJSON, TestPackageJsonBrowserMapAvoidMissing) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'component-classes'
			)test"},
			{"/Users/user/project/node_modules/component-classes/package.json", R"test(

				{
					"browser": {
						"indexof": "component-indexof"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/component-classes/index.js", R"test(

				try {
					var index = require('indexof');
				} catch (err) {
					var index = require('component-indexof');
				}
			)test"},
			{"/Users/user/project/node_modules/component-indexof/index.js", R"test(

				module.exports = function() {
					return 234
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

TEST(BundlerPackageJSON, TestPackageJsonBrowserOverModuleBrowser) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./main.esm.js",
					"browser": "./main.browser.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.esm.js", R"test(

				export default function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.browser.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kBrowser,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}



TEST(BundlerPackageJSON, TestPackageJsonBrowserWithModuleBrowser) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./main.esm.js",
					"browser": {
						"./main.js": "./main.browser.js",
						"./main.esm.js": "./main.browser.esm.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.esm.js", R"test(

				export default function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.browser.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.browser.esm.js", R"test(

				export default function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kBrowser,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}



TEST(BundlerPackageJSON, TestPackageJsonBrowserNodeModulesNoExt) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {browser as a} from 'demo-pkg/no-ext'
				import {node as b} from 'demo-pkg/no-ext.js'
				import {browser as c} from 'demo-pkg/ext'
				import {browser as d} from 'demo-pkg/ext.js'
				console.log(a)
				console.log(b)
				console.log(c)
				console.log(d)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"browser": {
						"./no-ext": "./no-ext-browser.js",
						"./ext.js": "./ext-browser.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/no-ext.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/no-ext-browser.js", R"test(

				export let browser = 'browser'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/ext.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/ext-browser.js", R"test(

				export let browser = 'browser'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserNodeModulesIndexNoExt) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {browser as a} from 'demo-pkg/no-ext'
				import {node as b} from 'demo-pkg/no-ext/index.js'
				import {browser as c} from 'demo-pkg/ext'
				import {browser as d} from 'demo-pkg/ext/index.js'
				console.log(a)
				console.log(b)
				console.log(c)
				console.log(d)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"browser": {
						"./no-ext": "./no-ext-browser/index.js",
						"./ext/index.js": "./ext-browser/index.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/no-ext/index.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/no-ext-browser/index.js", R"test(

				export let browser = 'browser'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/ext/index.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/ext-browser/index.js", R"test(

				export let browser = 'browser'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserNoExt) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {browser as a} from './demo-pkg/no-ext'
				import {node as b} from './demo-pkg/no-ext.js'
				import {browser as c} from './demo-pkg/ext'
				import {browser as d} from './demo-pkg/ext.js'
				console.log(a)
				console.log(b)
				console.log(c)
				console.log(d)
			)test"},
			{"/Users/user/project/src/demo-pkg/package.json", R"test(

				{
					"browser": {
						"./no-ext": "./no-ext-browser.js",
						"./ext.js": "./ext-browser.js"
					}
				}
			)test"},
			{"/Users/user/project/src/demo-pkg/no-ext.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/src/demo-pkg/no-ext-browser.js", R"test(

				export let browser = 'browser'
			)test"},
			{"/Users/user/project/src/demo-pkg/ext.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/src/demo-pkg/ext-browser.js", R"test(

				export let browser = 'browser'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserIndexNoExt) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import {browser as a} from './demo-pkg/no-ext'
				import {node as b} from './demo-pkg/no-ext/index.js'
				import {browser as c} from './demo-pkg/ext'
				import {browser as d} from './demo-pkg/ext/index.js'
				console.log(a)
				console.log(b)
				console.log(c)
				console.log(d)
			)test"},
			{"/Users/user/project/src/demo-pkg/package.json", R"test(

				{
					"browser": {
						"./no-ext": "./no-ext-browser/index.js",
						"./ext/index.js": "./ext-browser/index.js"
					}
				}
			)test"},
			{"/Users/user/project/src/demo-pkg/no-ext/index.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/src/demo-pkg/no-ext-browser/index.js", R"test(

				export let browser = 'browser'
			)test"},
			{"/Users/user/project/src/demo-pkg/ext/index.js", R"test(

				export let node = 'node'
			)test"},
			{"/Users/user/project/src/demo-pkg/ext-browser/index.js", R"test(

				export let browser = 'browser'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}



TEST(BundlerPackageJSON, TestPackageJsonBrowserIssue2002B) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", "require('pkg/sub')"},
			{"/Users/user/project/src/node_modules/pkg/package.json", R"test(
{
				"browser": {
					"./sub": "./sub/foo.js",
					"./sub/sub": "./sub/bar.js"
				}
			})test"},
			{"/Users/user/project/src/node_modules/pkg/sub/foo.js", "require('sub')"},
			{"/Users/user/project/src/node_modules/pkg/sub/bar.js", "works()"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserIssue2002C) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", "require('pkg/sub')"},
			{"/Users/user/project/src/node_modules/pkg/package.json", R"test(
{
				"browser": {
					"./sub": "./sub/foo.js",
					"./sub/sub.js": "./sub/bar.js"
				}
			})test"},
			{"/Users/user/project/src/node_modules/pkg/sub/foo.js", "require('sub')"},
			{"/Users/user/project/src/node_modules/sub/index.js", "works()"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardImportOnly) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./module.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = 'main'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardRequireOnly) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				console.log(require('demo-pkg'))
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./module.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = 'main'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardImportAndRequireSameFile) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import value from 'demo-pkg'
				console.log(value, require('demo-pkg'))
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./module.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = 'main'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}



TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardImportAndRequireForceModuleBeforeMain) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import './test-main'
				import './test-module'
			)test"},
			{"/Users/user/project/src/test-main.js", R"test(

				console.log(require('demo-pkg'))
			)test"},
			{"/Users/user/project/src/test-module.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./module.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = 'main'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"module", "main"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardImportAndRequireImplicitMain) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import './test-index'
				import './test-module'
			)test"},
			{"/Users/user/project/src/test-index.js", R"test(

				console.log(require('demo-pkg'))
			)test"},
			{"/Users/user/project/src/test-module.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"module": "./module.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				module.exports = 'index'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardImportAndRequireImplicitMainForceModuleBeforeMain) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import './test-index'
				import './test-module'
			)test"},
			{"/Users/user/project/src/test-index.js", R"test(

				console.log(require('demo-pkg'))
			)test"},
			{"/Users/user/project/src/test-module.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"module": "./module.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				module.exports = 'index'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"module", "main"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonDualPackageHazardImportAndRequireBrowser) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import './test-main'
				import './test-module'
			)test"},
			{"/Users/user/project/src/test-main.js", R"test(

				console.log(require('demo-pkg'))
			)test"},
			{"/Users/user/project/src/test-module.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./module.js",
					"browser": {
						"./main.js": "./main.browser.js",
						"./module.js": "./module.browser.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = 'main'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.js", R"test(

				export default 'module'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.browser.js", R"test(

				module.exports = 'browser main'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/module.browser.js", R"test(

				export default 'browser module'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonMainFieldsA) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"a": "./a.js",
					"b": "./b.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/a.js", R"test(

				module.exports = 'a'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/b.js", R"test(

				export default 'b'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"a", "b"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonMainFieldsB) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import value from 'demo-pkg'
				console.log(value)
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"a": "./a.js",
					"b": "./b.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/a.js", R"test(

				module.exports = 'a'
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/b.js", R"test(

				export default 'b'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"b", "a"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonNeutralNoDefaultMainFields) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"main": "./main.js",
					"module": "./main.esm.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.esm.js", R"test(

				export default function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNeutral,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "demo-pkg"
Users/user/project/node_modules/demo-pkg/package.json: NOTE: The "main" field here was ignored. Main fields must be configured explicitly when using the "neutral" platform.
NOTE: You can mark the path "demo-pkg" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonNeutralExplicitMainFields) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/package.json", R"test(

				{
					"hello": "./main.js",
					"module": "./main.esm.js"
				}
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/main.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"hello"},
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNeutral,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsErrorInvalidModuleSpecifier) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
				import 'pkg2'
				import 'pkg3'
				import 'pkg4'
				import 'pkg5'
				import 'pkg6'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{ "exports": { ".": "./%%" } }
			)test"},
			{"/Users/user/project/node_modules/pkg2/package.json", R"test(

				{ "exports": { ".": "./%2f" } }
			)test"},
			{"/Users/user/project/node_modules/pkg3/package.json", R"test(

				{ "exports": { ".": "./%2F" } }
			)test"},
			{"/Users/user/project/node_modules/pkg4/package.json", R"test(

				{ "exports": { ".": "./%5c" } }
			)test"},
			{"/Users/user/project/node_modules/pkg5/package.json", R"test(

				{ "exports": { ".": "./%5C" } }
			)test"},
			{"/Users/user/project/node_modules/pkg6/package.json", R"test(

				{ "exports": { ".": "./%31.js" } }
			)test"},
			{"/Users/user/project/node_modules/pkg6/1.js", R"test(

				console.log(1)
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The module specifier "./%%" is invalid:
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg2"
Users/user/project/node_modules/pkg2/package.json: NOTE: The module specifier "./%2f" is invalid:
NOTE: You can mark the path "pkg2" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg3"
Users/user/project/node_modules/pkg3/package.json: NOTE: The module specifier "./%2F" is invalid:
NOTE: You can mark the path "pkg3" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg4"
Users/user/project/node_modules/pkg4/package.json: NOTE: The module specifier "./%5c" is invalid:
NOTE: You can mark the path "pkg4" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg5"
Users/user/project/node_modules/pkg5/package.json: NOTE: The module specifier "./%5C" is invalid:
NOTE: You can mark the path "pkg5" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsErrorInvalidPackageConfiguration) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
				import 'pkg2/foo'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{ "exports": { ".": false } }
			)test"},
			{"/Users/user/project/node_modules/pkg2/package.json", R"test(

				{ "exports": { "./foo": false } }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},.expected_scan_log = R"scan(
Users/user/project/node_modules/pkg1/package.json: WARNING: This value must be a string, an object, an array, or null
Users/user/project/node_modules/pkg2/package.json: WARNING: This value must be a string, an object, an array, or null
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The package configuration has an invalid value here:
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg2/foo"
Users/user/project/node_modules/pkg2/package.json: NOTE: The package configuration has an invalid value here:
NOTE: You can mark the path "pkg2/foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsErrorInvalidPackageTarget) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
				import 'pkg2'
				import 'pkg3'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{ "exports": { ".": "invalid" } }
			)test"},
			{"/Users/user/project/node_modules/pkg2/package.json", R"test(

				{ "exports": { ".": "./../pkg3" } }
			)test"},
			{"/Users/user/project/node_modules/pkg3/package.json", R"test(

				{ "exports": { ".": "./node_modules/pkg" } }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The package target "invalid" is invalid because it doesn't start with "./":
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg2"
Users/user/project/node_modules/pkg2/package.json: NOTE: The package target "./../pkg3" is invalid because it contains invalid segment "..":
NOTE: You can mark the path "pkg2" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg3"
Users/user/project/node_modules/pkg3/package.json: NOTE: The package target "./node_modules/pkg" is invalid because it contains invalid segment "node_modules":
NOTE: You can mark the path "pkg3" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsErrorPackagePathNotExported) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1/foo'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{ "exports": { ".": {} } }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1/foo"
Users/user/project/node_modules/pkg1/package.json: NOTE: The path "./foo" is not exported by package "pkg1":
NOTE: You can mark the path "pkg1/foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsErrorModuleNotFound) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{ "exports": { ".": "./foo.js" } }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The module "./foo.js" was not found on the file system:
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsErrorUnsupportedDirectoryImport) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
				import 'pkg2'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{ "exports": { ".": "./foo/" } }
			)test"},
			{"/Users/user/project/node_modules/pkg2/package.json", R"test(

				{ "exports": { ".": "./foo" } }
			)test"},
			{"/Users/user/project/node_modules/pkg2/foo/bar.js", R"test(

				console.log(bar)
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The module "./foo" was not found on the file system:
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg2"
Users/user/project/node_modules/pkg2/package.json: NOTE: Importing the directory "./foo" is forbidden by this package:
Users/user/project/node_modules/pkg2/package.json: NOTE: The presence of "exports" here makes importing a directory forbidden:
NOTE: You can mark the path "pkg2" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsErrorUnsupportedDirectoryImport) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#foo1/bar'
				import '#foo2/bar'
			)test"},
			{"/Users/user/project/package.json", R"test(

				{
					"imports": {
						"#foo1/*": "./foo1/*",
						"#foo2/bar": "./foo2/bar"
					}
				}
			)test"},
			{"/Users/user/project/foo1/bar/index.js", R"test(

				console.log(bar)
			)test"},
			{"/Users/user/project/foo2/bar/index.js", R"test(

				console.log(bar)
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "#foo1/bar"
Users/user/project/package.json: NOTE: Importing the directory "./foo1/bar" is forbidden by this package:
Users/user/project/package.json: NOTE: The presence of "imports" here makes importing a directory forbidden:
Users/user/project/src/entry.js: NOTE: Import from "/index.js" to get the file "Users/user/project/foo1/bar/index.js":
NOTE: You can mark the path "#foo1/bar" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "#foo2/bar"
Users/user/project/package.json: NOTE: Importing the directory "./foo2/bar" is forbidden by this package:
Users/user/project/package.json: NOTE: The presence of "imports" here makes importing a directory forbidden:
NOTE: You can mark the path "#foo2/bar" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsRequireOverImport) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				require('pkg')
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"import": "./import.js",
						"require": "./require.js",
						"default": "./default.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/import.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/require.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsImportOverRequire) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg'
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"require": "./require.js",
						"import": "./import.js",
						"default": "./default.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/require.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/import.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsDefaultOverImportAndRequire) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg'
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"default": "./default.js",
						"import": "./import.js",
						"require": "./require.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/require.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/import.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/default.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsEntryPointImportOverRequire) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"import": "./import.js",
						"require": "./require.js"
					},
					"module": "./module.js",
					"main": "./main.js"
				}
			)test"},
			{"/node_modules/pkg/import.js", R"test(

				console.log('SUCCESS')
			)test"},
			{"/node_modules/pkg/require.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/node_modules/pkg/module.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/node_modules/pkg/main.js", R"test(

				console.log('FAILURE')
			)test"},
		},
		.entry_paths = {"pkg"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
		},
	});
}



TEST(BundlerPackageJSON, TestPackageJsonExportsEntryPointModuleOverMain) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/node_modules/pkg/package.json", R"test(

				{
					"module": "./module.js",
					"main": "./main.js"
				}
			)test"},
			{"/node_modules/pkg/module.js", R"test(

				console.log('SUCCESS')
			)test"},
			{"/node_modules/pkg/main.js", R"test(

				console.log('FAILURE')
			)test"},
		},
		.entry_paths = {"pkg"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsEntryPointMainOnly) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/node_modules/pkg/package.json", R"test(

				{
					"main": "./main.js"
				}
			)test"},
			{"/node_modules/pkg/main.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"pkg"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsBrowser) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg'
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"node": "./node.js",
						"browser": "./browser.js",
						"default": "./default.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/node.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/browser.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kBrowser,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsNode) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg'
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"browser": "./browser.js",
						"node": "./node.js",
						"default": "./default.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/browser.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/node.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsNeutral) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg'
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"node": "./node.js",
						"browser": "./browser.js",
						"default": "./default.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/node.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/browser.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg/default.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNeutral,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsOrderIndependent) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1/foo/bar.js'
				import 'pkg2/foo/bar.js'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						"./": "./1/",
						"./foo/": "./2/"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/1/foo/bar.js", R"test(

				console.log('FAILURE')
			)test"},
			{"/Users/user/project/node_modules/pkg1/2/bar.js", R"test(

				console.log('SUCCESS')
			)test"},
			{"/Users/user/project/node_modules/pkg2/package.json", R"test(

				{
					"exports": {
						"./foo/": "./1/",
						"./": "./2/"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg2/1/bar.js", R"test(

				console.log('SUCCESS')
			)test"},
			{"/Users/user/project/node_modules/pkg2/2/foo/bar.js", R"test(

				console.log('FAILURE')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsWildcard) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1/foo'
				import 'pkg1/foo2'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						"./foo*": "./file*.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/file.js", R"test(

				console.log('SUCCESS')
			)test"},
			{"/Users/user/project/node_modules/pkg1/file2.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsErrorMissingTrailingSlash) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1/foo/bar'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{ "exports": { "./foo/": "./test" } }
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1/foo/bar"
Users/user/project/node_modules/pkg1/package.json: NOTE: The module specifier "./test" is invalid because it doesn't end in "/":
NOTE: You can mark the path "pkg1/foo/bar" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsCustomConditions) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						"custom1": "./custom1.js",
						"custom2": "./custom2.js",
						"default": "./default.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/custom2.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.Conditions = {"custom2"},
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsNotExactMissingExtension) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1/foo/bar'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						"./foo/": "./dir/"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/dir/bar.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsNotExactMissingExtensionPattern) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1/foo/bar'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						"./foo/*": "./dir/*"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/dir/bar.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1/foo/bar"
Users/user/project/node_modules/pkg1/package.json: NOTE: The module "./dir/bar" was not found on the file system:
Users/user/project/src/entry.js: NOTE: Import from "pkg1/foo/bar.js" to get the file "Users/user/project/node_modules/pkg1/dir/bar.js":
NOTE: You can mark the path "pkg1/foo/bar" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsExactMissingExtension) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1/foo/bar'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						"./foo/bar": "./dir/bar"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/dir/bar.js", R"test(

				console.log('SUCCESS')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1/foo/bar"
Users/user/project/node_modules/pkg1/package.json: NOTE: The module "./dir/bar" was not found on the file system:
NOTE: You can mark the path "pkg1/foo/bar" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsNoConditionsMatch) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
				import 'pkg1/foo.js'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						".": {
							"what": "./foo.js"
						},
						"./foo.js": {
							"what": "./foo.js"
						}
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/foo.js", R"test(

				console.log('FAILURE')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The path "." is not currently exported by package "pkg1":
Users/user/project/node_modules/pkg1/package.json: NOTE: None of the conditions in the package definition ("what") match any of the currently active conditions ("browser", "default", "import"):
Users/user/project/node_modules/pkg1/package.json: NOTE: Consider enabling the "what" condition if this package expects it to be enabled. You can use 'Conditions: []string{"what"}' to do that:
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1/foo.js"
Users/user/project/node_modules/pkg1/package.json: NOTE: The path "./foo.js" is not currently exported by package "pkg1":
Users/user/project/node_modules/pkg1/package.json: NOTE: None of the conditions in the package definition ("what") match any of the currently active conditions ("browser", "default", "import"):
Users/user/project/node_modules/pkg1/package.json: NOTE: Consider enabling the "what" condition if this package expects it to be enabled. You can use 'Conditions: []string{"what"}' to do that:
NOTE: You can mark the path "pkg1/foo.js" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsMustUseRequire) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg1'
				import 'pkg1/foo.js'
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						".": {
							"require": "./foo.js"
						},
						"./foo.js": {
							"require": "./foo.js"
						}
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/foo.js", R"test(

				console.log('FAILURE')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The path "." is not currently exported by package "pkg1":
Users/user/project/node_modules/pkg1/package.json: NOTE: None of the conditions in the package definition ("require") match any of the currently active conditions ("browser", "default", "import"):
Users/user/project/src/entry.js: NOTE: Consider using a "require()" call to import this file, which will work because the "require" condition is supported by this package:
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1/foo.js"
Users/user/project/node_modules/pkg1/package.json: NOTE: The path "./foo.js" is not currently exported by package "pkg1":
Users/user/project/node_modules/pkg1/package.json: NOTE: None of the conditions in the package definition ("require") match any of the currently active conditions ("browser", "default", "import"):
Users/user/project/src/entry.js: NOTE: Consider using a "require()" call to import this file, which will work because the "require" condition is supported by this package:
NOTE: You can mark the path "pkg1/foo.js" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsMustUseImport) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				require('pkg1')
				require('pkg1/foo.js')
			)test"},
			{"/Users/user/project/node_modules/pkg1/package.json", R"test(

				{
					"exports": {
						".": {
							"import": "./foo.js"
						},
						"./foo.js": {
							"import": "./foo.js"
						}
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg1/foo.js", R"test(

				console.log('FAILURE')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1"
Users/user/project/node_modules/pkg1/package.json: NOTE: The path "." is not currently exported by package "pkg1":
Users/user/project/node_modules/pkg1/package.json: NOTE: None of the conditions in the package definition ("import") match any of the currently active conditions ("browser", "default", "require"):
Users/user/project/src/entry.js: NOTE: Consider using an "import" statement to import this file, which will work because the "import" condition is supported by this package:
NOTE: You can mark the path "pkg1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also surround this "require" call with a try/catch block to handle this failure at run-time instead of bundle-time.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg1/foo.js"
Users/user/project/node_modules/pkg1/package.json: NOTE: The path "./foo.js" is not currently exported by package "pkg1":
Users/user/project/node_modules/pkg1/package.json: NOTE: None of the conditions in the package definition ("import") match any of the currently active conditions ("browser", "default", "require"):
Users/user/project/src/entry.js: NOTE: Consider using an "import" statement to import this file, which will work because the "import" condition is supported by this package:
NOTE: You can mark the path "pkg1/foo.js" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also surround this "require" call with a try/catch block to handle this failure at run-time instead of bundle-time.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsReverseLookup) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				require('pkg/path/to/real/file')
				require('pkg/path/to/other/file')
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"./lib/te*": {
							"default": "./path/to/re*.js"
						},
						"./extra/": {
							"default": "./path/to/"
						}
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/path/to/real/file.js", ""},
			{"/Users/user/project/node_modules/pkg/path/to/other/file.js", ""},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg/path/to/real/file"
Users/user/project/node_modules/pkg/package.json: NOTE: The path "./path/to/real/file" is not exported by package "pkg":
Users/user/project/node_modules/pkg/package.json: NOTE: The file "./path/to/real/file.js" is exported at path "./lib/teal/file":
Users/user/project/src/entry.js: NOTE: Import from "pkg/lib/teal/file" to get the file "Users/user/project/node_modules/pkg/path/to/real/file.js":
NOTE: You can mark the path "pkg/path/to/real/file" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also surround this "require" call with a try/catch block to handle this failure at run-time instead of bundle-time.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg/path/to/other/file"
Users/user/project/node_modules/pkg/package.json: NOTE: The path "./path/to/other/file" is not exported by package "pkg":
Users/user/project/node_modules/pkg/package.json: NOTE: The file "./path/to/other/file.js" is exported at path "./extra/other/file.js":
Users/user/project/src/entry.js: NOTE: Import from "pkg/extra/other/file.js" to get the file "Users/user/project/node_modules/pkg/path/to/other/file.js":
NOTE: You can mark the path "pkg/path/to/other/file" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also surround this "require" call with a try/catch block to handle this failure at run-time instead of bundle-time.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsPatternTrailers) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'pkg/path/foo.js/bar.js'
				import 'pkg2/features/abc'
				import 'pkg2/features/xyz.js'
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"./path/*/bar.js": "./dir/baz-*"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/dir/baz-foo.js", R"test(

				console.log('works')
			)test"},
			{"/Users/user/project/node_modules/pkg2/package.json", R"test(

				{
					"exports": {
						"./features/*": "./public/*.js",
						"./features/*.js": "./public/*.js"
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg2/public/abc.js", R"test(

				console.log('abc')
			)test"},
			{"/Users/user/project/node_modules/pkg2/public/xyz.js", R"test(

				console.log('xyz')
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsAlternatives) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import redApple from 'pkg/apples/red.js'
				import greenApple from 'pkg/apples/green.js'
				import redBook from 'pkg/books/red'
				import greenBook from 'pkg/books/green'
				console.log({redApple, greenApple, redBook, greenBook})
			)test"},
			{"/Users/user/project/node_modules/pkg/package.json", R"test(

				{
					"exports": {
						"./apples/": ["./good-apples/", "./bad-apples/"],
						"./books/*": ["./good-books/*-book.js", "./bad-books/*-book.js"]
					}
				}
			)test"},
			{"/Users/user/project/node_modules/pkg/good-apples/green.js", R"test(

				export default '🍏'
			)test"},
			{"/Users/user/project/node_modules/pkg/bad-apples/red.js", R"test(

				export default '🍎'
			)test"},
			{"/Users/user/project/node_modules/pkg/good-books/green-book.js", R"test(

				export default '📗'
			)test"},
			{"/Users/user/project/node_modules/pkg/bad-books/red-book.js", R"test(

				export default '📕'
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg/apples/red.js"
Users/user/project/node_modules/pkg/package.json: NOTE: The module "./good-apples/red.js" was not found on the file system:
NOTE: You can mark the path "pkg/apples/red.js" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/entry.js: ERROR: Could not resolve "pkg/books/red"
Users/user/project/node_modules/pkg/package.json: NOTE: The module "./good-books/red-book.js" was not found on the file system:
NOTE: You can mark the path "pkg/books/red" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonImports) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/foo/entry.js", R"test(

				import '#top-level'
				import '#nested/path.js'
				import '#star/c.js'
				import '#slash/d.js'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": {
						"#top-level": "./a.js",
						"#nested/path.js": "./b.js",
						"#star/*": "./some-star/*",
						"#slash/": "./some-slash/"
					}
				}
			)test"},
			{"/Users/user/project/src/a.js", "console.log('a.js')"},
			{"/Users/user/project/src/b.js", "console.log('b.js')"},
			{"/Users/user/project/src/some-star/c.js", "console.log('c.js')"},
			{"/Users/user/project/src/some-slash/d.js", "console.log('d.js')"},
		},
		.entry_paths = {"/Users/user/project/src/foo/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsRemapToOtherPackage) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#top-level'
				import '#nested/path.js'
				import '#star/c.js'
				import '#slash/d.js'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": {
						"#top-level": "pkg/a.js",
						"#nested/path.js": "pkg/b.js",
						"#star/*": "pkg/some-star/*",
						"#slash/": "pkg/some-slash/"
					}
				}
			)test"},
			{"/Users/user/project/src/node_modules/pkg/a.js", "console.log('a.js')"},
			{"/Users/user/project/src/node_modules/pkg/b.js", "console.log('b.js')"},
			{"/Users/user/project/src/node_modules/pkg/some-star/c.js", "console.log('c.js')"},
			{"/Users/user/project/src/node_modules/pkg/some-slash/d.js", "console.log('d.js')"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsErrorMissingRemappedPackage) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#foo'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": {
						"#foo": "bar"
					}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "#foo"
Users/user/project/src/package.json: NOTE: The remapped path "bar" could not be resolved:
NOTE: You can mark the path "#foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsInvalidPackageConfiguration) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#foo'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": "#foo"
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
				.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "#foo"
Users/user/project/src/package.json: NOTE: The package configuration has an invalid value here:
NOTE: You can mark the path "#foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
Users/user/project/src/package.json: WARNING: The value for "imports" must be an object
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsErrorEqualsHash) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": {}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "#"
Users/user/project/src/package.json: NOTE: This "imports" map was ignored because the module specifier "#" is invalid:
NOTE: You can mark the path "#" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsHashSlashWithWildcard) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#/foo.js'
				import '#/bar/baz.js'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": {
						"#/*": "./src/*"
					}
				}
			)test"},
			{"/Users/user/project/src/src/foo.js", "console.log('foo.js')"},
			{"/Users/user/project/src/src/bar/baz.js", "console.log('bar/baz.js')"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsHashSlashExactMatch) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#/'
				import '#/utils'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": {
						"#/": "./index.js",
						"#/utils": "./utils.js"
					}
				}
			)test"},
			{"/Users/user/project/src/index.js", "console.log('index.js')"},
			{"/Users/user/project/src/utils.js", "console.log('utils.js')"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsHashSlashWithConditions) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#/lib'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"imports": {
						"#/*": {
							"import": "./esm/*.js",
							"require": "./cjs/*.js"
						}
					}
				}
			)test"},
			{"/Users/user/project/src/esm/lib.js", "console.log('esm/lib.js')"},
			{"/Users/user/project/src/cjs/lib.js", "console.log('cjs/lib.js')"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportsHashSlashSymmetricWithExports) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import '#/components/button.js'
				import '#/utils/format.js'
			)test"},
			{"/Users/user/project/src/package.json", R"test(

				{
					"exports": { "./*": "./src/*" },
					"imports": { "#/*": "./src/*" }
				}
			)test"},
			{"/Users/user/project/src/src/components/button.js", "console.log('button.js')"},
			{"/Users/user/project/src/src/utils/format.js", "console.log('format.js')"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonMainFieldsErrorMessageDefault) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'foo'
			)test"},
			{"/Users/user/project/node_modules/foo/package.json", R"test(

				{
					"main": "./foo"
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "foo"
NOTE: You can mark the path "foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonMainFieldsErrorMessageNotIncluded) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'foo'
			)test"},
			{"/Users/user/project/node_modules/foo/package.json", R"test(

				{
					"main": "./foo"
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFields = {"some", "fields"},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "foo"
Users/user/project/node_modules/foo/package.json: NOTE: The "main" field here was ignored because the list of main fields to use is currently set to ["some", "fields"].
NOTE: You can mark the path "foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonMainFieldsErrorMessageEmpty) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'foo'
			)test"},
			{"/Users/user/project/node_modules/foo/package.json", R"test(

				{
					"main": "./foo"
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.MainFieldsSet = true,
			.MainFields = {},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "foo"
Users/user/project/node_modules/foo/package.json: NOTE: The "main" field here was ignored because the list of main fields to use is currently set to [].
NOTE: You can mark the path "foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonTypeShouldBeTypes) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", ""},
			{"/Users/user/project/package.json", R"test(

				{
					"main": "./src/index.js",
					"type": "./src/index.d.ts"
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
		.options = guchho::config::Options{
			.MainFields = {},
            .BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
        .expected_scan_log = R"scan(
Users/user/project/package.json: WARNING: "./src/index.d.ts" is not a valid value for the "type" field
Users/user/project/package.json: NOTE: TypeScript type declarations use the "types" field, not the "type" field:
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportSelfUsingRequire) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", R"test(

				module.exports = 'index'
				console.log(
					require("xyz"),
					require("xyz/bar"),
				)
			)test"},
			{"/Users/user/project/src/foo-import.js", R"test(

				export default 'foo'
			)test"},
			{"/Users/user/project/src/foo-require.js", R"test(

				module.exports = 'foo'
			)test"},
			{"/Users/user/project/package.json", R"test(

				{
					"name": "xyz",
					"exports": {
						".": "./src/index.js",
						"./bar": {
							"import": "./src/foo-import.js",
							"require": "./src/foo-require.js"
						}
					}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
		.options = guchho::config::Options{
			.MainFields = {},
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportSelfUsingImport) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", R"test(

				import xyz from "xyz"
				import foo from "xyz/bar"
				export default 'index'
				console.log(xyz, foo)
			)test"},
			{"/Users/user/project/src/foo-import.js", R"test(

				export default 'foo'
			)test"},
			{"/Users/user/project/src/foo-require.js", R"test(

				module.exports = 'foo'
			)test"},
			{"/Users/user/project/package.json", R"test(

				{
					"name": "xyz",
					"exports": {
						".": "./src/index.js",
						"./bar": {
							"import": "./src/foo-import.js",
							"require": "./src/foo-require.js"
						}
					}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
		.options = guchho::config::Options{
			.MainFields = {},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportSelfUsingRequireScoped) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", R"test(

				module.exports = 'index'
				console.log(
					require("@some-scope/xyz"),
					require("@some-scope/xyz/bar"),
				)
			)test"},
			{"/Users/user/project/src/foo-import.js", R"test(

				export default 'foo'
			)test"},
			{"/Users/user/project/src/foo-require.js", R"test(

				module.exports = 'foo'
			)test"},
			{"/Users/user/project/package.json", R"test(

				{
					"name": "@some-scope/xyz",
					"exports": {
						".": "./src/index.js",
						"./bar": {
							"import": "./src/foo-import.js",
							"require": "./src/foo-require.js"
						}
					}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
		.options = guchho::config::Options{
			.MainFields = {},
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportSelfUsingImportScoped) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", R"test(

				import xyz from "@some-scope/xyz"
				import foo from "@some-scope/xyz/bar"
				export default 'index'
				console.log(xyz, foo)
			)test"},
			{"/Users/user/project/src/foo-import.js", R"test(

				export default 'foo'
			)test"},
			{"/Users/user/project/src/foo-require.js", R"test(

				module.exports = 'foo'
			)test"},
			{"/Users/user/project/package.json", R"test(

				{
					"name": "@some-scope/xyz",
					"exports": {
						".": "./src/index.js",
						"./bar": {
							"import": "./src/foo-import.js",
							"require": "./src/foo-require.js"
						}
					}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
		.options = guchho::config::Options{
			.MainFields = {},
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportSelfUsingRequireFailure) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", R"test(

				require("xyz/src/foo.js")
			)test"},
			{"/Users/user/project/src/foo.js", R"test(

				module.exports = 'foo'
			)test"},
			{"/Users/user/project/package.json", R"test(

				{
					"name": "xyz",
					"exports": {
						".": "./src/index.js",
						"./bar": "./src/foo.js"
					}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
        .options = guchho::config::Options{
			.MainFields = {},
            .BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
		.expected_scan_log = R"scan(
Users/user/project/src/index.js: ERROR: Could not resolve "xyz/src/foo.js"
Users/user/project/package.json: NOTE: The path "./src/foo.js" is not exported by package "xyz":
Users/user/project/package.json: NOTE: The file "./src/foo.js" is exported at path "./bar":
Users/user/project/src/index.js: NOTE: Import from "xyz/bar" to get the file "Users/user/project/src/foo.js":
NOTE: You can mark the path "xyz/src/foo.js" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also surround this "require" call with a try/catch block to handle this failure at run-time instead of bundle-time.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonImportSelfUsingImportFailure) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", R"test(

				import "xyz/src/foo.js"
			)test"},
			{"/Users/user/project/src/foo.js", R"test(

				export default 'foo'
			)test"},
			{"/Users/user/project/package.json", R"test(

				{
					"name": "xyz",
					"exports": {
						".": "./src/index.js",
						"./bar": "./src/foo.js"
					}
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
		.options = guchho::config::Options{
			.MainFields = {},
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/Users/user/project/out.js",
		},
        .expected_scan_log = R"scan(
Users/user/project/src/index.js: ERROR: Could not resolve "xyz/src/foo.js"
Users/user/project/package.json: NOTE: The path "./src/foo.js" is not exported by package "xyz":
Users/user/project/package.json: NOTE: The file "./src/foo.js" is exported at path "./bar":
Users/user/project/src/index.js: NOTE: Import from "xyz/bar" to get the file "Users/user/project/src/foo.js":
NOTE: You can mark the path "xyz/src/foo.js" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestCommonJSVariableInESMTypeModule) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", "module.exports = null"},
			{"/package.json", R"test({ "type": "module" })test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
entry.js: WARNING: The CommonJS "module" variable is treated as a global variable in an ECMAScript module and may not work as expected
package.json: NOTE: This file is considered to be an ECMAScript module because the enclosing "package.json" file sets the type of this file to "module":
NOTE: Node's package format requires that CommonJS files in a "type": "module" package use the ".cjs" file extension.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonNodePathsIssue2752) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entry.js", R"test(

				import "pkg1"
				import "pkg2"
				import "@scope/pkg3/baz"
				import "@scope/pkg4"
			)test"},
			{"/usr/lib/pkg/pkg1/package.json", R"test({ "main": "./foo.js" })test"},
			{"/usr/lib/pkg/pkg1/foo.js", "console.log('pkg1')"},
			{"/lib/pkg/pkg2/package.json", R"test({ "exports": { ".": "./bar.js" } })test"},
			{"/lib/pkg/pkg2/bar.js", "console.log('pkg2')"},
			{"/var/lib/pkg/@scope/pkg3/package.json", R"test({ "browser": { "./baz.js": "./baz-browser.js" } })test"},
			{"/var/lib/pkg/@scope/pkg3/baz-browser.js", "console.log('pkg3')"},
			{"/tmp/pkg/@scope/pkg4/package.json", R"test({ "exports": { ".": { "import": "./bat.js" } } })test"},
			{"/tmp/pkg/@scope/pkg4/bat.js", "console.log('pkg4')"},
		},
		.entry_paths = {"/src/entry.js"},
		.options = guchho::config::Options{
			.AbsNodePaths = {"/usr/lib/pkg", "/lib/pkg", "/var/lib/pkg", "/tmp/pkg"},
			.BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonReversePackageExportsIssue3377) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/lib/msw-config.ts", R"test(

				import { setupWorker, type SetupWorker } from 'msw/browser'
				setupWorker();
			)test"},
			{"/node_modules/msw/package.json", R"test(
{
				"exports": {
					"./browser": {
						"node": null,
						"require": "./lib/browser/index.js",
						"import": "./lib/browser/index.mjs",
						"default": "./lib/browser/index.js"
					}
				}
			})test"},
			{"/node_modules/msw/browser/package.json", R"test(
{
				"main": "../lib/browser/index.js",
				"module": "../lib/browser/index.mjs"
			})test"},
			{"/node_modules/msw/lib/browser/index.js", "TEST FAILURE"},
			{"/node_modules/msw/lib/browser/index.mjs", "TEST FAILURE"},
		},
		.entry_paths = {"/lib/msw-config.ts"},
        	.options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
lib/msw-config.ts: ERROR: Could not resolve "msw/browser"
node_modules/msw/package.json: NOTE: The path "./browser" cannot be imported from package "msw" because it was explicitly disabled by the package author here:
NOTE: You can mark the path "msw/browser" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",

	});
}

TEST(BundlerPackageJSON, TestPackageJsonDisabledTypeModuleIssue3367) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import foo from 'foo'
				foo()
			)test"},
			{"/package.json", R"test(

				{
					"browser": {
						"foo": false
					}
				}
			)test"},
			{"/node_modules/foo/package.json", R"test(

				{
					"type": "module"
				}
			)test"},
			{"/node_modules/foo/index.js", R"test(

				export default function() {}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonSubpathImportNodeBuiltinIssue3485) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import fs from '#fs'
				import http from '#http'
				fs.readFileSync()
				http.createServer()
			)test"},
			{"/package.json", R"test(

				{
					"imports": {
						"#fs": {
							"node": "fs",
							"default": "./empty.js"
						},
						"#http": {
							"node": "node:http",
							"default": "./empty.js"
						}
					}
				}
			)test"},
			{"/empty.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBadExportsImportAndRequireWarningIssue3867) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "foo"
			)test"},
			{"/node_modules/foo/package.json", R"test(

				{
					"exports": {
						".": {
							"import": "./dist/node/index.js",
							"require": "./dist/node/index.cjs",
							"node": {
								"import": "./dist/node/index.js",
								"require": "./dist/node/index.cjs"
							},
							"browser": {
								"import": "./dist/browser/index.js",
								"require": "./dist/browser/index.cjs"
							},
							"worker": {
								"import": "./dist/browser/index.js",
								"require": "./dist/browser/index.cjs"
							}
						}
					}
				}
			)test"},
			{"/node_modules/foo/dist/node/index.js", ""},
			{"/node_modules/foo/dist/node/index.cjs", ""},
			{"/node_modules/foo/dist/browser/index.js", ""},
			{"/node_modules/foo/dist/browser/index.cjs", ""},
		},
		.entry_paths = {"/entry.js"},
        		.options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
node_modules/foo/package.json: DEBUG: The conditions "node" and "browser" and "worker" here will never be used as they come after both "import" and "require"
node_modules/foo/package.json: NOTE: The "import" condition comes earlier and will be used for all "import" statements:
node_modules/foo/package.json: NOTE: The "require" condition comes earlier and will be used for all "require" calls:
)scan",
		.debug_logs = true,

	});
}

TEST(BundlerPackageJSON, TestPackageJsonBadExportsDefaultWarningIssue3867) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "foo"
			)test"},
			{"/node_modules/foo/package.json", R"test(

				{
					"exports": {
						".": {
							"default": "./dist/node/index.js",
							"node": {
								"import": "./dist/node/index.js",
								"require": "./dist/node/index.cjs"
							},
							"browser": {
								"import": "./dist/browser/index.js",
								"require": "./dist/browser/index.cjs"
							},
							"worker": {
								"import": "./dist/browser/index.js",
								"require": "./dist/browser/index.cjs"
							}
						}
					}
				}
			)test"},
			{"/node_modules/foo/dist/node/index.js", ""},
			{"/node_modules/foo/dist/node/index.cjs", ""},
			{"/node_modules/foo/dist/browser/index.js", ""},
			{"/node_modules/foo/dist/browser/index.cjs", ""},
		},
		.entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,

			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"scan(
node_modules/foo/package.json: DEBUG: The conditions "node" and "browser" and "worker" here will never be used as they come after "default"
node_modules/foo/package.json: NOTE: The "default" condition comes earlier and will always be chosen:
)scan",
		.debug_logs = true,

	});
}

TEST(BundlerPackageJSON, TestPackageJsonExportsDefaultWarningIssue3887) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "foo"
			)test"},
			{"/node_modules/foo/dist/index.js", R"test(

				success()
			)test"},
			{"/node_modules/foo/package.json", R"test(

				{
					"exports": {
						".": {
							"node": "./dist/index.js",
							"require": "./dist/index.js",
							"import": "./dist/index.esm.js",
							"default": "./dist/index.esm.js"
						}
					}
				}
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

TEST(BundlerPackageJSON, TestConfusingNameCollisionsIssue4144) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { it } from 'mydependency'
				console.log(it())
			)test"},
			{"/node_modules/mydependency/package.json", R"test(

				{
					"main": "./package/index.js"
				}
			)test"},
			{"/node_modules/mydependency/package/index.js", R"test(

				export { it } from './utils'
				export let works = true
			)test"},
			{"/node_modules/mydependency/package/utils/index.js", R"test(

				export { it } from './utils'
			)test"},
			{"/node_modules/mydependency/package/utils/utils.js", R"test(

				// This should resolve to "../index.js" not "../../package.json"
				import { works } from '..'
				export function it() { return works }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,

			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerPackageJSON, TestPackageJsonBrowserMatchingTrailingSlashIssue4187) {
	packagejson_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import axios from "axios"
			)test"},
			{"/node_modules/axios/package.json", R"test(

				{
					"browser": {
						"./node/index.js": "./browser/index.js"
					}
				}
			)test"},
			{"/node_modules/axios/index.js", R"test(

				module.exports = require('./node/');
			)test"},
			{"/node_modules/axios/node/index.js", R"test(

				module.exports = { get: () => new Promise('Node') }
			)test"},
			{"/node_modules/axios/browser/index.js", R"test(

				module.exports = { get: () => new Promise('Browser') }
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
