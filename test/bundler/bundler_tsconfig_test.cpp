#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"


namespace bundler::test {

static Suite tsconfig_suite{"tsconfig"};

TEST(BundlerTSConfig, TsconfigPaths) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.ts", R"(
                import baseurl_dot from './baseurl_dot'
                import baseurl_nested from './baseurl_nested'
                console.log(baseurl_dot, baseurl_nested)
            )"},

            {"/Users/user/project/baseurl_dot/index.ts", R"(
                import test0 from 'test0'
                import test1 from 'test1/foo'
                import test2 from 'test2/foo'
                import test3 from 'test3/foo'
                import test4 from 'test4/foo'
                import test5 from 'test5/foo'
                import absoluteIn from './absolute-in'
                import absoluteInStar from './absolute-in-star'
                import absoluteOut from './absolute-out'
                import absoluteOutStar from './absolute-out-star'
                export default {
                    test0,
                    test1,
                    test2,
                    test3,
                    test4,
                    test5,
                    absoluteIn,
                    absoluteInStar,
                    absoluteOut,
                    absoluteOutStar,
                }
            )"},
            {"/Users/user/project/baseurl_dot/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "test0": ["./test0-success.ts"],
                            "test1/*": ["./test1-success.ts"],
                            "test2/*": ["./test2-success/*"],
                            "t*t3/foo": ["./test3-succ*s.ts"],
                            "test4/*": ["./test4-first/*", "./test4-second/*"],
                            "test5/*": ["./test5-first/*", "./test5-second/*"],
                            "/virtual-in/test": ["./actual/test"],
                            "/virtual-in-star/*": ["./actual/*"],
                            "/virtual-out/test": ["/Users/user/project/baseurl_dot/actual/test"],
                            "/virtual-out-star/*": ["/Users/user/project/baseurl_dot/actual/*"],
                        }
                    }
                }
            )"},
            {"/Users/user/project/baseurl_dot/test0-success.ts", R"(
                export default 'test0-success'
            )"},
            {"/Users/user/project/baseurl_dot/test1-success.ts", R"(
                export default 'test1-success'
            )"},
            {"/Users/user/project/baseurl_dot/test2-success/foo.ts", R"(
                export default 'test2-success'
            )"},
            {"/Users/user/project/baseurl_dot/test3-success.ts", R"(
                export default 'test3-success'
            )"},
            {"/Users/user/project/baseurl_dot/test4-first/foo.ts", R"(
                export default 'test4-success'
            )"},
            {"/Users/user/project/baseurl_dot/test5-second/foo.ts", R"(
                export default 'test5-success'
            )"},
            {"/Users/user/project/baseurl_dot/absolute-in.ts", R"(
                export {default} from '/virtual-in/test'
            )"},
            {"/Users/user/project/baseurl_dot/absolute-in-star.ts", R"(
                export {default} from '/virtual-in-star/test'
            )"},
            {"/Users/user/project/baseurl_dot/absolute-out.ts", R"(
                export {default} from '/virtual-out/test'
            )"},
            {"/Users/user/project/baseurl_dot/absolute-out-star.ts", R"(
                export {default} from '/virtual-out-star/test'
            )"},
            {"/Users/user/project/baseurl_dot/actual/test.ts", R"(
                export default 'absolute-success'
            )"},

            {"/Users/user/project/baseurl_nested/index.ts", R"(
                import test0 from 'test0'
                import test1 from 'test1/foo'
                import test2 from 'test2/foo'
                import test3 from 'test3/foo'
                import test4 from 'test4/foo'
                import test5 from 'test5/foo'
                import absoluteIn from './absolute-in'
                import absoluteInStar from './absolute-in-star'
                import absoluteOut from './absolute-out'
                import absoluteOutStar from './absolute-out-star'
                export default {
                    test0,
                    test1,
                    test2,
                    test3,
                    test4,
                    test5,
                    absoluteIn,
                    absoluteInStar,
                    absoluteOut,
                    absoluteOutStar,
                }
            )"},
            {"/Users/user/project/baseurl_nested/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": "nested",
                        "paths": {
                            "test0": ["./test0-success.ts"],
                            "test1/*": ["./test1-success.ts"],
                            "test2/*": ["./test2-success/*"],
                            "t*t3/foo": ["./test3-succ*s.ts"],
                            "test4/*": ["./test4-first/*", "./test4-second/*"],
                            "test5/*": ["./test5-first/*", "./test5-second/*"],
                            "/virtual-in/test": ["./actual/test"],
                            "/virtual-in-star/*": ["./actual/*"],
                            "/virtual-out/test": ["/Users/user/project/baseurl_nested/nested/actual/test"],
                            "/virtual-out-star/*": ["/Users/user/project/baseurl_nested/nested/actual/*"],
                        }
                    }
                }
            )"},
            {"/Users/user/project/baseurl_nested/nested/test0-success.ts", R"(
                export default 'test0-success'
            )"},
            {"/Users/user/project/baseurl_nested/nested/test1-success.ts", R"(
                export default 'test1-success'
            )"},
            {"/Users/user/project/baseurl_nested/nested/test2-success/foo.ts", R"(
                export default 'test2-success'
            )"},
            {"/Users/user/project/baseurl_nested/nested/test3-success.ts", R"(
                export default 'test3-success'
            )"},
            {"/Users/user/project/baseurl_nested/nested/test4-first/foo.ts", R"(
                export default 'test4-success'
            )"},
            {"/Users/user/project/baseurl_nested/nested/test5-second/foo.ts", R"(
                export default 'test5-success'
            )"},
            {"/Users/user/project/baseurl_nested/absolute-in.ts", R"(
                export {default} from '/virtual-in/test'
            )"},
            {"/Users/user/project/baseurl_nested/absolute-in-star.ts", R"(
                export {default} from '/virtual-in/test'
            )"},
            {"/Users/user/project/baseurl_nested/absolute-out.ts", R"(
                export {default} from '/virtual-out/test'
            )"},
            {"/Users/user/project/baseurl_nested/absolute-out-star.ts", R"(
                export {default} from '/virtual-out-star/test'
            )"},
            {"/Users/user/project/baseurl_nested/nested/actual/test.ts", R"(
                export default 'absolute-success'
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPathsNoBaseURL) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.ts", R"(
                import simple from './simple'
                import extended from './extended'
                console.log(simple, extended)
            )"},

            {"/Users/user/project/simple/index.ts", R"(
                import test0 from 'test0'
                import test1 from 'test1/foo'
                import test2 from 'test2/foo'
                import test3 from 'test3/foo'
                import test4 from 'test4/foo'
                import test5 from 'test5/foo'
                import absolute from './absolute'
                export default {
                    test0,
                    test1,
                    test2,
                    test3,
                    test4,
                    test5,
                    absolute,
                }
            )"},
            {"/Users/user/project/simple/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "test0": ["./test0-success.ts"],
                            "test1/*": ["./test1-success.ts"],
                            "test2/*": ["./test2-success/*"],
                            "t*t3/foo": ["./test3-succ*s.ts"],
                            "test4/*": ["./test4-first/*", "./test4-second/*"],
                            "test5/*": ["./test5-first/*", "./test5-second/*"],
                            "/virtual/*": ["./actual/*"],
                        }
                    }
                }
            )"},
            {"/Users/user/project/simple/test0-success.ts", R"(
                export default 'test0-success'
            )"},
            {"/Users/user/project/simple/test1-success.ts", R"(
                export default 'test1-success'
            )"},
            {"/Users/user/project/simple/test2-success/foo.ts", R"(
                export default 'test2-success'
            )"},
            {"/Users/user/project/simple/test3-success.ts", R"(
                export default 'test3-success'
            )"},
            {"/Users/user/project/simple/test4-first/foo.ts", R"(
                export default 'test4-success'
            )"},
            {"/Users/user/project/simple/test5-second/foo.ts", R"(
                export default 'test5-success'
            )"},
            {"/Users/user/project/simple/absolute.ts", R"(
                export {default} from '/virtual/test'
            )"},
            {"/Users/user/project/simple/actual/test.ts", R"(
                export default 'absolute-success'
            )"},

            {"/Users/user/project/extended/index.ts", R"(
                import test0 from 'test0'
                import test1 from 'test1/foo'
                import test2 from 'test2/foo'
                import test3 from 'test3/foo'
                import test4 from 'test4/foo'
                import test5 from 'test5/foo'
                import absolute from './absolute'
                export default {
                    test0,
                    test1,
                    test2,
                    test3,
                    test4,
                    test5,
                    absolute,
                }
            )"},
            {"/Users/user/project/extended/tsconfig.json", R"(
                {
                    "extends": "./nested/tsconfig.json"
                }
            )"},
            {"/Users/user/project/extended/nested/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "test0": ["./test0-success.ts"],
                            "test1/*": ["./test1-success.ts"],
                            "test2/*": ["./test2-success/*"],
                            "t*t3/foo": ["./test3-succ*s.ts"],
                            "test4/*": ["./test4-first/*", "./test4-second/*"],
                            "test5/*": ["./test5-first/*", "./test5-second/*"],
                            "/virtual/*": ["./actual/*"],
                        }
                    }
                }
            )"},
            {"/Users/user/project/extended/nested/test0-success.ts", R"(
                export default 'test0-success'
            )"},
            {"/Users/user/project/extended/nested/test1-success.ts", R"(
                export default 'test1-success'
            )"},
            {"/Users/user/project/extended/nested/test2-success/foo.ts", R"(
                export default 'test2-success'
            )"},
            {"/Users/user/project/extended/nested/test3-success.ts", R"(
                export default 'test3-success'
            )"},
            {"/Users/user/project/extended/nested/test4-first/foo.ts", R"(
                export default 'test4-success'
            )"},
            {"/Users/user/project/extended/nested/test5-second/foo.ts", R"(
                export default 'test5-success'
            )"},
            {"/Users/user/project/extended/absolute.ts", R"(
                export {default} from '/virtual/test'
            )"},
            {"/Users/user/project/extended/nested/actual/test.ts", R"(
                export default 'absolute-success'
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigBadPathsNoBaseURL) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.ts", R"(
                import "should-not-be-imported"
            )"},
            {"/Users/user/project/should-not-be-imported.ts", R"(
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "test": [
                                ".",
                                "..",
                                "./good",
                                ".\\good",
                                "../good",
                                "..\\good",
                                "/good",
                                "\\good",
                                "c:/good",
                                "c:\\good",
                                "C:/good",
                                "C:\\good",

                                "bad",
                                "@bad/core",
                                ".*/bad",
                                "..*/bad",
                                "c*:\\bad",
                                "c:*\\bad",
                                "http://bad"
                            ]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/entry.ts: ERROR: Could not resolve "should-not-be-imported"
NOTE: Use the relative path "./should-not-be-imported" to reference the file "Users/user/project/should-not-be-imported.ts". Without the leading "./", the path "should-not-be-imported" is being interpreted as a package path instead.
Users/user/project/tsconfig.json: WARNING: Non-relative path "bad" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
Users/user/project/tsconfig.json: WARNING: Non-relative path "@bad/core" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
Users/user/project/tsconfig.json: WARNING: Non-relative path ".*/bad" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
Users/user/project/tsconfig.json: WARNING: Non-relative path "..*/bad" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
Users/user/project/tsconfig.json: WARNING: Non-relative path "c*:\\bad" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
Users/user/project/tsconfig.json: WARNING: Non-relative path "c:*\\bad" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
Users/user/project/tsconfig.json: WARNING: Non-relative path "http://bad" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
)",
    });
}

TEST(BundlerTSConfig, TsconfigPathsOverriddenBaseURL) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import test from '#/test'
                console.log(test)
            )"},
            {"/Users/user/project/src/test.ts", R"(
                export default 123
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "extends": "./tsconfig.paths.json",
                    "compilerOptions": {
                        "baseUrl": "./src"
                    }
                }
            )"},
            {"/Users/user/project/tsconfig.paths.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "#/*": ["./*"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPathsOverriddenBaseURLDifferentDir) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import test from '#/test'
                console.log(test)
            )"},
            {"/Users/user/project/src/test.ts", R"(
                export default 123
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "../tsconfig.paths.json",
                    "compilerOptions": {
                        "baseUrl": "./"
                    }
                }
            )"},
            {"/Users/user/project/tsconfig.paths.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "#/*": ["./*"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPathsMissingBaseURL) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import test from '#/test'
                console.log(test)
            )"},
            {"/Users/user/project/src/test.ts", R"(
                export default 123
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "../tsconfig.paths.json",
                    "compilerOptions": {
                    }
                }
            )"},
            {"/Users/user/project/tsconfig.paths.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "#/*": ["./*"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/entry.ts: ERROR: Could not resolve "#/test"
NOTE: You can mark the path "#/test" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)",
    });
}

TEST(BundlerTSConfig, TsconfigPathsTypeOnly) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.ts", R"(
                import { fib } from "fib";

                console.log(fib(10));
            )"},
            {"/Users/user/project/node_modules/fib/index.js", R"(
                export function fib(input) {
                    if (input < 2) {
                        return input;
                    }
                    return fib(input - 1) + fib(input - 2);
                }
            )"},
            {"/Users/user/project/fib-local.d.ts", R"(
                export function fib(input: number): number;
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "fib": ["fib-local.d.ts"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJSX) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.tsx", R"(
                console.log(<><div/><div/></>)
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "R.c",
                        "jsxFragmentFactory": "R.F"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigNestedJSX) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.ts", R"(
                import factory from './factory'
                import fragment from './fragment'
                import both from './both'
                console.log(factory, fragment, both)
            )"},
            {"/Users/user/project/factory/index.tsx", R"(
                export default <><div/><div/></>
            )"},
            {"/Users/user/project/factory/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "h"
                    }
                }
            )"},
            {"/Users/user/project/fragment/index.tsx", R"(
                export default <><div/><div/></>
            )"},
            {"/Users/user/project/fragment/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsxFragmentFactory": "a.b"
                    }
                }
            )"},
            {"/Users/user/project/both/index.tsx", R"(
                export default <><div/><div/></>
            )"},
            {"/Users/user/project/both/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "R.c",
                        "jsxFragmentFactory": "R.F"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPreserveJSX) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.tsx", R"(
                console.log(<><div/><div/></>)
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "preserve" // This should be ignored
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPreserveJSXAutomatic) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.tsx", R"(
                console.log(<><div/><div/></>)
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "preserve" // This should be ignored
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"react/jsx-runtime", true}},
                },
            },
            .JSX = guchho::config::JSXOptions{
                .AutomaticRuntime = true,
            },

        },
    });
}

TEST(BundlerTSConfig, TsconfigReactJSX) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.tsx", R"(
                console.log(<><div/><div/></>)
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react-jsx",
                        "jsxImportSource": "notreact"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"notreact/jsx-runtime", true}},
                },
            },
        },
    });
}

TEST(BundlerTSConfig, TsconfigReactJSXDev) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.tsx", R"(
				console.log(<><div/><div/></>)
			)"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react-jsxdev"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"react/jsx-dev-runtime", true}},
                },
            },
        },
    });
}

TEST(BundlerTSConfig, TsconfigReactJSXWithDevInMainConfig) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.tsx", R"(
				console.log(<><div/><div/></>)
			)"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react-jsx"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"react/jsx-dev-runtime", true}},
                },
            },
            .JSX = guchho::config::JSXOptions{
                .Development = true,
            },

        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonBaseUrl) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.js", R"(
                import fn from 'lib/util'
                console.log(fn())
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": "."
                    }
                }
            )"},
            {"/Users/user/project/src/lib/util.js", R"(
                module.exports = function() {
                    return 123
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, JsconfigJsonBaseUrl) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.js", R"(
                import fn from 'lib/util'
                console.log(fn())
            )"},
            {"/Users/user/project/src/jsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": "."
                    }
                }
            )"},
            {"/Users/user/project/src/lib/util.js", R"(
                module.exports = function() {
                    return 123
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonAbsoluteBaseUrl) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.js", R"(
                import fn from 'lib/util'
                console.log(fn())
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": "/Users/user/project/src"
                    }
                }
            )"},
            {"/Users/user/project/src/lib/util.js", R"(
                module.exports = function() {
                    return 123
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonCommentAllowed) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.js", R"(
                import fn from 'lib/util'
                console.log(fn())
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    // Single-line comment
                    "compilerOptions": {
                        "baseUrl": "."
                    }
                }
            )"},
            {"/Users/user/project/src/lib/util.js", R"(
                module.exports = function() {
                    return 123
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonTrailingCommaAllowed) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.js", R"(
                import fn from 'lib/util'
                console.log(fn())
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                    },
                }
            )"},
            {"/Users/user/project/src/lib/util.js", R"(
                module.exports = function() {
                    return 123
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonExtends) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.jsx", R"(
                console.log(<div/>, <></>)
            )"},
            {"/tsconfig.json", R"(
                {
                    "extends": "./base",
                    "compilerOptions": {
                        "jsxFragmentFactory": "derivedFragment"
                    }
                }
            )"},
            {"/base.json", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "baseFactory",
                        "jsxFragmentFactory": "baseFragment"
                    }
                }
            )"},
        },
        .entry_paths = {"/entry.jsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonExtendsAbsolute) {
    tsconfig_suite.ExpectBundledUnix(Bundled{
        .files = {
            {"/Users/user/project/entry.jsx", R"(
                console.log(<div/>, <></>)
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "extends": "/Users/user/project/base.json",
                    "compilerOptions": {
                        "jsxFragmentFactory": "derivedFragment"
                    }
                }
            )"},
            {"/Users/user/project/base.json", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "baseFactory",
                        "jsxFragmentFactory": "baseFragment"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/entry.jsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });

    tsconfig_suite.ExpectBundledWindows(Bundled{
        .files = {
            {R"(C:\Users\user\project\entry.jsx)", R"(
                console.log(<div/>, <></>)
            )"},
            {R"(C:\Users\user\project\tsconfig.json)", R"(
                {
                    "extends": "C:\\Users\\user\\project\\base.json",
                    "compilerOptions": {
                        "jsxFragmentFactory": "derivedFragment"
                    }
                }
            )"},
            {R"(C:\Users\user\project\base.json)", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "baseFactory",
                        "jsxFragmentFactory": "baseFragment"
                    }
                }
            )"},
        },
        .entry_paths = {R"(C:\Users\user\project\entry.jsx)"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = R"(C:\out.js)",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonExtendsThreeLevels) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.jsx", R"(
                import "test/import.js"
                console.log(<div/>, <></>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "./path1/base",
                    "compilerOptions": {
                        "jsxFragmentFactory": "derivedFragment"
                    }
                }
            )"},
            {"/Users/user/project/src/path1/base.json", R"(
                {
                    "extends": "../path2/base2"
                }
            )"},
            {"/Users/user/project/src/path2/base2.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "test/*": ["./works/*"]
                        },
                        "jsxFactory": "baseFactory",
                        "jsxFragmentFactory": "baseFragment"
                    }
                }
            )"},
            {"/Users/user/project/src/path2/works/import.js", R"(
                console.log('works')
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.jsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonExtendsLoop) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                console.log(123)
            )"},
            {"/tsconfig.json", R"(
                {
                    "extends": "./base.json"
                }
            )"},
            {"/base.json", R"(
                {
                    "extends": "./tsconfig"
                }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"(base.json: WARNING: Base config file "./tsconfig" forms cycle
)",
    });
}

TEST(BundlerTSConfig, TsconfigJsonExtendsPackage) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.jsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "@package/foo/tsconfig.json"
                }
            )"},
            {"/Users/user/project/node_modules/@package/foo/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "worked"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.jsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonOverrideMissing) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.ts", R"(
                import 'foo'
            )"},
            {"/Users/user/project/src/foo-bad.ts", R"(
                console.log('bad')
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "foo": ["./foo-bad.ts"]
                        }
                    }
                }
            )"},
            {"/Users/user/project/other/foo-good.ts", R"(
                console.log('good')
            )"},
            {"/Users/user/project/other/config-for-ts.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "foo": ["./foo-good.ts"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .TSConfigPath = "/Users/user/project/other/config-for-ts.json",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonOverrideNodeModules) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.ts", R"(
                import 'foo'
            )"},
            {"/Users/user/project/src/node_modules/foo/index.js", R"(
                console.log('default')
            )"},
            {"/Users/user/project/src/foo-bad.ts", R"(
                console.log('bad')
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "foo": ["./foo-bad.ts"]
                        }
                    }
                }
            )"},
            {"/Users/user/project/other/foo-good.ts", R"(
                console.log('good')
            )"},
            {"/Users/user/project/other/config-for-ts.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "foo": ["./foo-good.ts"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .TSConfigPath = "/Users/user/project/other/config-for-ts.json",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonOverrideInvalid) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", ""},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .TSConfigPath = "/this/file/doesn't/exist/tsconfig.json",
        },
        .expected_scan_log = R"(ERROR: Cannot find tsconfig file "this/file/doesn't/exist/tsconfig.json"
)",
    });
}

TEST(BundlerTSConfig, TsconfigJsonNodeModulesImplicitFile) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "foo"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react",
                        "jsxFactory": "worked"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonNodeModulesTsconfigPathExact) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "foo"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/package.json", R"(
                {
                    "tsconfig": "over/here.json"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/over/here.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react",
                        "jsxFactory": "worked"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonNodeModulesTsconfigPathImplicitJson) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "foo"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/package.json", R"(
                {
                    "tsconfig": "over/here"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/over/here.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react",
                        "jsxFactory": "worked"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonNodeModulesTsconfigPathDirectory) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "foo"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/package.json", R"(
                {
                    "tsconfig": "over/here"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/over/here/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react",
                        "jsxFactory": "worked"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonNodeModulesTsconfigPathBad) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "foo"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/package.json", R"(
                {
                    "tsconfig": "over/here.json"
                }
            )"},
            {"/Users/user/project/src/node_modules/foo/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsx": "react",
                        "jsxFactory": "THIS SHOULD NOT BE LOADED"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/tsconfig.json: WARNING: Cannot find base config file "foo"
)",
    });
}

TEST(BundlerTSConfig, TsconfigJsonInsideNodeModules) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/app/entry.tsx", R"(
                import 'foo'
            )"},
            {"/Users/user/project/src/node_modules/foo/index.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/node_modules/foo/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "jsxFactory": "TEST_FAILED"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/app/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigWarningsInsideNodeModules) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.tsx", R"(
                import "./foo"
                import "bar"
            )"},

            {"/Users/user/project/src/foo/tsconfig.json", R"({ "extends": "extends for foo" })"},
            {"/Users/user/project/src/foo/index.js", R"()"},

            {"/Users/user/project/src/node_modules/bar/tsconfig.json", R"({ "extends": "extends for bar" })"},
            {"/Users/user/project/src/node_modules/bar/index.js", R"()"},
        },
        .entry_paths = {"/Users/user/project/src/entry.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/foo/tsconfig.json: WARNING: Cannot find base config file "extends for foo"
)",
    });
}

TEST(BundlerTSConfig, TsconfigRemoveUnusedImports) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import {x, y} from "./foo"
                console.log(1 as x)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "importsNotUsedAsValues": "remove"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPreserveUnusedImports) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import {x, y} from "./foo"
                console.log(1 as x)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "importsNotUsedAsValues": "preserve"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PostResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"/Users/user/project/src/foo", true}},
                },
            },
        },
    });
}

TEST(BundlerTSConfig, TsconfigImportsNotUsedAsValuesPreserve) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import {x, y} from "./foo"
                import z from "./foo"
                import * as ns from "./foo"
                console.log(1 as x, 2 as z, 3 as ns.y)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "importsNotUsedAsValues": "preserve"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PostResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"/Users/user/project/src/foo", true}},
                },
            },
        },
    });
}

TEST(BundlerTSConfig, TsconfigPreserveValueImports) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import {} from "a"
                import {b1} from "b"
                import {c1, type c2} from "c"
                import {d1, d2, type d3} from "d"
                import {type e1, type e2} from "e"
                import f1, {} from "f"
                import g1, {g2} from "g"
                import h1, {type h2} from "h"
                import * as i1 from "i"
                import "j"
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "preserveValueImports": true
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PostResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"/Users/user/project/src/foo", true}},
                },
            },
        },
    });
}

TEST(BundlerTSConfig, TsconfigPreserveValueImportsAndImportsNotUsedAsValuesPreserve) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import {} from "a"
                import {b1} from "b"
                import {c1, type c2} from "c"
                import {d1, d2, type d3} from "d"
                import {type e1, type e2} from "e"
                import f1, {} from "f"
                import g1, {g2} from "g"
                import h1, {type h2} from "h"
                import * as i1 from "i"
                import "j"
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "importsNotUsedAsValues": "preserve",
                    "preserveValueImports": true
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PostResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"/Users/user/project/src/foo", true}},
                },
            },
        },
    });
}

TEST(BundlerTSConfig, TsconfigUseDefineForClassFieldsES2020) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                Foo = class {
                    useDefine = false
                }
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "target": "ES2020"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .OriginalTargetEnv = "esnext",

            .BuildMode = guchho::config::Mode::kBundle,
            
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigUseDefineForClassFieldsESNext) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                Foo = class {
                    useDefine = true
                }
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "target": "ESNext"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .OriginalTargetEnv = "esnext",

            .BuildMode = guchho::config::Mode::kBundle,

            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigUnrecognizedTargetWarning) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import "./a"
                import "b"
            )"},
            {"/Users/user/project/src/a/index.ts", R"()"},
            {"/Users/user/project/src/a/tsconfig.json", R"({
                "compilerOptions": {
                    "target": "es4"
                }
            })"},
            {"/Users/user/project/src/node_modules/b/index.ts", R"()"},
            {"/Users/user/project/src/node_modules/b/tsconfig.json", R"({
                "compilerOptions": {
                    "target": "es4"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/a/tsconfig.json: WARNING: Unrecognized target environment "es4"
)",
    });
}

TEST(BundlerTSConfig, TsconfigIgnoredTargetSilent) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import "./a"
                import "b"
            )"},
            {"/Users/user/project/src/a/index.ts", R"()"},
            {"/Users/user/project/src/a/tsconfig.json", R"({
                "compilerOptions": {
                    "target": "es5"
                }
            })"},
            {"/Users/user/project/src/node_modules/b/index.ts", R"()"},
            {"/Users/user/project/src/node_modules/b/tsconfig.json", R"({
                "compilerOptions": {
                    "target": "es5"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .OriginalTargetEnv = "ES5",

            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {5}}}}),
        },
    });
}

TEST(BundlerTSConfig, TsconfigNoBaseURLExtendsPaths) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import { foo } from "foo"
                console.log(foo)
            )"},
            {"/Users/user/project/lib/foo.ts", R"(
                export let foo = 123
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": "./base/defaults"
            })"},
            {"/Users/user/project/base/defaults.json", R"({
                "compilerOptions": {
                    "paths": {
                        "*": ["lib/*"]
                    }
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/base/defaults.json: WARNING: Non-relative path "lib/*" is not allowed when "baseUrl" is not set (did you forget a leading "./"?)
Users/user/project/src/entry.ts: ERROR: Could not resolve "foo"
NOTE: You can mark the path "foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)",
    });
}

TEST(BundlerTSConfig, TsconfigBaseURLExtendsPaths) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import { foo } from "foo"
                console.log(foo)
            )"},
            {"/Users/user/project/lib/foo.ts", R"(
                export let foo = 123
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": "./base/defaults",
                "compilerOptions": {
                    "baseUrl": "."
                }
            })"},
            {"/Users/user/project/base/defaults.json", R"({
                "compilerOptions": {
                    "paths": {
                        "*": ["lib/*"]
                    }
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPathsExtendsBaseURL) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import { foo } from "foo"
                console.log(foo)
            )"},
            {"/Users/user/project/base/test/lib/foo.ts", R"(
                export let foo = 123
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": "./base/defaults",
                "compilerOptions": {
                    "paths": {
                        "*": ["lib/*"]
                    }
                }
            })"},
            {"/Users/user/project/base/defaults.json", R"({
                "compilerOptions": {
                    "baseUrl": "test"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigPathsInNodeModulesIssue2386) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/main.js", R"(
                import first from "wow/first";
                import next from "wow/next";
                console.log(first, next);
            )"},
            {"/Users/user/project/node_modules/wow/package.json", R"({
                "name": "wow",
                "type": "module",
                "private": true,
                "exports": {
                    "./*": "./dist/*.js"
                },
                "typesVersions": {
                    "*": {
                        "*": [
                            "dist/*"
                        ]
                    }
                }
            })"},
            {"/Users/user/project/node_modules/wow/tsconfig.json", R"({
                "compilerOptions": {
                    "paths": { "wow/*": [ "./*" ] }
                }
            })"},
            {"/Users/user/project/node_modules/wow/dist/first.js", R"(
                export default "dist";
            )"},
            {"/Users/user/project/node_modules/wow/dist/next.js", R"(
                import next from "wow/first";
                export default next;
            )"},
            {"/Users/user/project/node_modules/wow/first.ts", R"(
                export default "source";
            )"},
        },
        .entry_paths = {"/Users/user/project/main.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigWithStatementAlwaysStrictFalse) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                with (x) y
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "alwaysStrict": false
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,

            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigWithStatementAlwaysStrictTrue) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                with (x) y
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "alwaysStrict": true
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/entry.ts: ERROR: With statements cannot be used in strict mode
Users/user/project/tsconfig.json: NOTE: TypeScript's "alwaysStrict" setting was enabled here:
)",
    });
}

TEST(BundlerTSConfig, TsconfigWithStatementStrictFalse) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                with (x) y
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "strict": false
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,
            
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigWithStatementStrictTrue) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                with (x) y
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "strict": true
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/entry.ts: ERROR: With statements cannot be used in strict mode
Users/user/project/tsconfig.json: NOTE: TypeScript's "strict" setting was enabled here:
)",
    });
}

TEST(BundlerTSConfig, TsconfigWithStatementStrictFalseAlwaysStrictTrue) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                with (x) y
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "strict": false,
                    "alwaysStrict": true
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/entry.ts: ERROR: With statements cannot be used in strict mode
Users/user/project/tsconfig.json: NOTE: TypeScript's "alwaysStrict" setting was enabled here:
)",
    });
}

TEST(BundlerTSConfig, TsconfigWithStatementStrictTrueAlwaysStrictFalse) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                with (x) y
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "strict": true,
                    "alwaysStrict": false
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,

            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigAlwaysStrictTrueEmitDirectivePassThrough) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/implicit.ts", R"(
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/src/explicit.ts", R"(
                'use strict'
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "alwaysStrict": true
                }
            })"},
        },
        .entry_paths = {
            "/Users/user/project/src/implicit.ts",
            "/Users/user/project/src/explicit.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigAlwaysStrictTrueEmitDirectiveFormat) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/implicit.ts", R"(
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/src/explicit.ts", R"(
                'use strict'
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "alwaysStrict": true
                }
            })"},
        },
        .entry_paths = {
            "/Users/user/project/src/implicit.ts",
            "/Users/user/project/src/explicit.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kConvertFormat,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigAlwaysStrictTrueEmitDirectiveBundleIIFE) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/implicit.ts", R"(
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/src/explicit.ts", R"(
                'use strict'
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "alwaysStrict": true
                }
            })"},
        },
        .entry_paths = {
            "/Users/user/project/src/implicit.ts",
            "/Users/user/project/src/explicit.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kIIFE,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigAlwaysStrictTrueEmitDirectiveBundleCJS) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/implicit.ts", R"(
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/src/explicit.ts", R"(
                'use strict'
                console.log('this file should start with "use strict"')
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "alwaysStrict": true
                }
            })"},
        },
        .entry_paths = {
            "/Users/user/project/src/implicit.ts",
            "/Users/user/project/src/explicit.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kCommonJS,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigAlwaysStrictTrueEmitDirectiveBundleESM) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/implicit.ts", R"(
                console.log('this file should not start with "use strict"')
            )"},
            {"/Users/user/project/src/explicit.ts", R"(
                'use strict'
                console.log('this file should not start with "use strict"')
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "alwaysStrict": true
                }
            })"},
        },
        .entry_paths = {
            "/Users/user/project/src/implicit.ts",
            "/Users/user/project/src/explicit.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsDotWithoutSlash) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/foo.json", R"({
                "extends": "."
            })"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "jsxFactory": "success"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
            .TSConfigPath = "/Users/user/project/src/foo.json",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsDotDotWithoutSlash) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "extends": ".."
            })"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "jsxFactory": "success"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsDotWithSlash) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/foo.json", R"({
                "extends": "./"
            })"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "jsxFactory": "FAILURE"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
            
            .TSConfigPath = "/Users/user/project/src/foo.json",
            
        },
        .expected_scan_log = R"(Users/user/project/src/foo.json: WARNING: Cannot find base config file "./"
)",
    });
}

TEST(BundlerTSConfig, TsconfigExtendsDotDotWithSlash) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "extends": "../"
            })"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "jsxFactory": "FAILURE"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
        },
        .expected_scan_log = R"(Users/user/project/src/tsconfig.json: WARNING: Cannot find base config file "../"
)",
    });
}

TEST(BundlerTSConfig, TsconfigExtendsWithExports) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": "@whatever/tsconfig/a/b/c"
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/package.json", R"({
                "exports": {
                    "./a/b/c": "./foo.json"
                }
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/foo.json", R"({
                "compilerOptions": {
                    "jsxFactory": "success"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsWithExportsStar) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": "@whatever/tsconfig/a/b/c"
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/package.json", R"({
                "exports": {
                    "./*": "./tsconfig.*.json"
                }
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/tsconfig.a/b/c.json", R"({
                "compilerOptions": {
                    "jsxFactory": "success"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsWithExportsStarTrailing) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": "@whatever/tsconfig/a/b/c.json"
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/package.json", R"({
                "exports": {
                    "./*": "./tsconfig.*"
                }
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/tsconfig.a/b/c.json", R"({
                "compilerOptions": {
                    "jsxFactory": "success"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsWithExportsRequire) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                console.log(<div/>)
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": "@whatever/tsconfig/a/b/c.json"
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/package.json", R"({
                "exports": {
                    "./*": {
                        "import": "./import.json",
                        "require": "./require.json",
                        "default": "./default.json"
                    }
                }
            })"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/import.json", R"(FAILURE)"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/default.json", R"(FAILURE)"},
            {"/Users/user/project/node_modules/@whatever/tsconfig/require.json", R"({
                "compilerOptions": {
                    "jsxFactory": "success"
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigVerbatimModuleSyntaxTrue) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.ts", R"(
                export { Car } from "./car";
                import type * as car from "./car";
                import { type Car } from "./car";
                export { type Car } from "./car";
                import type { A } from "a";
                import { b, type c, type d } from "bcd";
                import { type xyz } from "xyz";
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "verbatimModuleSyntax": true
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigVerbatimModuleSyntaxFalse) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.ts", R"(
                export { Car } from "./car";
                import type * as car from "./car";
                import { type Car } from "./car";
                export { type Car } from "./car";
                import type { A } from "a";
                import { b, type c, type d } from "bcd";
                import { type xyz } from "xyz";
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "verbatimModuleSyntax": false
                }
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsArray) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                declare let h: any, frag: any
                console.log(<><div /></>)
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": [
                    "./a.json",
                    "./b.json",
                ],
            })"},
            {"/Users/user/project/a.json", R"({
                "compilerOptions": {
                    "jsxFactory": "h",
                    "jsxFragmentFactory": "FAILURE",
                },
            })"},
            {"/Users/user/project/b.json", R"({
                "compilerOptions": {
                    "jsxFragmentFactory": "frag",
                },
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigExtendsArrayNested) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.tsx", R"(
                import { foo } from 'foo'
                declare let b: any, bBase: any
                export class Foo {
                    render = () => <><div /></>
                }
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "extends": [
                    "./a.json",
                    "./b.json",
                ],
            })"},
            {"/Users/user/project/a.json", R"({
                "extends": "./a-base.json",
                "compilerOptions": {
                    "jsxFactory": "a",
                    "jsxFragmentFactory": "a",
                    "target": "ES2015",
                },
            })"},
            {"/Users/user/project/a-base.json", R"({
                "compilerOptions": {
                    "jsxFactory": "aBase",
                    "jsxFragmentFactory": "aBase",
                    "target": "ES2022",
                    "verbatimModuleSyntax": true,
                },
            })"},
            {"/Users/user/project/b.json", R"({
                "extends": "./b-base.json",
                "compilerOptions": {
                    "jsxFactory": "b",
                },
            })"},
            {"/Users/user/project/b-base.json", R"({
                "compilerOptions": {
                    "jsxFactory": "bBase",
                    "jsxFragmentFactory": "bBase",
                },
            })"},
        },
        .entry_paths = {"/Users/user/project/src/main.tsx"},
        .options = guchho::config::Options{
            .OriginalTargetEnv = "esnext",

            .BuildMode = guchho::config::Mode::kPassThrough,

            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigIgnoreInsideNodeModules) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/main.ts", R"(
                import { foo } from 'js-pkg'
                import { bar } from 'ts-pkg'
                import { foo as shimFoo, bar as shimBar } from 'pkg'
                if (foo !== 'foo') throw 'fail: foo'
                if (bar !== 'bar') throw 'fail: bar'
                if (shimFoo !== 'shimFoo') throw 'fail: shimFoo'
                if (shimBar !== 'shimBar') throw 'fail: shimBar'
            )"},
            {"/Users/user/project/shim.ts", R"(
                export let foo = 'shimFoo'
                export let bar = 'shimBar'
            )"},
            {"/Users/user/project/tsconfig.json", R"({
                "compilerOptions": {
                    "paths": {
                        "pkg": ["./shim"],
                    },
                },
            })"},
            {"/Users/user/project/node_modules/js-pkg/index.js", R"(
                import { foo as pkgFoo } from 'pkg'
                export let foo = pkgFoo
            )"},
            {"/Users/user/project/node_modules/ts-pkg/index.ts", R"(
                import { bar as pkgBar } from 'pkg'
                export let bar = pkgBar
            )"},
            {"/Users/user/project/node_modules/pkg/index.js", R"(
                export let foo = 'foo'
                export let bar = 'bar'
            )"},
        },
        .entry_paths = {"/Users/user/project/src/main.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonPackagesExternal) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.js", R"(
                import truePkg from 'pkg1'
                import falsePkg from 'internal/pkg2'
                truePkg()
                falsePkg()
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "internal/*": ["./stuff/*"]
                        }
                    }
                }
            )"},
            {"/Users/user/project/src/stuff/pkg2.js", R"(
                export default success
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
            .ExternalPackages = true,
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonTopLevelMistakeWarning) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                @foo
                class Foo {}
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "experimentalDecorators": true
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
        .expected_scan_log = R"(Users/user/project/src/tsconfig.json: WARNING: Expected the "experimentalDecorators" option to be nested inside a "compilerOptions" object
)",
    });
}

TEST(BundlerTSConfig, TsconfigJsonBaseUrlIssue3307) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": "./subdir"
                    }
                }
            )"},
            {"/Users/user/project/src/test.ts", R"(
                export const foo = "well, this is correct...";
            )"},
            {"/Users/user/project/src/subdir/test.ts", R"(
                export const foo = "WRONG";
            )"},
        },
        .entry_paths = {"test.ts"},
        .abs_working_dir = "/Users/user/project/src",
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonAsteriskNameCollisionIssue3354) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                import {foo} from "foo";
                foo();
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": ".",
                        "paths": {
                            "*": ["web/*"]
                        }
                    }
                }
            )"},
            {"/Users/user/project/src/web/foo.ts", R"(
                import {foo as barFoo} from 'bar/foo';
                export function foo() {
                    console.log('web/foo');
                    barFoo();
                }
            )"},
            {"/Users/user/project/src/web/bar/foo/foo.ts", R"(
                export function foo() {
                    console.log('bar/foo');
                }
            )"},
            {"/Users/user/project/src/web/bar/foo/index.ts", R"(
                export {foo} from './foo'
            )"},
        },
        .entry_paths = {"entry.ts"},
        .abs_working_dir = "/Users/user/project/src",
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonConfigDirBaseURL) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.js", R"(
                import "foo/bar"
            )"},
            {"/Users/user/project/lib/foo/bar", R"(
                console.log('works')
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "@scope/configs/tsconfig"
                }
            )"},
            {"/Users/user/project/node_modules/@scope/configs/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": "${configDir}../lib"
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonConfigDirPaths) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.js", R"(
                import "library/foo/bar"
            )"},
            {"/Users/user/project/lib/foo/bar", R"(
                console.log('works')
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "@scope/configs/tsconfig"
                }
            )"},
            {"/Users/user/project/node_modules/@scope/configs/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "paths": {
                            "library/*": ["${configDir}../lib/*"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonConfigDirBaseURLInheritedPaths) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.js", R"(
                import "library/foo/bar"
            )"},
            {"/Users/user/project/lib/foo/bar", R"(
                console.log('works')
            )"},
            {"/Users/user/project/src/tsconfig.json", R"(
                {
                    "extends": "@scope/configs/tsconfig"
                }
            )"},
            {"/Users/user/project/node_modules/@scope/configs/tsconfig.json", R"(
                {
                    "compilerOptions": {
                        "baseUrl": "${configDir}..",
                        "paths": {
                            "library/*": ["./lib/*"]
                        }
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/src/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerTSConfig, TsconfigJsonExtendsArrayIssue3898) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/index.tsx", R"(
                import { type SomeType } from 'MUST_KEEP'
                console.log(<>
                    <div/>
                </>)
            )"},
            {"/Users/user/project/tsconfig.json", R"(
                {
                    "extends": [
                        "./tsconfigs/a.json",
                        "./tsconfigs/b.json",
                    ]
                }
            )"},
            {"/Users/user/project/tsconfigs/base.json", R"(
                {
                    "compilerOptions": {
                        "verbatimModuleSyntax": true,
                    }
                }
            )"},
            {"/Users/user/project/tsconfigs/a.json", R"(
                {
                    "extends": "./base.json",
                    "compilerOptions": {
                        "jsxFactory": "SUCCESS",
                    }
                }
            )"},
            {"/Users/user/project/tsconfigs/b.json", R"(
                {
                    "extends": "./base.json",
                    "compilerOptions": {
                        "jsxFragmentFactory": "WORKS",
                    }
                }
            )"},
        },
        .entry_paths = {"/Users/user/project/index.tsx"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/Users/user/project/out.js",
            .JSX = guchho::config::JSXOptions{
                .SideEffects = true,
            },
        },
    });
}

TEST(BundlerTSConfig, TsconfigDecoratorsUseDefineForClassFieldsFalse) {
    tsconfig_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/src/entry.ts", R"(
                class Class {
                }
                class ClassMethod {
                    foo() {}
                }
                class ClassField {
                    foo = 123
                    bar
                }
                class ClassAccessor {
                    accessor foo = 123
                    accessor bar
                }
                new Class
                new ClassMethod
                new ClassField
                new ClassAccessor
            )"},
            {"/Users/user/project/src/entrywithdec.ts", R"(
                @dec class Class {
                }
                class ClassMethod {
                    @dec foo() {}
                }
                class ClassField {
                    @dec foo = 123
                    @dec bar
                }
                class ClassAccessor {
                    @dec accessor foo = 123
                    @dec accessor bar
                }
                new Class
                new ClassMethod
                new ClassField
                new ClassAccessor
            )"},
            {"/Users/user/project/src/tsconfig.json", R"({
                "compilerOptions": {
                    "useDefineForClassFields": false
                }
            })"},
        },
        .entry_paths = {
            "/Users/user/project/src/entry.ts",
            "/Users/user/project/src/entrywithdec.ts",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/Users/user/project/out",
        },
    });
}


#if defined(__GNUC__) && !defined(_MSC_VER)
#pragma GCC diagnostic pop
#endif
} // namespace bundler::test
