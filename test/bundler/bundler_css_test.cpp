#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"


namespace bundler::test {

Suite css_suite{"css"};

TEST(BundlerCSS, CSSEntryPoint) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                body {
                    background: white;
                    color: black }
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
        },
    });
}

TEST(BundlerCSS, CSSAtImportMissing) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "./missing.css";
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
        },
        .expected_scan_log = R"(entry.css: ERROR: Could not resolve "./missing.css"
)",
    });
}

TEST(BundlerCSS, CSSAtImportExternal) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "./internal.css";
                @import "./external1.css";
                @import "./external2.css";
                @import "./charset1.css";
                @import "./charset2.css";
                @import "./external5.css" screen;
            )"},
            {"/internal.css", R"(
                @import "./external5.css" print;
                .before { color: red }
            )"},
            {"/charset1.css", R"(
                @charset "UTF-8";
                @import "./external3.css";
                @import "./external4.css";
                @import "./external5.css";
                @import "https://www.example.com/style1.css";
                @import "https://www.example.com/style2.css";
                @import "https://www.example.com/style3.css" print;
                .middle { color: green }
            )"},
            {"/charset2.css", R"(
                @charset "UTF-8";
                @import "./external3.css";
                @import "./external5.css" screen;
                @import "https://www.example.com/style1.css";
                @import "https://www.example.com/style3.css";
                .after { color: blue }
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PostResolve = guchho::config::ExternalMatchers{
                    .Exact = {
                        {"/external1.css", true},
                        {"/external2.css", true},
                        {"/external3.css", true},
                        {"/external4.css", true},
                        {"/external5.css", true},
                    },
                },
            },
        },
    });
}

TEST(BundlerCSS, CSSAtImport) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "./a.css";
                @import "./b.css";
                .entry { color: red }
            )"},
            {"/a.css", R"(
                @import "./shared.css";
                .a { color: green }
            )"},
            {"/b.css", R"(
                @import "./shared.css";
                .b { color: blue }
            )"},
            {"/shared.css", R"(
                .shared { color: black }
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
        },
    });
}

TEST(BundlerCSS, CSSFromJSMissingImport) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import {missing} from "./a.css"
                console.log(missing)
            )"},
            {"/a.css", R"(
                .a { color: red }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_compile_log = R"(entry.js: ERROR: No matching export in "a.css" for import "missing"
)",
    });
}

TEST(BundlerCSS, CSSFromJSMissingStarImport) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import * as ns from "./a.css"
                console.log(ns.missing)
            )"},
            {"/a.css", R"(
                .a { color: red }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_compile_log = R"(entry.js: WARNING: Import "missing" will always be undefined because there is no matching export in "a.css"
)",
    });
}

TEST(BundlerCSS, ImportGlobalCSSFromJS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import "./a.js"
                import "./b.js"
            )"},
            {"/a.js", R"(
                import * as stylesA from "./a.css"
                console.log('a', stylesA.a, stylesA.default.a)
            )"},
            {"/a.css", R"(
                .a { color: red }
            )"},
            {"/b.js", R"(
                import * as stylesB from "./b.css"
                console.log('b', stylesB.b, stylesB.default.b)
            )"},
            {"/b.css", R"(
                .b { color: blue }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_compile_log = R"(a.js: WARNING: Import "a" will always be undefined because there is no matching export in "a.css"
b.js: WARNING: Import "b" will always be undefined because there is no matching export in "b.css"
)",
    });
}

TEST(BundlerCSS, ImportLocalCSSFromJS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import "./a.js"
                import "./b.js"
            )"},
            {"/a.js", R"(
                import * as stylesA from "./dir1/style.css"
                console.log('file 1', stylesA.button, stylesA.default.a)
            )"},
            {"/dir1/style.css", R"(
                .a { color: red }
                .button { display: none }
            )"},
            {"/b.js", R"(
                import * as stylesB from "./dir2/style.css"
                console.log('file 2', stylesB.button, stylesB.default.b)
            )"},
            {"/dir2/style.css", R"(
                .b { color: blue }
                .button { display: none }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
        },
    });
}

TEST(BundlerCSS, ImportLocalCSSFromJSMinifyIdentifiers) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import "./a.js"
                import "./b.js"
            )"},
            {"/a.js", R"(
                import * as stylesA from "./dir1/style.css"
                console.log('file 1', stylesA.button, stylesA.default.a)
            )"},
            {"/dir1/style.css", R"(
                .a { color: red }
                .button { display: none }
            )"},
            {"/b.js", R"(
                import * as stylesB from "./dir2/style.css"
                console.log('file 2', stylesB.button, stylesB.default.b)
            )"},
            {"/dir2/style.css", R"(
                .b { color: blue }
                .button { display: none }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
            .MinifyIdentifiers = true,
        },
    });
}

TEST(BundlerCSS, ImportLocalCSSFromJSMinifyIdentifiersAvoidGlobalNames) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import "./global.css"
                import "./local.module.css"
            )"},
            {"/global.css", R"(
                :is(.a, .b, .c, .d, .e, .f, .g, .h, .i, .j, .k, .l, .m, .n, .o, .p, .q, .r, .s, .t, .u, .v, .w, .x, .y, .z),
                :is(.A, .B, .C, .D, .E, .F, .G, .H, .I, .J, .K, .L, .M, .N, .O, .P, .Q, .R, .S, .T, .U, .V, .W, .X, .Y, .Z),
                ._ { color: red }
            )"},
            {"/local.module.css", R"(
                .rename-this { color: blue }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kCSS},
                {".module.css", guchho::config::Loader::kLocalCSS},
            },
            .MinifyIdentifiers = true,
        },
    });
}

TEST(BundlerCSS, ImportLocalCSSFromJSMinifyIdentifiersMultipleEntryPoints) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import { foo, bar } from "./a.module.css";
                console.log(foo, bar);
            )"},
            {"/a.module.css", R"(
                .foo { color: #001; }
                .bar { color: #002; }
            )"},
            {"/b.js", R"(
                import { foo, bar } from "./b.module.css";
                console.log(foo, bar);
            )"},
            {"/b.module.css", R"(
                .foo { color: #003; }
                .bar { color: #004; }
            )"},
        },
        .entry_paths = {"/a.js", "/b.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .MinifyIdentifiers = true,
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSLocalVsGlobal) {
    static const char* css = R"(
        .top_level { color: #000 }

        :global(.GLOBAL) { color: #001 }
        :local(.local) { color: #002 }

        div:global(.GLOBAL) { color: #003 }
        div:local(.local) { color: #004 }

        .top_level:global(div) { color: #005 }
        .top_level:local(div) { color: #006 }

        :global(div.GLOBAL) { color: #007 }
        :local(div.local) { color: #008 }

        div:global(span.GLOBAL) { color: #009 }
        div:local(span.local) { color: #00A }

        div:global(#GLOBAL_A.GLOBAL_B.GLOBAL_C):local(.local_a.local_b#local_c) { color: #00B }
        div:global(#GLOBAL_A .GLOBAL_B .GLOBAL_C):local(.local_a .local_b #local_c) { color: #00C }

        .nested {
            :global(&.GLOBAL) { color: #00D }
            :local(&.local) { color: #00E }

            &:global(.GLOBAL) { color: #00F }
            &:local(.local) { color: #010 }
        }

        :global(.GLOBAL_A .GLOBAL_B) { color: #011 }
        :local(.local_a .local_b) { color: #012 }

        div:global(.GLOBAL_A .GLOBAL_B):hover { color: #013 }
        div:local(.local_a .local_b):hover { color: #014 }

        div :global(.GLOBAL_A .GLOBAL_B) span { color: #015 }
        div :local(.local_a .local_b) span { color: #016 }

        div > :global(.GLOBAL_A ~ .GLOBAL_B) + span { color: #017 }
        div > :local(.local_a ~ .local_b) + span { color: #018 }

        div:global(+ .GLOBAL_A):hover { color: #019 }
        div:local(+ .local_a):hover { color: #01A }

        :global.GLOBAL:local.local { color: #01B }
        :global .GLOBAL :local .local { color: #01C }

        :global {
            .GLOBAL {
                before: outer;
                :local {
                    before: inner;
                    .local {
                        color: #01D;
                    }
                    after: inner;
                }
                after: outer;
            }
        }
    )";

    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import normalStyles from "./normal.css"
                import globalStyles from "./LOCAL.global-css"
                import localStyles from "./LOCAL.local-css"

                console.log('should be empty:', normalStyles)
                console.log('fewer local names:', globalStyles)
                console.log('more local names:', localStyles)
            )"},
            {"/normal.css", css},
            {"/LOCAL.global-css", css},
            {"/LOCAL.local-css", css},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kCSS},
                {".global-css", guchho::config::Loader::kGlobalCSS},
                {".local-css", guchho::config::Loader::kLocalCSS},
            },
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSLowerBareLocalAndGlobal) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                .before { color: #000 }
                :local { .button { color: #000 } }
                .after { color: #000 }

                .before { color: #001 }
                :global { .button { color: #001 } }
                .after { color: #001 }

                div { :local { .button { color: #002 } } }
                div { :global { .button { color: #003 } } }

                :local(:global) { color: #004 }
                :global(:local) { color: #005 }

                :local(:global) { .button { color: #006 } }
                :global(:local) { .button { color: #007 } }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
            .UnsupportedCSSFeatures = guchho::compat::CSSFeature::kNesting,
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSLocalAtKeyframes) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                @keyframes local_name { to { color: red } }

                div :global { animation-name: none }
                div :local { animation-name: none }

                div :global { animation-name: global_name }
                div :local { animation-name: local_name }

                div :global { animation-name: global_name1, none, global_name2, Inherit, INITIAL, revert, revert-layer, unset }
                div :local { animation-name: local_name1, none, local_name2, Inherit, INITIAL, revert, revert-layer, unset }

                div :global { animation: 2s infinite global_name }
                div :local { animation: 2s infinite local_name }

                /* Someone wanted to be able to name their animations "none" */
                @keyframes "none" { to { color: red } }
                div :global { animation-name: "none" }
                div :local { animation-name: "none" }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
            .UnsupportedCSSFeatures = guchho::compat::CSSFeature::kNesting,
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSLocalAtCounterStyle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import list_style_type from "./list_style_type.css"
                import list_style from "./list_style.css"
                console.log(list_style_type, list_style)
            )"},
            {"/list_style_type.css", R"(
                @counter-style local { symbols: A B C }

                div :global { list-style-type: GLOBAL }
                div :local { list-style-type: local }

                /* Must not accept invalid type values */
                div :local { list-style-type: none }
                div :local { list-style-type: INITIAL }
                div :local { list-style-type: decimal }
                div :local { list-style-type: disc }
                div :local { list-style-type: SQUARE }
                div :local { list-style-type: circle }
                div :local { list-style-type: disclosure-OPEN }
                div :local { list-style-type: DISCLOSURE-closed }
                div :local { list-style-type: LAO }
                div :local { list-style-type: "\1F44D" }
            )"},

            {"/list_style.css", R"(
                @counter-style local { symbols: A B C }

                div :global { list-style: GLOBAL }
                div :local { list-style: local }

                /* The first one is the type */
                div :local { list-style: local none }
                div :local { list-style: local url(http://) }
                div :local { list-style: local linear-gradient(red, green) }
                div :local { list-style: local inside }
                div :local { list-style: local outside }

                /* The second one is the type */
                div :local { list-style: none local }
                div :local { list-style: url(http://) local }
                div :local { list-style: linear-gradient(red, green) local }
                div :local { list-style: local inside }
                div :local { list-style: local outside }
                div :local { list-style: inside inside }
                div :local { list-style: inside outside }
                div :local { list-style: outside inside }
                div :local { list-style: outside outside }

                /* The type is set to "none" here */
                div :local { list-style: url(http://) none invalid }
                div :local { list-style: linear-gradient(red, green) none invalid }

                /* Must not accept invalid type values */
                div :local { list-style: INITIAL }
                div :local { list-style: decimal }
                div :local { list-style: disc }
                div :local { list-style: SQUARE }
                div :local { list-style: circle }
                div :local { list-style: disclosure-OPEN }
                div :local { list-style: DISCLOSURE-closed }
                div :local { list-style: LAO }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
            .UnsupportedCSSFeatures = guchho::compat::CSSFeature::kNesting,
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSLocalAtContainer) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                @container not (max-width: 100px) { div { color: red } }
                @container local (max-width: 100px) { div { color: red } }
                @container local not (max-width: 100px) { div { color: red } }
                @container local (max-width: 100px) or (min-height: 100px) { div { color: red } }
                @container local (max-width: 100px) and (min-height: 100px) { div { color: red } }
                @container general_enclosed(max-width: 100px) { div { color: red } }
                @container local general_enclosed(max-width: 100px) { div { color: red } }

                div :global { container-name: NONE initial }
                div :local { container-name: none INITIAL }
                div :global { container-name: GLOBAL1 GLOBAL2 }
                div :local { container-name: local1 local2 }

                div :global { container: none }
                div :local { container: NONE }
                div :global { container: NONE / size }
                div :local { container: none / size }

                div :global { container: GLOBAL1 GLOBAL2 }
                div :local { container: local1 local2 }
                div :global { container: GLOBAL1 GLOBAL2 / size }
                div :local { container: local1 local2 / size }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
            .UnsupportedCSSFeatures = guchho::compat::CSSFeature::kNesting,
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSNthIndexLocal) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                :nth-child(2n of .local) { color: #000 }
                :nth-child(2n of :local(#local), :global(.GLOBAL)) { color: #001 }
                :nth-child(2n of .local1 :global .GLOBAL1, .GLOBAL2 :local .local2) { color: #002 }
                .local1, :nth-child(2n of :global .GLOBAL), .local2 { color: #003 }

                :nth-last-child(2n of .local) { color: #000 }
                :nth-last-child(2n of :local(#local), :global(.GLOBAL)) { color: #001 }
                :nth-last-child(2n of .local1 :global .GLOBAL1, .GLOBAL2 :local .local2) { color: #002 }
                .local1, :nth-last-child(2n of :global .GLOBAL), .local2 { color: #003 }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
            .UnsupportedCSSFeatures = guchho::compat::CSSFeature::kNesting,
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSComposes) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.module.css"
                console.log(styles)
            )"},
            {"/global.css", R"(
                .GLOBAL1 {
                    color: black;
                }
            )"},
            {"/styles.module.css", R"(
                @import "global.css";
                .local0 {
                    composes: local1;
                    :global {
                        composes: GLOBAL1 GLOBAL2;
                    }
                }
                .local0 {
                    composes: GLOBAL2 GLOBAL3 from global;
                    composes: local1 local2;
                    background: green;
                }
                .local0 :global {
                    composes: GLOBAL4;
                }
                .local3 {
                    border: 1px solid black;
                    composes: local4;
                }
                .local4 {
                    opacity: 0.5;
                }
                .local1 {
                    color: red;
                    composes: local3;
                }
                .fromOtherFile {
                    composes: local0 from "other1.module.css";
                    composes: local0 from "other2.module.css";
                }
            )"},
            {"/other1.module.css", R"(
                .local0 {
                    composes: base1 base2 from "base.module.css";
                    color: blue;
                }
            )"},
            {"/other2.module.css", R"(
                .local0 {
                    composes: base1 base3 from "base.module.css";
                    background: purple;
                }
            )"},
            {"/base.module.css", R"(
                .base1 {
                    cursor: pointer;
                }
                .base2 {
                    display: inline;
                }
                .base3 {
                    float: left;
                }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kCSS},
                {".module.css", guchho::config::Loader::kLocalCSS},
            },
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSComposesFromMissingImport) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.module.css"
                console.log(styles)
            )"},
            {"/styles.module.css", R"(
                .foo {
                    composes: x from "file.module.css";
                    composes: y from "file.module.css";
                    composes: z from "file.module.css";
                    composes: x from "file.css";
                }
            )"},
            {"/file.module.css", R"(
                .x {
                    color: red;
                }
                :global(.y) {
                    color: blue;
                }
            )"},
            {"/file.css", R"(
                .x {
                    color: red;
                }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".module.css", guchho::config::Loader::kLocalCSS},
                {".css", guchho::config::Loader::kCSS},
            },
        },
        .expected_compile_log = R"(styles.module.css: ERROR: Cannot use global name "y" with "composes"
file.module.css: NOTE: The global name "y" is defined here:
NOTE: Use the ":local" selector to change "y" into a local name.
styles.module.css: ERROR: The name "z" never appears in "file.module.css"
styles.module.css: ERROR: Cannot use global name "x" with "composes"
file.css: NOTE: The global name "x" is defined here:
NOTE: Use the "local-css" loader for "file.css" to enable local names.
)",
    });
}

TEST(BundlerCSS, ImportCSSFromJSComposesFromNotCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                .foo {
                    composes: bar from "file.txt";
                }
            )"},
            {"/file.txt", R"(
                .bar {
                    color: red;
                }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
                {".txt", guchho::config::Loader::kText},
            },
        },
        .expected_scan_log = R"(styles.css: ERROR: Cannot use "composes" with "file.txt"
NOTE: You can only use "composes" with CSS files and "file.txt" is not a CSS file (it was loaded with the "text" loader).
)",
    });
}

TEST(BundlerCSS, ImportCSSFromJSComposesCircular) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                .foo {
                    composes: bar;
                }
                .bar {
                    composes: foo;
                }
                .baz {
                    composes: baz;
                }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSComposesFromCircular) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                .foo {
                    composes: bar from "other.css";
                }
                .bar {
                    composes: bar from "styles.css";
                }
            )"},
            {"/other.css", R"(
                .bar {
                    composes: foo from "styles.css";
                }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
        },
    });
}

TEST(BundlerCSS, ImportCSSFromJSComposesFromUndefined) {
    std::string note =
        "NOTE: The specification of \"composes\" does not define an order when class declarations from separate files are composed together. "
        "The value of the \"zoom\" property for \"foo\" may change unpredictably as the code is edited. "
        "Make sure that all definitions of \"zoom\" for \"foo\" are in a single file.";
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import styles from "./styles.css"
                console.log(styles)
            )"},
            {"/styles.css", R"(
                @import "well-defined.css";
                @import "undefined/case1.css";
                @import "undefined/case2.css";
                @import "undefined/case3.css";
                @import "undefined/case4.css";
                @import "undefined/case5.css";
            )"},
            {"/well-defined.css", R"(
                .z1 { composes: z2; zoom: 1; }
                .z2 { zoom: 2; }

                .z4 { zoom: 4; }
                .z3 { composes: z4; zoom: 3; }

                .z5 { composes: foo bar from "file-1.css"; }
            )"},
            {"/undefined/case1.css", R"(
                .foo {
                    composes: foo from "../file-1.css";
                    zoom: 2;
                }
            )"},
            {"/undefined/case2.css", R"(
                .foo {
                    composes: foo from "../file-1.css";
                    composes: foo from "../file-2.css";
                }
            )"},
            {"/undefined/case3.css", R"(
                .foo { composes: nested1 nested2; }
                .nested1 { zoom: 3; }
                .nested2 { composes: foo from "../file-2.css"; }
            )"},
            {"/undefined/case4.css", R"(
                .foo { composes: nested1 nested2; }
                .nested1 { composes: foo from "../file-1.css"; }
                .nested2 { zoom: 3; }
            )"},
            {"/undefined/case5.css", R"(
                .foo { composes: nested1 nested2; }
                .nested1 { composes: foo from "../file-1.css"; }
                .nested2 { composes: foo from "../file-2.css"; }
            )"},
            {"/file-1.css", R"(
                .foo { zoom: 1; }
                .bar { zoom: 2; }
            )"},
            {"/file-2.css", R"(
                .foo { zoom: 2; }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kLocalCSS},
            },
        },
        .expected_compile_log =
            "undefined/case1.css: WARNING: The value of \"zoom\" in the \"foo\" class is undefined\n"
            "file-1.css: NOTE: The first definition of \"zoom\" is here:\n"
            "undefined/case1.css: NOTE: The second definition of \"zoom\" is here:\n"
            + note + "\n"
            "undefined/case2.css: WARNING: The value of \"zoom\" in the \"foo\" class is undefined\n"
            "file-1.css: NOTE: The first definition of \"zoom\" is here:\n"
            "file-2.css: NOTE: The second definition of \"zoom\" is here:\n"
            + note + "\n"
            "undefined/case3.css: WARNING: The value of \"zoom\" in the \"foo\" class is undefined\n"
            "undefined/case3.css: NOTE: The first definition of \"zoom\" is here:\n"
            "file-2.css: NOTE: The second definition of \"zoom\" is here:\n"
            + note + "\n"
            "undefined/case4.css: WARNING: The value of \"zoom\" in the \"foo\" class is undefined\n"
            "file-1.css: NOTE: The first definition of \"zoom\" is here:\n"
            "undefined/case4.css: NOTE: The second definition of \"zoom\" is here:\n"
            + note + "\n"
            "undefined/case5.css: WARNING: The value of \"zoom\" in the \"foo\" class is undefined\n"
            "file-1.css: NOTE: The first definition of \"zoom\" is here:\n"
            "file-2.css: NOTE: The second definition of \"zoom\" is here:\n"
            + note + "\n",
    });
}

TEST(BundlerCSS, ImportCSSFromJSWriteToStdout) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import "./entry.css"
            )"},
            {"/entry.css", R"(
                .entry { color: red }
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .WriteToStdout = true,
        },
        .expected_scan_log = R"(entry.js: ERROR: Cannot import "entry.css" into a JavaScript file without an output path configured
)",
    });
}

TEST(BundlerCSS, ImportJSFromCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                export default 123
            )"},
            {"/entry.css", R"(
                @import "./entry.js";
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_scan_log = R"(entry.css: ERROR: Cannot import "entry.js" into a CSS file
NOTE: An "@import" rule can only be used to import another CSS file and "entry.js" is not a CSS file (it was loaded with the "js" loader).
)",
    });
}

TEST(BundlerCSS, ImportJSONFromCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.json", R"(
                {}
            )"},
            {"/entry.css", R"(
                @import "./entry.json";
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_scan_log = R"(entry.css: ERROR: Cannot import "entry.json" into a CSS file
NOTE: An "@import" rule can only be used to import another CSS file and "entry.json" is not a CSS file (it was loaded with the "json" loader).
)",
    });
}

TEST(BundlerCSS, MissingImportURLInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/src/entry.css", R"(
                a { background: url(./one.png); }
                b { background: url("./two.png"); }
            )"},
        },
        .entry_paths = {"/src/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_scan_log = R"(src/entry.css: ERROR: Could not resolve "./one.png"
src/entry.css: ERROR: Could not resolve "./two.png"
)",
    });
}

TEST(BundlerCSS, ExternalImportURLInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/src/entry.css", R"(
                div:after {
                    content: 'If this is recognized, the path should become "../src/external.png"';
                    background: url(./external.png);
                }

                /* These URLs should be external automatically */
                a { background: url(http://example.com/images/image.png) }
                b { background: url(https://example.com/images/image.png) }
                c { background: url(//example.com/images/image.png) }
                d { background: url(data:image/png;base64,iVBORw0KGgo=) }
                path { fill: url(#filter) }
            )"},
        },
        .entry_paths = {"/src/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PostResolve = guchho::config::ExternalMatchers{
                    .Exact = {
                        {"/src/external.png", true},
                    },
                },
            },
        },
    });
}

TEST(BundlerCSS, InvalidImportURLInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a {
                    background: url(./js.js);
                    background: url("./jsx.jsx");
                    background: url(./ts.ts);
                    background: url('./tsx.tsx');
                    background: url(./json.json);
                    background: url(./css.css);
                }
            )"},
            {"/js.js", R"(export default 123)"},
            {"/jsx.jsx", R"(export default 123)"},
            {"/ts.ts", R"(export default 123)"},
            {"/tsx.tsx", R"(export default 123)"},
            {"/json.json", R"({ "test": true })"},
            {"/css.css", R"(a { color: red })"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_scan_log =
            "entry.css: ERROR: Cannot use \"js.js\" as a URL\n"
            "NOTE: You can't use a \"url()\" token to reference the file \"js.js\" because it was loaded with the \"js\" loader, which doesn't provide a URL to embed in the resulting CSS.\n"
            "entry.css: ERROR: Cannot use \"jsx.jsx\" as a URL\n"
            "NOTE: You can't use a \"url()\" token to reference the file \"jsx.jsx\" because it was loaded with the \"jsx\" loader, which doesn't provide a URL to embed in the resulting CSS.\n"
            "entry.css: ERROR: Cannot use \"ts.ts\" as a URL\n"
            "NOTE: You can't use a \"url()\" token to reference the file \"ts.ts\" because it was loaded with the \"ts\" loader, which doesn't provide a URL to embed in the resulting CSS.\n"
            "entry.css: ERROR: Cannot use \"tsx.tsx\" as a URL\n"
            "NOTE: You can't use a \"url()\" token to reference the file \"tsx.tsx\" because it was loaded with the \"tsx\" loader, which doesn't provide a URL to embed in the resulting CSS.\n"
            "entry.css: ERROR: Cannot use \"json.json\" as a URL\n"
            "NOTE: You can't use a \"url()\" token to reference the file \"json.json\" because it was loaded with the \"json\" loader, which doesn't provide a URL to embed in the resulting CSS.\n"
            "entry.css: ERROR: Cannot use \"css.css\" as a URL\n"
            "NOTE: You can't use a \"url()\" token to reference a CSS file, and \"css.css\" is a CSS file (it was loaded with the \"css\" loader).\n",
    });
}

TEST(BundlerCSS, TextImportURLInCSSText) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a {
                    background: url(./example.txt);
                }
            )"},
            {"/example.txt", "This is some text."},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, DataURLImportURLInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a {
                    background: url(./example.png);
                }
            )"},
            {"/example.png", "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".png", guchho::config::Loader::kDataURL},
            },
        },
    });
}

TEST(BundlerCSS, BinaryImportURLInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a {
                    background: url(./example.png);
                }
            )"},
            {"/example.png", "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".png", guchho::config::Loader::kBinary},
            },
        },
    });
}

TEST(BundlerCSS, Base64ImportURLInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a {
                    background: url(./example.png);
                }
            )"},
            {"/example.png", "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".png", guchho::config::Loader::kBase64},
            },
        },
    });
}

TEST(BundlerCSS, FileImportURLInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "./one.css";
                @import "./two.css";
            )"},
            {"/one.css", R"(
                a { background: url(./example.data) }
            )"},
            {"/two.css", R"(
                b { background: url(./example.data) }
            )"},
            {"/example.data", "This is some data."},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".data", guchho::config::Loader::kFile},
            },
        },
    });
}

TEST(BundlerCSS, IgnoreURLsInAtRulePrelude) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                /* This should not generate a path resolution error */
                @supports (background: url(ignored.png)) {
                    a { color: red }
                }
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, PackageURLsInCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "test.css";

                a { background: url(a/1.png); }
                b { background: url(b/2.png); }
                c { background: url(c/3.png); }
            )"},
            {"/test.css", R"(.css { color: red })"},
            {"/a/1.png", "a-1"},
            {"/node_modules/b/2.png", "b-2-node_modules"},
            {"/c/3.png", "c-3"},
            {"/node_modules/c/3.png", "c-3-node_modules"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".png", guchho::config::Loader::kBase64},
            },
        },
    });
}

TEST(BundlerCSS, CSSAtImportExtensionOrderCollision) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            // This should avoid picking ".js" because it's explicitly configured as non-CSS
            {"/entry.css", R"(@import "./test";)"},
            {"/test.js", R"(console.log('js'))"},
            {"/test.css", R"(.css { color: red })"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
            .ExtensionOrder = {".js", ".css"},
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kCSS},
            },
        },
    });
}

TEST(BundlerCSS, CSSAtImportExtensionOrderCollisionUnsupported) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            // This still shouldn't pick ".js" even though ".sass" isn't ".css"
            {"/entry.css", R"(@import "./test";)"},
            {"/test.js", R"(console.log('js'))"},
            {"/test.sass", R"(// some code)"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
            .ExtensionOrder = {".js", ".sass"},
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".css", guchho::config::Loader::kCSS},
            },
        },
        .expected_scan_log = R"(entry.css: ERROR: No loader is configured for ".sass" files: test.sass
)",
    });
}

TEST(BundlerCSS, CSSAtImportConditionsNoBundle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(@import "./print.css" print;)"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.css",
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsBundleExternal) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(@import "https://example.com/print.css" print;)"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsBundleExternalConditionWithURL) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "https://example.com/foo.css" (foo: url("foo.png")) and (bar: url("bar.png"));
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsBundle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import url(http://example.com/foo.css);
                @import url(http://example.com/foo.css) layer;
                @import url(http://example.com/foo.css) layer(layer-name);
                @import url(http://example.com/foo.css) layer(layer-name) supports(supports-condition);
                @import url(http://example.com/foo.css) layer(layer-name) supports(supports-condition) list-of-media-queries;
                @import url(http://example.com/foo.css) layer(layer-name) list-of-media-queries;
                @import url(http://example.com/foo.css) supports(supports-condition);
                @import url(http://example.com/foo.css) supports(supports-condition) list-of-media-queries;
                @import url(http://example.com/foo.css) list-of-media-queries;

                @import url(foo.css);
                @import url(foo.css) layer;
                @import url(foo.css) layer(layer-name);
                @import url(foo.css) layer(layer-name) supports(supports-condition);
                @import url(foo.css) layer(layer-name) supports(supports-condition) list-of-media-queries;
                @import url(foo.css) layer(layer-name) list-of-media-queries;
                @import url(foo.css) supports(supports-condition);
                @import url(foo.css) supports(supports-condition) list-of-media-queries;
                @import url(foo.css) list-of-media-queries;

                @import url(empty-1.css) layer(empty-1);
                @import url(empty-2.css) supports(empty: 2);
                @import url(empty-3.css) (empty: 3);

                @import "nested-layer.css" layer(outer);
                @import "nested-layer.css" supports(outer: true);
                @import "nested-layer.css" (outer: true);
                @import "nested-supports.css" layer(outer);
                @import "nested-supports.css" supports(outer: true);
                @import "nested-supports.css" (outer: true);
                @import "nested-media.css" layer(outer);
                @import "nested-media.css" supports(outer: true);
                @import "nested-media.css" (outer: true);
            )"},

            {"/foo.css", R"(body { color: red })"},

            {"/empty-1.css", ""},
            {"/empty-2.css", ""},
            {"/empty-3.css", ""},

            {"/nested-layer.css", R"(@import "foo.css" layer(inner);)"},
            {"/nested-supports.css", R"(@import "foo.css" supports(inner: true);)"},
            {"/nested-media.css", R"(@import "foo.css" (inner: true);)"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsWithImportRecordsBundle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import url(foo.css) supports(background: url(a.png));
                @import url(foo.css) supports(background: url(b.png)) list-of-media-queries;
                @import url(foo.css) layer(layer-name) supports(background: url(a.png));
                @import url(foo.css) layer(layer-name) supports(background: url(b.png)) list-of-media-queries;
            )"},
            {"/foo.css", R"(body { color: red })"},
            {"/a.png", "A"},
            {"/b.png", "B"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".png", guchho::config::Loader::kBase64},
            },
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsFromExternalRepo) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/001/default/a.css", R"(.box { background-color: green; })"},
            {"/001/default/style.css", R"(@import url("a.css");)"},

            {"/001/relative-url/a.css", R"(.box { background-color: green; })"},
            {"/001/relative-url/style.css", R"(@import url("./a.css");)"},

            {"/at-charset/001/a.css", R"(@charset "utf-8"; .box { background-color: red; })"},
            {"/at-charset/001/b.css", R"(@charset "utf-8"; .box { background-color: green; })"},
            {"/at-charset/001/style.css", R"(@charset "utf-8"; @import url("a.css"); @import url("b.css");)"},

            {"/at-keyframes/001/a.css", R"(
                .box { animation: BOX; animation-duration: 0s; animation-fill-mode: both; }
                @keyframes BOX { 0%, 100% { background-color: green; } }
            )"},
            {"/at-keyframes/001/b.css", R"(
                .box { animation: BOX; animation-duration: 0s; animation-fill-mode: both; }
                @keyframes BOX { 0%, 100% { background-color: red; } }
            )"},
            {"/at-keyframes/001/style.css", R"(@import url("a.css") screen; @import url("b.css") print;)"},

            {"/at-layer/001/a.css", R"(.box { background-color: red; })"},
            {"/at-layer/001/b.css", R"(.box { background-color: green; })"},
            {"/at-layer/001/style.css", R"(
                @import url("a.css") layer(a);
                @import url("b.css") layer(b);
                @import url("a.css") layer(a);
            )"},

            {"/at-layer/002/a.css", R"(.box { background-color: green; })"},
            {"/at-layer/002/b.css", R"(.box { background-color: red; })"},
            {"/at-layer/002/style.css", R"(
                @import url("a.css") layer(a) print;
                @import url("b.css") layer(b);
                @import url("a.css") layer(a);
            )"},

            // Note: This case is currently bundled incorrectly. Normal CSS takes
            // effect at the position of the last "@import". However, "@layer" CSS
            // takes effect at the position of the first "@import". This discrepancy
            // in behavior is not currently handled.
            {"/at-layer/003/a.css", R"(@layer a { .box { background-color: red; } })"},
            {"/at-layer/003/b.css", R"(@layer b { .box { background-color: green; } })"},
            {"/at-layer/003/style.css", R"(@import url("a.css"); @import url("b.css"); @import url("a.css");)"},

            {"/at-layer/004/a.css", R"(@layer { .box { background-color: green; } })"},
            {"/at-layer/004/b.css", R"(@layer { .box { background-color: red; } })"},
            {"/at-layer/004/style.css", R"(@import url("a.css"); @import url("b.css"); @import url("a.css");)"},

            {"/at-layer/005/a.css", R"(@import url("b.css") layer(b) (width: 1px);)"},
            {"/at-layer/005/b.css", R"(.box { background-color: red; })"},
            {"/at-layer/005/style.css", R"(
                @import url("a.css") layer(a) (min-width: 1px);
                @layer a.c { .box { background-color: red; } }
                @layer a.b { .box { background-color: green; } }
            )"},

            {"/at-layer/006/a.css", R"(@import url("b.css") layer(b) (min-width: 1px);)"},
            {"/at-layer/006/b.css", R"(.box { background-color: red; })"},
            {"/at-layer/006/style.css", R"(
                @import url("a.css") layer(a) (min-width: 1px);
                @layer a.c { .box { background-color: green; } }
                @layer a.b { .box { background-color: red; } }
            )"},

            {"/at-layer/007/style.css", R"(
                @layer foo {}
                @layer bar {}
                @layer bar { .box { background-color: green; } }
                @layer foo { .box { background-color: red; } }
            )"},

            {"/at-layer/008/a.css", R"(@import "b.css" layer; .box { background-color: green; })"},
            {"/at-layer/008/b.css", R"(.box { background-color: red; })"},
            {"/at-layer/008/style.css", R"(@import url("a.css") layer;)"},

            {"/at-media/001/default/a.css", R"(.box { background-color: green; })"},
            {"/at-media/001/default/style.css", R"(@import url("a.css") screen;)"},

            {"/at-media/002/a.css", R"(.box { background-color: green; })"},
            {"/at-media/002/b.css", R"(.box { background-color: red; })"},
            {"/at-media/002/style.css", R"(@import url("a.css") screen; @import url("b.css") print;)"},

            {"/at-media/003/a.css", R"(@import url("b.css") (min-width: 1px);)"},
            {"/at-media/003/b.css", R"(.box { background-color: green; })"},
            {"/at-media/003/style.css", R"(@import url("a.css") screen;)"},

            {"/at-media/004/a.css", R"(@import url("b.css") print;)"},
            {"/at-media/004/b.css", R"(.box { background-color: red; })"},
            {"/at-media/004/c.css", R"(.box { background-color: green; })"},
            {"/at-media/004/style.css", R"(@import url("c.css"); @import url("a.css") print;)"},

            {"/at-media/005/a.css", R"(@import url("b.css") (max-width: 1px);)"},
            {"/at-media/005/b.css", R"(.box { background-color: red; })"},
            {"/at-media/005/c.css", R"(.box { background-color: green; })"},
            {"/at-media/005/style.css", R"(@import url("c.css"); @import url("a.css") (max-width: 1px);)"},

            {"/at-media/006/a.css", R"(@import url("b.css") (min-width: 1px);)"},
            {"/at-media/006/b.css", R"(.box { background-color: green; })"},
            {"/at-media/006/style.css", R"(@import url("a.css") (min-height: 1px);)"},

            {"/at-media/007/a.css", R"(@import url("b.css") screen;)"},
            {"/at-media/007/b.css", R"(.box { background-color: green; })"},
            {"/at-media/007/style.css", R"(@import url("a.css") all;)"},

            {"/at-media/008/a.css", R"(@import url("green.css") layer(alpha) print;)"},
            {"/at-media/008/b.css", R"(@import url("red.css") layer(beta) print;)"},
            {"/at-media/008/green.css", R"(.box { background-color: green; })"},
            {"/at-media/008/red.css", R"(.box { background-color: red; })"},
            {"/at-media/008/style.css", R"(
                @import url("a.css") layer(alpha) all;
                @import url("b.css") layer(beta) all;
                @layer beta { .box { background-color: green; } }
                @layer alpha { .box { background-color: red; } }
            )"},

            {"/at-supports/001/a.css", R"(.box { background-color: green; })"},
            {"/at-supports/001/style.css", R"(@import url("a.css") supports(display: block);)"},

            {"/at-supports/002/a.css", R"(@import url("b.css") supports(width: 10px);)"},
            {"/at-supports/002/b.css", R"(.box { background-color: green; })"},
            {"/at-supports/002/style.css", R"(@import url("a.css") supports(display: block);)"},

            {"/at-supports/003/a.css", R"(@import url("b.css") supports(width: 10px);)"},
            {"/at-supports/003/b.css", R"(.box { background-color: green; })"},
            {"/at-supports/003/style.css", R"(@import url("a.css") supports((display: block) or (display: inline));)"},

            {"/at-supports/004/a.css", R"(@import url("b.css") layer(b) supports(width: 10px);)"},
            {"/at-supports/004/b.css", R"(.box { background-color: green; })"},
            {"/at-supports/004/style.css", R"(@import url("a.css") layer(a) supports(display: block);)"},

            {"/at-supports/005/a.css", R"(@import url("green.css") layer(alpha) supports(foo: bar);)"},
            {"/at-supports/005/b.css", R"(@import url("red.css") layer(beta) supports(foo: bar);)"},
            {"/at-supports/005/green.css", R"(.box { background-color: green; })"},
            {"/at-supports/005/red.css", R"(.box { background-color: red; })"},
            {"/at-supports/005/style.css", R"(
                @import url("a.css") layer(alpha) supports(display: block);
                @import url("b.css") layer(beta) supports(display: block);
                @layer beta { .box { background-color: green; } }
                @layer alpha { .box { background-color: red; } }
            )"},

            {"/cycles/001/style.css", R"(@import url("style.css"); .box { background-color: green; })"},

            {"/cycles/002/a.css", R"(@import url("red.css"); @import url("b.css");)"},
            {"/cycles/002/b.css", R"(@import url("green.css"); @import url("a.css");)"},
            {"/cycles/002/green.css", R"(.box { background-color: green; })"},
            {"/cycles/002/red.css", R"(.box { background-color: red; })"},
            {"/cycles/002/style.css", R"(@import url("a.css");)"},

            {"/cycles/003/a.css", R"(@import url("b.css"); .box { background-color: green; })"},
            {"/cycles/003/b.css", R"(@import url("a.css"); .box { background-color: red; })"},
            {"/cycles/003/style.css", R"(@import url("a.css");)"},

            {"/cycles/004/a.css", R"(@import url("b.css"); .box { background-color: red; })"},
            {"/cycles/004/b.css", R"(@import url("a.css"); .box { background-color: green; })"},
            {"/cycles/004/style.css", R"(@import url("a.css"); @import url("b.css");)"},

            {"/cycles/005/a.css", R"(@import url("b.css"); .box { background-color: green; })"},
            {"/cycles/005/b.css", R"(@import url("a.css"); .box { background-color: red; })"},
            {"/cycles/005/style.css", R"(@import url("a.css"); @import url("b.css"); @import url("a.css");)"},

            {"/cycles/006/a.css", R"(@import url("red.css"); @import url("b.css");)"},
            {"/cycles/006/b.css", R"(@import url("green.css"); @import url("a.css");)"},
            {"/cycles/006/c.css", R"(@import url("a.css");)"},
            {"/cycles/006/green.css", R"(.box { background-color: green; })"},
            {"/cycles/006/red.css", R"(.box { background-color: red; })"},
            {"/cycles/006/style.css", R"(@import url("b.css"); @import url("c.css");)"},

            {"/cycles/007/a.css", R"(@import url("red.css"); @import url("b.css") screen;)"},
            {"/cycles/007/b.css", R"(@import url("green.css"); @import url("a.css") all;)"},
            {"/cycles/007/c.css", R"(@import url("a.css") not print;)"},
            {"/cycles/007/green.css", R"(.box { background-color: green; })"},
            {"/cycles/007/red.css", R"(.box { background-color: red; })"},
            {"/cycles/007/style.css", R"(@import url("b.css"); @import url("c.css");)"},

            {"/cycles/008/a.css", R"(@import url("red.css") layer; @import url("b.css");)"},
            {"/cycles/008/b.css", R"(@import url("green.css") layer; @import url("a.css");)"},
            {"/cycles/008/c.css", R"(@import url("a.css") layer;)"},
            {"/cycles/008/green.css", R"(.box { background-color: green; })"},
            {"/cycles/008/red.css", R"(.box { background-color: red; })"},
            {"/cycles/008/style.css", R"(@import url("b.css"); @import url("c.css");)"},

            {"/data-urls/002/style.css", R"(@import url('data:text/css;plain,.box%20%7B%0A%09background-color%3A%20green%3B%0A%7D%0A');)"},

            {"/data-urls/003/style.css", R"(@import url('data:text/css,.box%20%7B%0A%09background-color%3A%20green%3B%0A%7D%0A');)"},

            {"/duplicates/001/a.css", R"(.box { background-color: green; })"},
            {"/duplicates/001/b.css", R"(.box { background-color: red; })"},
            {"/duplicates/001/style.css", R"(@import url("a.css"); @import url("b.css"); @import url("a.css");)"},

            {"/duplicates/002/a.css", R"(.box { background-color: green; })"},
            {"/duplicates/002/b.css", R"(.box { background-color: red; })"},
            {"/duplicates/002/style.css", R"(@import url("a.css"); @import url("b.css"); @import url("a.css"); @import url("b.css"); @import url("a.css");)"},

            {"/empty/001/empty.css", ""},
            {"/empty/001/style.css", R"(@import url("./empty.css"); .box { background-color: green; })"},

            {"/relative-paths/001/a/a.css", R"(@import url("../b/b.css"))"},
            {"/relative-paths/001/b/b.css", R"(.box { background-color: green; })"},
            {"/relative-paths/001/style.css", R"(@import url("./a/a.css");)"},

            {"/relative-paths/002/a/a.css", R"(@import url("./../b/b.css"))"},
            {"/relative-paths/002/b/b.css", R"(.box { background-color: green; })"},
            {"/relative-paths/002/style.css", R"(@import url("./a/a.css");)"},

            {"/subresource/001/something/images/green.png", "..."},
            {"/subresource/001/something/styles/green.css", R"(.box { background-image: url("../images/green.png"); })"},
            {"/subresource/001/style.css", R"(@import url("./something/styles/green.css");)"},

            {"/subresource/002/green.png", "..."},
            {"/subresource/002/style.css", R"(@import url("./styles/green.css");)"},
            {"/subresource/002/styles/green.css", R"(.box { background-image: url("../green.png"); })"},

            {"/subresource/004/style.css", R"(@import url("./styles/green.css");)"},
            {"/subresource/004/styles/green.css", R"(.box { background-image: url("green.png"); })"},
            {"/subresource/004/styles/green.png", "..."},

            {"/subresource/005/style.css", R"(@import url("./styles/green.css");)"},
            {"/subresource/005/styles/green.css", R"(.box { background-image: url("./green.png"); })"},
            {"/subresource/005/styles/green.png", "..."},

            {"/subresource/007/green.png", "..."},
            {"/subresource/007/style.css", R"(.box { background-image: url("./green.png"); })"},

            {"/url-format/001/default/a.css", R"(.box { background-color: green; })"},
            {"/url-format/001/default/style.css", R"(@import url(a.css);)"},

            {"/url-format/001/relative-url/a.css", R"(.box { background-color: green; })"},
            {"/url-format/001/relative-url/style.css", R"(@import url(./a.css);)"},

            {"/url-format/002/default/a.css", R"(.box { background-color: green; })"},
            {"/url-format/002/default/style.css", R"(@import "a.css";)"},

            {"/url-format/002/relative-url/a.css", R"(.box { background-color: green; })"},
            {"/url-format/002/relative-url/style.css", R"(@import "./a.css";)"},

            {"/url-format/003/default/a.css", R"(.box { background-color: green; })"},
            {"/url-format/003/default/style.css", R"(@import url("a.css")"},

            {"/url-format/003/relative-url/a.css", R"(.box { background-color: green; })"},
            {"/url-format/003/relative-url/style.css", R"(@import url("./a.css")"},

            {"/url-fragments/001/a.css", R"(.box { background-color: green; })"},
            {"/url-fragments/001/style.css", R"(@import url("./a.css#foo");)"},

            {"/url-fragments/002/a.css", R"(.box { background-color: green; })"},
            {"/url-fragments/002/b.css", R"(.box { background-color: red; })"},
            {"/url-fragments/002/style.css", R"(@import url("./a.css#1"); @import url("./b.css#2"); @import url("./a.css#3");)"},
        },
        .entry_paths = {
            "/001/default/style.css",
            "/001/relative-url/style.css",

            "/at-charset/001/style.css",

            "/at-keyframes/001/style.css",

            "/at-layer/001/style.css",
            "/at-layer/002/style.css",
            "/at-layer/003/style.css",
            "/at-layer/004/style.css",
            "/at-layer/005/style.css",
            "/at-layer/006/style.css",
            "/at-layer/007/style.css",
            "/at-layer/008/style.css",

            "/at-media/001/default/style.css",
            "/at-media/002/style.css",
            "/at-media/003/style.css",
            "/at-media/004/style.css",
            "/at-media/005/style.css",
            "/at-media/006/style.css",
            "/at-media/007/style.css",
            "/at-media/008/style.css",

            "/at-supports/001/style.css",
            "/at-supports/002/style.css",
            "/at-supports/003/style.css",
            "/at-supports/004/style.css",
            "/at-supports/005/style.css",

            "/cycles/001/style.css",
            "/cycles/002/style.css",
            "/cycles/003/style.css",
            "/cycles/004/style.css",
            "/cycles/005/style.css",
            "/cycles/006/style.css",
            "/cycles/007/style.css",
            "/cycles/008/style.css",

            "/data-urls/002/style.css",
            "/data-urls/003/style.css",

            "/duplicates/001/style.css",
            "/duplicates/002/style.css",

            "/empty/001/style.css",

            "/relative-paths/001/style.css",
            "/relative-paths/002/style.css",

            "/subresource/001/style.css",
            "/subresource/002/style.css",
            "/subresource/004/style.css",
            "/subresource/005/style.css",
            "/subresource/007/style.css",

            "/url-format/001/default/style.css",
            "/url-format/001/relative-url/style.css",
            "/url-format/002/default/style.css",
            "/url-format/002/relative-url/style.css",
            "/url-format/003/default/style.css",
            "/url-format/003/relative-url/style.css",
            "/url-fragments/001/style.css",
            "/url-fragments/002/style.css",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".png", guchho::config::Loader::kBase64},
            },
        },
        .expected_scan_log =
            "relative-paths/001/a/a.css: WARNING: Expected \";\" but found end of file\n"
            "relative-paths/002/a/a.css: WARNING: Expected \";\" but found end of file\n"
            "url-format/003/default/style.css: WARNING: Expected \")\" to go with \"(\"\n"
            "url-format/003/default/style.css: NOTE: The unbalanced \"(\" is here:\n"
            "url-format/003/relative-url/style.css: WARNING: Expected \")\" to go with \"(\"\n"
            "url-format/003/relative-url/style.css: NOTE: The unbalanced \"(\" is here:\n",
    });
}

TEST(BundlerCSS, CSSAtImportConditionsAtLayerBundle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/case1.css", R"(
                @import url(case1-foo.css) layer(first.one);
                @import url(case1-foo.css) layer(last.one);
                @import url(case1-foo.css) layer(first.one);
            )"},
            {"/case1-foo.css", R"(body { color: red })"},

            {"/case2.css", R"(
                @import url(case2-foo.css);
                @import url(case2-bar.css);
                @import url(case2-foo.css);
            )"},
            {"/case2-foo.css", R"(@layer first.one { body { color: red } })"},
            {"/case2-bar.css", R"(@layer last.one { body { color: green } })"},

            {"/case3.css", R"(
                @import url(case3-foo.css);
                @import url(case3-bar.css);
                @import url(case3-foo.css);
            )"},
            {"/case3-foo.css", R"(@layer { body { color: red } })"},
            {"/case3-bar.css", R"(@layer only.one { body { color: green } })"},

            {"/case4.css", R"(
                @import url(case4-foo.css) layer(first);
                @import url(case4-foo.css) layer(last);
                @import url(case4-foo.css) layer(first);
            )"},
            {"/case4-foo.css", R"(@layer one { @layer two, three.four; body { color: red } })"},

            {"/case5.css", R"(
                @import url(case5-foo.css) layer;
                @import url(case5-foo.css) layer(middle);
                @import url(case5-foo.css) layer;
            )"},
            {"/case5-foo.css", R"(@layer one { @layer two, three.four; body { color: red } })"},

            {"/case6.css", R"(
                @import url(case6-foo.css) layer(first);
                @import url(case6-foo.css) layer(last);
                @import url(case6-foo.css) layer(first);
            )"},
            {"/case6-foo.css", R"(@layer { @layer two, three.four; body { color: red } })"},
        },
        .entry_paths = {
            "/case1.css",
            "/case2.css",
            "/case3.css",
            "/case4.css",
            "/case5.css",
            "/case6.css",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsAtLayerBundleAlternatingLayerInFile) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.css", R"(@layer first { body { color: red } })"},
            {"/b.css", R"(@layer last { body { color: green } })"},

            {"/case1.css", R"(
                @import url(a.css);
                @import url(a.css);
            )"},

            {"/case2.css", R"(
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
            )"},

            {"/case3.css", R"(
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
                @import url(b.css);
            )"},

            {"/case4.css", R"(
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
            )"},

            {"/case5.css", R"(
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
                @import url(b.css);
            )"},

            // Note: There was a bug that only showed up in this case. We need at least this many cases.
            {"/case6.css", R"(
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
                @import url(b.css);
                @import url(a.css);
            )"},
        },
        .entry_paths = {
            "/case1.css",
            "/case2.css",
            "/case3.css",
            "/case4.css",
            "/case5.css",
            "/case6.css",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsAtLayerBundleAlternatingLayerOnImport) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.css", R"(body { color: red })"},
            {"/b.css", R"(body { color: green })"},

            {"/case1.css", R"(
                @import url(a.css) layer(first);
                @import url(a.css) layer(first);
            )"},

            {"/case2.css", R"(
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
            )"},

            {"/case3.css", R"(
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
            )"},

            {"/case4.css", R"(
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
            )"},

            {"/case5.css", R"(
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
            )"},

            // Note: There was a bug that only showed up in this case. We need at least this many cases.
            {"/case6.css", R"(
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
                @import url(b.css) layer(last);
                @import url(a.css) layer(first);
            )"},
        },
        .entry_paths = {
            "/case1.css",
            "/case2.css",
            "/case3.css",
            "/case4.css",
            "/case5.css",
            "/case6.css",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, CSSAtImportConditionsChainExternal) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "a.css" layer(a) not print;
            )"},
            {"/a.css", R"(
                @import "http://example.com/external1.css";
                @import "b.css" layer(b) not tv;
                @import "http://example.com/external2.css" layer(a2);
            )"},
            {"/b.css", R"(
                @import "http://example.com/external3.css";
                @import "http://example.com/external4.css" layer(b2);
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
        },
    });
}

// This test mainly just makes sure that this scenario doesn't crash
TEST(BundlerCSS, CSSAndJavaScriptCodeSplittingIssue1064) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", R"(
                import shared from './shared.js'
                console.log(shared() + 1)
            )"},
            {"/b.js", R"(
                import shared from './shared.js'
                console.log(shared() + 2)
            )"},
            {"/c.css", R"(
                @import "./shared.css";
                body { color: red }
            )"},
            {"/d.css", R"(
                @import "./shared.css";
                body { color: blue }
            )"},
            {"/shared.js", R"(
                export default function() { return 3 }
            )"},
            {"/shared.css", R"(
                body { background: black }
            )"},
        },
        .entry_paths = {
            "/a.js",
            "/b.js",
            "/c.css",
            "/d.css",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .CodeSplitting = true,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, CSSExternalQueryAndHashNoMatchIssue1822) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a { background: url(foo/bar.png?baz) }
                b { background: url(foo/bar.png#baz) }
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {
                        guchho::config::WildcardPattern{.Suffix = ".png"},
                    },
                },
            },
        },
        .expected_scan_log = R"(entry.css: ERROR: Could not resolve "foo/bar.png?baz"
NOTE: You can mark the path "foo/bar.png?baz" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
entry.css: ERROR: Could not resolve "foo/bar.png#baz"
NOTE: You can mark the path "foo/bar.png#baz" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)",
    });
}

TEST(BundlerCSS, CSSExternalQueryAndHashMatchIssue1822) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a { background: url(foo/bar.png?baz) }
                b { background: url(foo/bar.png#baz) }
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Patterns = {
                        guchho::config::WildcardPattern{.Suffix = ".png?baz"},
                        guchho::config::WildcardPattern{.Suffix = ".png#baz"},
                    },
                },
            },
        },
    });
}

TEST(BundlerCSS, CSSNestingOldBrowser) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            // These are now the only two cases that warn about ":is" not being supported
            {"/two-type-selectors.css", R"(a { .c b& { color: red; } })"},
            {"/two-parent-selectors.css", R"(a b { .c & { color: red; } })"},

            // Make sure this only generates one warning (even though it generates ":is" three times)
            {"/only-one-warning.css", R"(.a, .b .c, .d { & > & { color: red; } })"},

            {"/nested-@layer.css", R"(a { @layer base { color: red; } })"},
            {"/nested-@media.css", R"(a { @media screen { color: red; } })"},
            {"/nested-ampersand-twice.css", R"(a { &, & { color: red; } })"},
            {"/nested-ampersand-first.css", R"(a { &, b { color: red; } })"},
            {"/nested-attribute.css", R"(a { [href] { color: red; } })"},
            {"/nested-colon.css", R"(a { :hover { color: red; } })"},
            {"/nested-dot.css", R"(a { .cls { color: red; } })"},
            {"/nested-greaterthan.css", R"(a { > b { color: red; } })"},
            {"/nested-hash.css", R"(a { #id { color: red; } })"},
            {"/nested-plus.css", R"(a { + b { color: red; } })"},
            {"/nested-tilde.css", R"(a { ~ b { color: red; } })"},

            {"/toplevel-ampersand-twice.css", R"(&, & { color: red; })"},
            {"/toplevel-ampersand-first.css", R"(&, a { color: red; })"},
            {"/toplevel-ampersand-second.css", R"(a, & { color: red; })"},
            {"/toplevel-attribute.css", R"([href] { color: red; })"},
            {"/toplevel-colon.css", R"(:hover { color: red; })"},
            {"/toplevel-dot.css", R"(.cls { color: red; })"},
            {"/toplevel-greaterthan.css", R"(> b { color: red; })"},
            {"/toplevel-hash.css", R"(#id { color: red; })"},
            {"/toplevel-plus.css", R"(+ b { color: red; })"},
            {"/toplevel-tilde.css", R"(~ b { color: red; })"},

            {"/media-ampersand-twice.css", R"(@media screen { &, & { color: red; } })"},
            {"/media-ampersand-first.css", R"(@media screen { &, a { color: red; } })"},
            {"/media-ampersand-second.css", R"(@media screen { a, & { color: red; } })"},
            {"/media-attribute.css", R"(@media screen { [href] { color: red; } })"},
            {"/media-colon.css", R"(@media screen { :hover { color: red; } })"},
            {"/media-dot.css", R"(@media screen { .cls { color: red; } })"},
            {"/media-greaterthan.css", R"(@media screen { > b { color: red; } })"},
            {"/media-hash.css", R"(@media screen { #id { color: red; } })"},
            {"/media-plus.css", R"(@media screen { + b { color: red; } })"},
            {"/media-tilde.css", R"(@media screen { ~ b { color: red; } })"},

        
            {"/page-no-warning.css", R"(@page { @top-left { background: red } })"},
        },
        .entry_paths = {
            "/two-type-selectors.css",
            "/two-parent-selectors.css",

            "/only-one-warning.css",

            "/nested-@layer.css",
            "/nested-@media.css",
            "/nested-ampersand-twice.css",
            "/nested-ampersand-first.css",
            "/nested-attribute.css",
            "/nested-colon.css",
            "/nested-dot.css",
            "/nested-greaterthan.css",
            "/nested-hash.css",
            "/nested-plus.css",
            "/nested-tilde.css",

            "/toplevel-ampersand-twice.css",
            "/toplevel-ampersand-first.css",
            "/toplevel-ampersand-second.css",
            "/toplevel-attribute.css",
            "/toplevel-colon.css",
            "/toplevel-dot.css",
            "/toplevel-greaterthan.css",
            "/toplevel-hash.css",
            "/toplevel-plus.css",
            "/toplevel-tilde.css",

            "/media-ampersand-twice.css",
            "/media-ampersand-first.css",
            "/media-ampersand-second.css",
            "/media-attribute.css",
            "/media-colon.css",
            "/media-dot.css",
            "/media-greaterthan.css",
            "/media-hash.css",
            "/media-plus.css",
            "/media-tilde.css",

            "/page-no-warning.css",
        },
        .options = guchho::config::Options{
            .OriginalTargetEnv = "chrome10",
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .UnsupportedCSSFeatures = guchho::compat::CSSFeature::kNesting | guchho::compat::CSSFeature::kIsPseudoClass,
        },
        .expected_scan_log =
            "only-one-warning.css: WARNING: Transforming this CSS nesting syntax is not supported in the configured target environment (chrome10)\n"
            "NOTE: The nesting transform for this case must generate an \":is(...)\" but the configured target environment does not support the \":is\" pseudo-class.\n"
            "two-parent-selectors.css: WARNING: Transforming this CSS nesting syntax is not supported in the configured target environment (chrome10)\n"
            "NOTE: The nesting transform for this case must generate an \":is(...)\" but the configured target environment does not support the \":is\" pseudo-class.\n"
            "two-type-selectors.css: WARNING: Transforming this CSS nesting syntax is not supported in the configured target environment (chrome10)\n"
            "NOTE: The nesting transform for this case must generate an \":is(...)\" but the configured target environment does not support the \":is\" pseudo-class.\n",
    });
}

// The mapping of JS entry point to associated CSS bundle isn't necessarily 1:1.
// Here is a case where it isn't. Two JS entry points share the same associated
// CSS bundle. This must be reflected in the metafile by only having the JS
// entry points point to the associated CSS bundle but not the other way around
// (since there isn't one JS entry point to point to). This test mainly exists
// to document this edge case.
TEST(BundlerCSS, MetafileCSSBundleTwoToOne) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/foo/entry.js", R"(
				import '../common.css'
				console.log('foo')
			)"},
            {"/bar/entry.js", R"(
				import '../common.css'
				console.log('bar')
			)"},
            {"/common.css", R"(
				body { color: red }
			)"},
        },
        .entry_paths = {
            "/foo/entry.js",
            "/bar/entry.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .NeedsMetafile = true,
            .AbsOutputDir = "/out",
            .EntryPathTemplate = {
                guchho::config::PathTemplate{.Data = "./", .Placeholder = guchho::config::PathPlaceholder::kExt},
                guchho::config::PathTemplate{.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kHash},
            },
        },
    });
}

TEST(BundlerCSS, DeduplicateRules) {
    // These are done as bundler tests instead of parser tests because rule
    // deduplication now happens during linking (so that it has effects across files)
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/yes0.css", "a { color: red; color: green; color: red }"},
            {"/yes1.css", "a { color: red } a { color: green } a { color: red }"},
            {"/yes2.css", "@media screen { a { color: red } } @media screen { a { color: red } }"},
            {"/yes3.css", "@media screen { a { color: red } } @media screen { & a { color: red } }"},

            {"/no0.css", "@media screen { a { color: red } } @media screen { b a& { color: red } }"},
            {"/no1.css", "@media screen { a { color: red } } @media screen { a[x] { color: red } }"},
            {"/no2.css", "@media screen { a { color: red } } @media screen { a.x { color: red } }"},
            {"/no3.css", "@media screen { a { color: red } } @media screen { a#x { color: red } }"},
            {"/no4.css", "@media screen { a { color: red } } @media screen { a:x { color: red } }"},
            {"/no5.css", "@media screen { a:x { color: red } } @media screen { a:x(y) { color: red } }"},
            {"/no6.css", "@media screen { a b { color: red } } @media screen { a + b { color: red } }"},

            {"/across-files.css", "@import 'across-files-0.css'; @import 'across-files-1.css'; @import 'across-files-2.css';"},
            {"/across-files-0.css", "a { color: red; color: red }"},
            {"/across-files-1.css", "a { color: green }"},
            {"/across-files-2.css", "a { color: red }"},

            {"/across-files-url.css", "@import 'across-files-url-0.css'; @import 'across-files-url-1.css'; @import 'across-files-url-2.css';"},
            {"/across-files-url-0.css", "@import 'http://example.com/some.css'; @font-face { src: url(http://example.com/some.font); }"},
            {"/across-files-url-1.css", "@font-face { src: url(http://example.com/some.other.font); }"},
            {"/across-files-url-2.css", "@font-face { src: url(http://example.com/some.font); }"},
        },
        .entry_paths = {
            "/yes0.css",
            "/yes1.css",
            "/yes2.css",
            "/yes3.css",

            "/no0.css",
            "/no1.css",
            "/no2.css",
            "/no3.css",
            "/no4.css",
            "/no5.css",
            "/no6.css",

            "/across-files.css",
            "/across-files-url.css",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerCSS, DeduplicateRulesGlobalVsLocalNames) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "a.css";
                @import "b.css";
            )"},
            {"/a.css", R"(
                a { color: red } /* SHOULD BE REMOVED */
                b { color: green }

                :global(.foo) { color: red } /* SHOULD BE REMOVED */
                :global(.bar) { color: green }

                :local(.foo) { color: red }
                :local(.bar) { color: green }

                div :global { animation-name: anim_global } /* SHOULD BE REMOVED */
                div :local { animation-name: anim_local }
            )"},
            {"/b.css", R"(
                a { color: red }
                b { color: blue }

                :global(.foo) { color: red }
                :global(.bar) { color: blue }

                :local(.foo) { color: red }
                :local(.bar) { color: blue }

                div :global { animation-name: anim_global }
                div :local { animation-name: anim_local }
            )"},
        },
        .entry_paths = {"entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kLocalCSS},
            },
            .MinifySyntax = true,
        },
    });
}

// This test makes sure JS files that import local CSS names using the
// wrong name (e.g. a typo) get a warning so that the problem is noticed.
TEST(BundlerCSS, UndefinedImportWarningCSS) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"(
                import * as empty_js from './empty.js'
                import * as empty_esm_js from './empty.esm.js'
                import * as empty_json from './empty.json'
                import * as empty_css from './empty.css'
                import * as empty_global_css from './empty.global-css'
                import * as empty_local_css from './empty.local-css'

                import * as pkg_empty_js from 'pkg/empty.js'
                import * as pkg_empty_esm_js from 'pkg/empty.esm.js'
                import * as pkg_empty_json from 'pkg/empty.json'
                import * as pkg_empty_css from 'pkg/empty.css'
                import * as pkg_empty_global_css from 'pkg/empty.global-css'
                import * as pkg_empty_local_css from 'pkg/empty.local-css'

                import 'pkg'

                console.log(
                    empty_js.foo,
                    empty_esm_js.foo,
                    empty_json.foo,
                    empty_css.foo,
                    empty_global_css.foo,
                    empty_local_css.foo,
                )

                console.log(
                    pkg_empty_js.foo,
                    pkg_empty_esm_js.foo,
                    pkg_empty_json.foo,
                    pkg_empty_css.foo,
                    pkg_empty_global_css.foo,
                    pkg_empty_local_css.foo,
                )
            )"},

            {"/empty.js", ""},
            {"/empty.esm.js", R"(export {})"},
            {"/empty.json", R"({})"},
            {"/empty.css", ""},
            {"/empty.global-css", ""},
            {"/empty.local-css", ""},

            {"/node_modules/pkg/empty.js", ""},
            {"/node_modules/pkg/empty.esm.js", R"(export {})"},
            {"/node_modules/pkg/empty.json", R"({})"},
            {"/node_modules/pkg/empty.css", ""},
            {"/node_modules/pkg/empty.global-css", ""},
            {"/node_modules/pkg/empty.local-css", ""},

            // Files inside of "node_modules" should not generate a warning
            {"/node_modules/pkg/index.js", R"(
                import * as empty_js from './empty.js'
                import * as empty_esm_js from './empty.esm.js'
                import * as empty_json from './empty.json'
                import * as empty_css from './empty.css'
                import * as empty_global_css from './empty.global-css'
                import * as empty_local_css from './empty.local-css'

                console.log(
                    empty_js.foo,
                    empty_esm_js.foo,
                    empty_json.foo,
                    empty_css.foo,
                    empty_global_css.foo,
                    empty_local_css.foo,
                )
            )"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".json", guchho::config::Loader::kJSON},
                {".css", guchho::config::Loader::kCSS},
                {".global-css", guchho::config::Loader::kGlobalCSS},
                {".local-css", guchho::config::Loader::kLocalCSS},
            },
        },
        .expected_compile_log =
            "entry.js: WARNING: Import \"foo\" will always be undefined because the file \"empty.js\" has no exports\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"empty.esm.js\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"empty.json\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"empty.css\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"empty.global-css\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"empty.local-css\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because the file \"node_modules/pkg/empty.js\" has no exports\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"node_modules/pkg/empty.esm.js\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"node_modules/pkg/empty.json\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"node_modules/pkg/empty.css\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"node_modules/pkg/empty.global-css\"\n"
            "entry.js: WARNING: Import \"foo\" will always be undefined because there is no matching export in \"node_modules/pkg/empty.local-css\"\n",
    });
}

TEST(BundlerCSS, CSSMalformedAtImport) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "./url-token-eof.css";
                @import "./url-token-whitespace-eof.css";
                @import "./function-token-eof.css";
                @import "./function-token-whitespace-eof.css";
            )"},
            {"/url-token-eof.css", R"(@import url(https://example.com/url-token-eof.css)"},
            {"/url-token-whitespace-eof.css", R"(
                @import url(https://example.com/url-token-whitespace-eof.css
            )"},
            {"/function-token-eof.css", R"(@import url("https://example.com/function-token-eof.css")"},
            {"/function-token-whitespace-eof.css", R"(
                @import url("https://example.com/function-token-whitespace-eof.css"
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
        .expected_scan_log =
            "function-token-eof.css: WARNING: Expected \")\" to go with \"(\"\n"
            "function-token-eof.css: NOTE: The unbalanced \"(\" is here:\n"
            "function-token-whitespace-eof.css: WARNING: Expected \")\" to go with \"(\"\n"
            "function-token-whitespace-eof.css: NOTE: The unbalanced \"(\" is here:\n"
            "url-token-eof.css: WARNING: Expected \")\" to end URL token\n"
            "url-token-eof.css: NOTE: The unbalanced \"(\" is here:\n"
            "url-token-eof.css: WARNING: Expected \";\" but found end of file\n"
            "url-token-whitespace-eof.css: WARNING: Expected \")\" to end URL token\n"
            "url-token-whitespace-eof.css: NOTE: The unbalanced \"(\" is here:\n"
            "url-token-whitespace-eof.css: WARNING: Expected \";\" but found end of file\n",
    });
}

TEST(BundlerCSS, CSSAtLayerBeforeImportNoBundle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @layer layer1, layer2.layer3;
                @import "a.css";
                @import "b.css";
                @layer layer6.layer7, layer8;
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, CSSAtLayerBeforeImportBundle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @layer layer1, layer2.layer3;
                @import "a.css";
                @import "b.css";
                @layer layer6.layer7, layer8;
            )"},
            {"/a.css", R"(
                @layer layer4 {
                    a { color: red }
                }
            )"},
            {"/b.css", R"(
                @layer layer5 {
                    b { color: red }
                }
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, CSSAtLayerMergingWithImportConditions) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                @import "a.css" supports(color: first);

                @import "a.css" supports(color: second);
                @import "b.css" supports(color: second);

                @import "a.css" supports(color: first);
                @import "b.css" supports(color: first);

                @import "a.css" supports(color: second);
                @import "b.css" supports(color: second);

                @import "b.css" supports(color: first);
            )"},
            {"/a.css", R"(
                @layer a;
                @import "http://example.com/a.css";
            )"},
            {"/b.css", R"(
                @layer b;
                @import "http://example.com/b.css";
            )"},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerCSS, CSSCaseInsensitivity) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css",
                "/* \"@IMPORT\" should be recognized as an import */\n"
                "/* \"LAYER(...)\" should wrap with \"@layer\" */\n"
                "/* \"SUPPORTS(...)\" should wrap with \"@supports\" */\n"
                "@IMPORT Url(\"nested.css\") LAYER(layer-name) SUPPORTS(supports-condition) list-of-media-queries;\n"},
            {"/nested.css", R"(
                /* "from" should be recognized and optimized to "0%" */
                @KeyFrames Foo {
                    froM { OPAcity: 0 }
                    tO { opaCITY: 1 }
                }

                body {
                    /* "#FF0000" should be optimized to "red" because "BACKGROUND-color" should be recognized */
                    BACKGROUND-color: #FF0000;

                    /* This should be optimized to 50px */
                    width: CaLc(20Px + 30pX);

                    /* This URL token should be recognized and bundled */
                    background-IMAGE: Url(image.png);
                }
            )"},
            {"/image.png", "..."},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".png", guchho::config::Loader::kCopy},
            },
            .MinifySyntax = true,
        },
    });
}

TEST(BundlerCSS, CSSAssetPathsWithSpacesBundle) {
    css_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.css", R"(
                a {
                    background: url(foo.copy);
                    background: url(foo.file);
                }

                /*! The URLs for "foo 2" files must have quotes in the final CSS */
                b {
                    background: url('foo 2.copy');
                    background: url('foo 2.file');
                }
            )"},
            {"/foo.file", "..."},
            {"/foo.copy", "..."},
            {"/foo 2.file", "..."},
            {"/foo 2.copy", "..."},
        },
        .entry_paths = {"/entry.css"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.css",
            .ExtensionToLoader = {
                {".css", guchho::config::Loader::kCSS},
                {".file", guchho::config::Loader::kFile},
                {".copy", guchho::config::Loader::kCopy},
            },
            .MinifySyntax = true,
        },
    });
}

} // namespace bundler::test
