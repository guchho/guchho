#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

static Suite importphase_suite{"importphase"};

TEST(BundlerImportPhase, ImportDeferExternalESM) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import defer * as foo0 from './foo.json'
				import defer * as foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.defer('./foo.json'),
					import.defer('./foo.json', { with: { type: 'json' } }),
					import.defer(`./${foo}.json`),
					import.defer(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {guchho::config::WildcardPattern{.Suffix = ".json"}},
                },
            },
        },
    });
}

TEST(BundlerImportPhase, ImportDeferExternalCommonJS) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import defer * as foo0 from './foo.json'
				import defer * as foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.defer('./foo.json'),
					import.defer('./foo.json', { with: { type: 'json' } }),
					import.defer(`./${foo}.json`),
					import.defer(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {guchho::config::WildcardPattern{.Suffix = ".json"}},
                },
            },
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
)",
    });
}

TEST(BundlerImportPhase, ImportDeferExternalIIFE) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import defer * as foo0 from './foo.json'
				import defer * as foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.defer('./foo.json'),
					import.defer('./foo.json', { with: { type: 'json' } }),
					import.defer(`./${foo}.json`),
					import.defer(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {guchho::config::WildcardPattern{.Suffix = ".json"}},
                },
            },
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
)",
    });
}

TEST(BundlerImportPhase, ImportDeferInternalESM) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import defer * as foo0 from './foo.json'
				import defer * as foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.defer('./foo.json'),
					import.defer('./foo.json', { with: { type: 'json' } }),
					import.defer(`./${foo}.json`),
					import.defer(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling with deferred imports is not supported unless they are external
entry.js: ERROR: Bundling with deferred imports is not supported unless they are external
entry.js: ERROR: Bundling with deferred imports is not supported unless they are external
entry.js: ERROR: Bundling with deferred imports is not supported unless they are external
entry.js: ERROR: Bundling with deferred imports is not supported unless they are external
entry.js: ERROR: Bundling with deferred imports is not supported unless they are external
)",
    });
}

TEST(BundlerImportPhase, ImportDeferInternalCommonJS) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import defer * as foo0 from './foo.json'
				import defer * as foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.defer('./foo.json'),
					import.defer('./foo.json', { with: { type: 'json' } }),
					import.defer(`./${foo}.json`),
					import.defer(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "cjs" output format is not supported
)",
    });
}

TEST(BundlerImportPhase, ImportDeferInternalIIFE) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import defer * as foo0 from './foo.json'
				import defer * as foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.defer('./foo.json'),
					import.defer('./foo.json', { with: { type: 'json' } }),
					import.defer(`./${foo}.json`),
					import.defer(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
entry.js: ERROR: Bundling deferred imports with the "iife" output format is not supported
)",
    });
}

TEST(BundlerImportPhase, ImportSourceExternalESM) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import source foo0 from './foo.json'
				import source foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.source('./foo.json'),
					import.source('./foo.json', { with: { type: 'json' } }),
					import.source(`./${foo}.json`),
					import.source(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {guchho::config::WildcardPattern{.Suffix = ".json"}},
                },
            },
        },
    });
}

TEST(BundlerImportPhase, ImportSourceExternalCommonJS) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import source foo0 from './foo.json'
				import source foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.source('./foo.json'),
					import.source('./foo.json', { with: { type: 'json' } }),
					import.source(`./${foo}.json`),
					import.source(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {guchho::config::WildcardPattern{.Suffix = ".json"}},
                },
            },
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
)",
    });
}

TEST(BundlerImportPhase, ImportSourceExternalIIFE) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import source foo0 from './foo.json'
				import source foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.source('./foo.json'),
					import.source('./foo.json', { with: { type: 'json' } }),
					import.source(`./${foo}.json`),
					import.source(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {guchho::config::WildcardPattern{.Suffix = ".json"}},
                },
            },
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
)",
    });
}

TEST(BundlerImportPhase, ImportSourceInternalESM) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import source foo0 from './foo.json'
				import source foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.source('./foo.json'),
					import.source('./foo.json', { with: { type: 'json' } }),
					import.source(`./${foo}.json`),
					import.source(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling with source phase imports is not supported unless they are external
entry.js: ERROR: Bundling with source phase imports is not supported unless they are external
entry.js: ERROR: Bundling with source phase imports is not supported unless they are external
entry.js: ERROR: Bundling with source phase imports is not supported unless they are external
entry.js: ERROR: Bundling with source phase imports is not supported unless they are external
entry.js: ERROR: Bundling with source phase imports is not supported unless they are external
)",
    });
}

TEST(BundlerImportPhase, ImportSourceInternalCommonJS) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import source foo0 from './foo.json'
				import source foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.source('./foo.json'),
					import.source('./foo.json', { with: { type: 'json' } }),
					import.source(`./${foo}.json`),
					import.source(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "cjs" output format is not supported
)",
    });
}

TEST(BundlerImportPhase, ImportSourceInternalIIFE) {
    importphase_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
				import source foo0 from './foo.json'
				import source foo1 from './foo.json' with { type: 'json' }

				console.log(
					foo0,
					foo1,
					import.source('./foo.json'),
					import.source('./foo.json', { with: { type: 'json' } }),
					import.source(`./${foo}.json`),
					import.source(`./${foo}.json`, { with: { type: 'json' } }),
				)
			)"},
            {"/foo.json", "{}"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
entry.js: ERROR: Bundling source phase imports with the "iife" output format is not supported
)",
    });
}



} // namespace bundler::test
