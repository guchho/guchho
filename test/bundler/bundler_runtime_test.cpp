// Runtime-helper emission coverage.
//
// Every other bundler suite runs with "OmitRuntimeForTests", which strips the
// generated helpers out of the compared text so snapshots only show application
// code. These scenarios opt in with "include_runtime" so the helper declarations
// are part of the snapshot, which is what makes it possible to assert both
// halves of the contract: a helper that is reachable from the generated output
// is declared exactly once, and a helper that nothing reaches is not declared at
// all.
//
// Read each snapshot as a pair of assertions. The presence of "__defProp" next
// to "__export" is only meaningful because "__getOwnPropNames" and
// "__hasOwnProp" are absent, and the presence of those two is only meaningful
// because the helper that uses them is present.

#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

static Suite runtime_suite{"runtime"};

// SystemJS reports its exports with "exports(name, value)", so it never calls
// "__toCommonJS". Everything reachable here is "__export" and, by transitivity,
// "__defProp": "__getOwnPropNames" and "__hasOwnProp" must not be declared even
// though they sit in the same runtime source as the helpers that are kept. This
// is the case PLAN.md calls out, and the regression that motivated the shared
// "EntryPointEmitsToCommonJS" predicate.
TEST(BundlerRuntime, SystemJSKeepsOnlyReachableHelpers) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export const a = 1
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kSystem,
			.AbsOutputFile = "/out.js",
		},
	});
}

// The mirror of the case above, and the reason removal cannot be taken further:
// AMD returns the exports from its factory with a real
// "__toCommonJS(exports)" call, so the helper and its whole dependency chain are
// reachable and every one of them must survive.
TEST(BundlerRuntime, AMDRetainsToCommonJSAndItsDependencies) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export const a = 1
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kAMD,
			.AbsOutputFile = "/out.js",
		},
	});
}

// UMD shares the tail branch with AMD, so it must agree helper-for-helper.
TEST(BundlerRuntime, UMDRetainsToCommonJSAndItsDependencies) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export const a = 1
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kUMD,
			.AbsOutputFile = "/out.js",
		},
	});
}

// CommonJS assigns through "module.exports = __toCommonJS(...)" from the
// namespace-export part rather than from the tail. That is a different emission
// mechanism from the predicate the other formats share, so it gets its own
// coverage: the helper is required exactly once and its dependencies are kept.
TEST(BundlerRuntime, CommonJSRetainsToCommonJSAndItsDependencies) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export const a = 1
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
		},
	});
}

// A re-export that has to stay external needs "__reExport", which depends on
// "__copyProps", which in turn needs "__getOwnPropNames" and "__hasOwnProp".
// All four are genuinely reachable here, and the ordering matters: "__copyProps"
// reads helpers declared above it, so the snapshot also pins the dependency
// order the runtime source is emitted in.
TEST(BundlerRuntime, TransitiveHelperDependenciesAreRetained) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export * from 'dep'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kSystem,
			.AbsOutputFile = "/out.js",
		},
	});
}

// Three entry points, each publishing its own namespace object, so each one
// reaches "__export" and "__defProp" independently. The helpers must still be
// declared once between them rather than once per entry point.
TEST(BundlerRuntime, SharedHelperRequestedByManyModulesIsDeclaredOnce) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				export const a = 1
			)test"},
			{"/b.js", R"test(

				export const b = 2
			)test"},
			{"/entry.js", R"test(

				export const c = 3
			)test"},
		},
		.entry_paths = {"/a.js", "/b.js", "/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kSystem,
			.AbsOutputDir = "/out",
		},
	});
}

// CommonJS imported from ESM has to be interop-wrapped, which is a real use of
// "__toESM" and therefore of "__copyProps", "__create", "__getProtoOf",
// "__getOwnPropNames" and "__hasOwnProp". Guards against "fixing" the removal
// cases by pruning the interop chain.
TEST(BundlerRuntime, ESMImportingCommonJSRetainsInteropHelpers) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import def from './cjs.js'
				export const a = def
			)test"},
			{"/cjs.js", R"test(

				module.exports = { a: 1 }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
		},
	});
}

// "import()" stays "import()" when the target supports it, and the printer
// writes no "__toESM" call in that case. Requesting the helper anyway used to
// leave "__toESM" plus its dependency chain declared but unreferenced in every
// format that converts ESM import syntax away.
TEST(BundlerRuntime, NativeDynamicImportDoesNotRequestToESM) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export const p = import('dep')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
		},
	});
}

// The opposite direction. With "import()" unsupported the printer lowers it to
// "Promise.resolve().then(() => __toESM(require(...)))", so "__toESM" and its
// dependencies become reachable and must be emitted. Guards against the removal
// above over-correcting into a bundle that references an undeclared helper.
TEST(BundlerRuntime, LoweredDynamicImportRetainsToESM) {
	runtime_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export const p = import('dep')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.include_runtime = true,
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kDynamicImport,
		},
	});
}

} // namespace bundler::test