#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

static Suite yarnpnp_suite{"yarnpnp"};


TEST(BundlerYarnPnP, TsconfigPackageJsonExportsYarnPnP) {
    yarnpnp_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/packages/app/index.tsx",
                R"(
                    console.log(<div/>)
                )"},
            {"/Users/user/project/packages/app/tsconfig.json",
                R"(
                    {
                        "extends": "tsconfigs/config"
                    }
                )"},
            {"/Users/user/project/packages/tsconfigs/package.json",
                R"(
                    {
                        "exports": {
                            "./config": "./configs/tsconfig.json"
                        }
                    }
                )"},
            {"/Users/user/project/packages/tsconfigs/configs/tsconfig.json",
                R"(
                    {
                        "compilerOptions": {
                            "jsxFactory": "success"
                        }
                    }
                )"},
            {"/Users/user/project/.pnp.data.json",
                R"(
                    {
                        "packageRegistryData": [
                            [
                                "app",
                                [
                                    [
                                        "workspace:packages/app",
                                        {
                                            "packageLocation": "./packages/app/",
                                            "packageDependencies": [
                                                [
                                                    "tsconfigs",
                                                    "workspace:packages/tsconfigs"
                                                ]
                                            ],
                                            "linkType": "SOFT"
                                        }
                                    ]
                                ]
                            ],
                            [
                                "tsconfigs",
                                [
                                    [
                                        "workspace:packages/tsconfigs",
                                        {
                                            "packageLocation": "./packages/tsconfigs/",
                                            "packageDependencies": [],
                                            "linkType": "SOFT"
                                        }
                                    ]
                                ]
                            ]
                        ]
                    }
                )"},
        },
        .entry_paths    = {"/Users/user/project/packages/app/index.tsx"},
        .abs_working_dir = "/Users/user/project",
        .options = guchho::config::Options{
            .BuildMode     = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}


TEST(BundlerYarnPnP, TsconfigStackOverflowYarnPnP) {
    yarnpnp_suite.ExpectBundled(Bundled{
        .files = {
            {"/Users/user/project/entry.jsx",
                R"(
                    console.log(<div />)
                )"},
            {"/Users/user/project/tsconfig.json",
                R"(
                    {
                        "extends": "tsconfigs/config"
                    }
                )"},
            {"/Users/user/project/packages/tsconfigs/package.json",
                R"(
                    {
                        "exports": {
                            "./config": "./configs/tsconfig.json"
                        }
                    }
                )"},
            {"/Users/user/project/packages/tsconfigs/configs/tsconfig.json",
                R"(
                    {
                        "compilerOptions": {
                            "jsxFactory": "success"
                        }
                    }
                )"},
            {"/Users/user/project/.pnp.data.json",
                R"(
                    {
                        "packageRegistryData": [
                            [null, [
                                [null, {
                                    "packageLocation": "./",
                                    "packageDependencies": [
                                        ["tsconfigs", "virtual:some-path"]
                                    ],
                                    "linkType": "SOFT"
                                }]
                            ]],
                            ["tsconfigs", [
                                ["virtual:some-path", {
                                    "packageLocation": "./packages/tsconfigs/",
                                    "packageDependencies": [
                                        ["tsconfigs", "virtual:some-path"]
                                    ],
                                    "packagePeers": [],
                                    "linkType": "SOFT"
                                }],
                                ["workspace:packages/tsconfigs", {
                                    "packageLocation": "./packages/tsconfigs/",
                                    "packageDependencies": [
                                        ["tsconfigs", "workspace:packages/tsconfigs"]
                                    ],
                                    "linkType": "SOFT"
                                }]
                            ]]
                        ]
                    }
                )"},
        },
        .entry_paths    = {"/Users/user/project/entry.jsx"},
        .abs_working_dir = "/Users/user/project",
        .options = guchho::config::Options{
            .BuildMode     = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/Users/user/project/out.js",
        },
    });
}

TEST(BundlerYarnPnP, WindowsCrossVolumeReferenceYarnPnP) {
    yarnpnp_suite.ExpectBundledWindows(Bundled{
        .files = {
            {"D:\\project\\entry.jsx",
                R"(
                    import * as React from 'react'
                    console.log(<div />)
                )"},
            {"C:\\Users\\user\\AppData\\Local\\Yarn\\Berry\\cache\\react.zip\\node_modules\\react\\index.js",
                R"(
                    export function createElement() {}
                )"},
            {"D:\\project\\.pnp.data.json",
                R"(
                    {
                        "packageRegistryData": [
                            [null, [
                                [null, {
                                    "packageLocation": "./",
                                    "packageDependencies": [
                                        ["react", "npm:19.1.1"],
                                        ["project", "workspace:."]
                                    ],
                                    "linkType": "SOFT"
                                }]
                            ]],
                            ["react", [
                                ["npm:19.1.1", {
                                    "packageLocation": "../../C:/Users/user/AppData/Local/Yarn/Berry/cache/react.zip/node_modules/react/",
                                    "packageDependencies": [
                                        ["react", "npm:19.1.1"]
                                    ],
                                    "linkType": "HARD"
                                }]
                            ]],
                            ["project", [
                                ["workspace:.", {
                                    "packageLocation": "./",
                                    "packageDependencies": [
                                        ["react", "npm:19.1.1"],
                                        ["project", "workspace:."]
                                    ],
                                    "linkType": "SOFT"
                                }]
                            ]]
                        ]
                    }
                )"},
        },
        .entry_paths    = {"D:\\project\\entry.jsx"},
        .abs_working_dir = "D:\\project",
        .options = guchho::config::Options{
            .BuildMode     = guchho::config::Mode::kBundle,
            .AbsOutputFile = "D:\\project\\out.js",
        },
    });
}

} // namespace bundler::test
