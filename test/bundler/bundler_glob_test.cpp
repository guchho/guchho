#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

Suite glob_suite{"glob"};

TEST(BundlerGlob, TestGlobBasicNoBundle) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const ab = Math.random() < 0.5 ? 'a.js' : 'b.js'
				console.log({
					concat: {
						require: require('./src/' + ab),
						import: import('./src/' + ab),
					},
					template: {
						require: require(`./src/${ab}`),
						import: import(`./src/${ab}`),
					},
				})
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

TEST(BundlerGlob, TestGlobBasicNoSplitting) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const ab = Math.random() < 0.5 ? 'a.js' : 'b.js'
				console.log({
					concat: {
						require: require('./src/' + ab),
						import: import('./src/' + ab),
					},
					template: {
						require: require(`./src/${ab}`),
						import: import(`./src/${ab}`),
					},
				})
			)test"},
			{"/src/a.js", "module.exports = 'a'"},
			{"/src/b.js", "module.exports = 'b'"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerGlob, TestTSGlobBasicNoSplitting) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				const ab = Math.random() < 0.5 ? 'a.ts' : 'b.ts'
				console.log({
					concat: {
						require: require('./src/' + ab),
						import: import('./src/' + ab),
					},
					template: {
						require: require(`./src/${ab}`),
						import: import(`./src/${ab}`),
					},
				})
			)test"},
			{"/src/a.ts", "module.exports = 'a'"},
			{"/src/b.ts", "module.exports = 'b'"},
		},
		.entry_paths = {"/entry.ts"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerGlob, TestGlobBasicSplitting) {
    glob_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"test(
                const ab = Math.random() < 0.5 ? 'a.js' : 'b.js'
                console.log({
                    concat: {
                        require: require('./src/' + ab),
                        import: import('./src/' + ab),
                    },
                    template: {
                        require: require(`./src/${ab}`),
                        import: import(`./src/${ab}`),
                    },
                })
            )test"},
            {"/src/a.js", "module.exports = 'a'"},
            {"/src/b.js", "module.exports = 'b'"},
        },
        .entry_paths = {
            "/entry.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .CodeSplitting = true,
            
			.AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerGlob, TestTSGlobBasicSplitting) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				const ab = Math.random() < 0.5 ? 'a.ts' : 'b.ts'
				console.log({
					concat: {
						require: require('./src/' + ab),
						import: import('./src/' + ab),
					},
					template: {
						require: require(`./src/${ab}`),
						import: import(`./src/${ab}`),
					},
				})
			)test"},
			{"/src/a.ts", "module.exports = 'a'"},
			{"/src/b.ts", "module.exports = 'b'"},
		},
		.entry_paths = {"/entry.ts"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerGlob, TestGlobDirDoesNotExist) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const ab = Math.random() < 0.5 ? 'a.js' : 'b.js'
				console.log({
					concat: {
						require: require('./src/' + ab),
						import: import('./src/' + ab),
					},
					template: {
						require: require(`./src/${ab}`),
						import: import(`./src/${ab}`),
					},
				})
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Could not resolve require("./src/**/*")
entry.js: ERROR: Could not resolve import("./src/**/*")
)scan",
	});
}

TEST(BundlerGlob, TestGlobNoMatches) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const ab = Math.random() < 0.5 ? 'a.js' : 'b.js'
				console.log({
					concat: {
						require: require('./src/' + ab + '.json'),
						import: import('./src/' + ab + '.json'),
					},
					template: {
						require: require(`./src/${ab}.json`),
						import: import(`./src/${ab}.json`),
					},
				})
			)test"},
			{"/src/dummy.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
		.expected_scan_log = R"scan(
entry.js: WARNING: The glob pattern require("./src/**/*.json") did not match any files
entry.js: WARNING: The glob pattern import("./src/**/*.json") did not match any files
)scan",
	});
}

TEST(BundlerGlob, TestGlobEntryPointAbsPath) {
    glob_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.js", R"test(
                works = true
            )test"},
        },
        .entry_paths = {
            "/Users/user/project/**/*.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerGlob, TestGlobWildcardSlash) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const ab = Math.random() < 0.5 ? 'a.js' : 'b.js'
				console.log({
					concat: {
						require: require('./src/' + ab + '.js'),
						import: import('./src/' + ab + '.js'),
					},
					template: {
						require: require(`./src/${ab}.js`),
						import: import(`./src/${ab}.js`),
					},
				})
			)test"},

			{"/src/file-a.js", "module.exports = 'a'"},
			{"/src/file-b.js", "module.exports = 'b'"},
			{"/src/file-a.js.map", "DO NOT BUNDLE"},
			{"/src/file-b.js.map", "DO NOT BUNDLE"},

			{"/src/nested/dir/file-a.js", "module.exports = 'a'"},
			{"/src/nested/dir/file-b.js", "module.exports = 'b'"},
			{"/src/nested/dir/file-a.js.map", "DO NOT BUNDLE"},
			{"/src/nested/dir/file-b.js.map", "DO NOT BUNDLE"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerGlob, TestGlobWildcardNoSlash) {
	glob_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const ab = Math.random() < 0.5 ? 'a.js' : 'b.js'
				console.log({
					concat: {
						require: require('./src/file-' + ab + '.js'),
						import: import('./src/file-' + ab + '.js'),
					},
					template: {
						require: require(`./src/file-${ab}.js`),
						import: import(`./src/file-${ab}.js`),
					},
				})
			)test"},

			{"/src/file-a.js", "module.exports = 'a'"},
			{"/src/file-b.js", "module.exports = 'b'"},
			{"/src/file-a.js.map", "DO NOT BUNDLE"},
			{"/src/file-b.js.map", "DO NOT BUNDLE"},

			{"/src/nested/dir/file-a.js", "DO NOT BUNDLE"},
			{"/src/nested/dir/file-b.js", "DO NOT BUNDLE"},
			{"/src/nested/dir/file-a.js.map", "DO NOT BUNDLE"},
			{"/src/nested/dir/file-b.js.map", "DO NOT BUNDLE"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

} // namespace bundler::test