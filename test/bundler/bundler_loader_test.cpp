#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

Suite loader_suite{"loader"};

TEST(BundlerLoader, TestLoaderFile) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				console.log(require('./test.svg'))
			)"},
			{"/test.svg", "<svg></svg>"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out/",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".svg", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileMultipleNoCollision) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				console.log(
					require('./a/test.txt'),
					require('./b/test.txt'),
				)
			)"},

			// Two files with the same contents but different paths
			{"/a/test.txt", "test"},
			{"/b/test.txt", "test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/dist/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestJSXSyntaxInJSWithJSXLoader) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				console.log(<div/>)
			)"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJSX},
			},
		},
	});
}

TEST(BundlerLoader, TestJSXPreserveCapitalLetter) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"(
				import { mustStartWithUpperCaseLetter as Test } from './foo'
				console.log(<Test/>)
			)"},
			{"/foo.js", R"(
				export class mustStartWithUpperCaseLetter {}
			)"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.JSX = guchho::config::JSXOptions{
				.Parse = true,
				.Preserve = true,
			},
		},
	});
}

TEST(BundlerLoader, TestJSXPreserveCapitalLetterMinify) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"(
				import { mustStartWithUpperCaseLetter as XYYYY } from './foo'
				console.log(<XYYYY tag-must-start-with-capital-letter />)
			)"},
			{"/foo.js", R"(
				export class mustStartWithUpperCaseLetter {}
			)"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			.JSX = guchho::config::JSXOptions{
				.Parse = true,
				.Preserve = true,
			},
		},
	});
}

TEST(BundlerLoader, TestJSXPreserveCapitalLetterMinifyNested) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"(
				x = () => {
					class XYYYYY {} // This should be named "Y" due to frequency analysis
					return <XYYYYY tag-must-start-with-capital-letter />
				}
			)"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			.JSX = guchho::config::JSXOptions{
				.Parse = true,
				.Preserve = true,
			},
		},
	});
}

TEST(BundlerLoader, TestRequireCustomExtensionString) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				console.log(require('./test.custom'))
			)"},
			{"/test.custom", "#include <stdio.h>"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".custom", guchho::config::Loader::kText},
			},
		},
	});
}

TEST(BundlerLoader, TestRequireCustomExtensionBase64) {
    loader_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"test(
                console.log(require('./test.custom'))
            )test"},
            {"/test.custom", std::string({'a', '\0', 'b', static_cast<char>(0x80),
                                          'c', static_cast<char>(0xFF), 'd'})},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .ExtensionToLoader = {
                {".js", guchho::config::Loader::kJS},
                {".custom", guchho::config::Loader::kBase64},
            },
        },
    });
}

TEST(BundlerLoader, TestRequireCustomExtensionDataURL) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				console.log(require('./test.custom'))
			)"},
			{"/test.custom", std::string({'a', '\0', 'b', static_cast<char>(0x80),
			                              'c', static_cast<char>(0xFF), 'd'})},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".custom", guchho::config::Loader::kDataURL},
			},
		},
	});
}

TEST(BundlerLoader, TestRequireCustomExtensionPreferLongest) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				console.log(require('./test.txt'), require('./test.base64.txt'))
			)"},
			{"/test.txt",        "test.txt"},
			{"/test.base64.txt", "test.base64.txt"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kText},
				{".base64.txt", guchho::config::Loader::kBase64},
			},
		},
	});
}

TEST(BundlerLoader, TestAutoDetectMimeTypeFromExtension) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				console.log(require('./test.svg'))
			)"},
			{"/test.svg", std::string({'a', '\0', 'b', static_cast<char>(0x80),
			                          'c', static_cast<char>(0xFF), 'd'})},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".svg", guchho::config::Loader::kDataURL},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONCommonJSAndES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				const x_json = require('./x.json')
				import y_json from './y.json'
				import {small, if as fi} from './z.json'
				console.log(x_json, y_json, small, fi)
			)"},
			{"/x.json", R"({"x": true})"},
			{"/y.json", R"({"y1": true, "y2": false})"},
			{"/z.json", R"({
				"big": "this is a big long line of text that should be discarded",
				"small": "some small text",
				"if": "test keyword imports"
			})"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONInvalidIdentifierES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import * as ns from './test.json'
				import * as ns2 from './test2.json'
				console.log(ns['invalid-identifier'], ns2)
			)"},
			{"/test.json",  R"({"invalid-identifier": true})"},
			{"/test2.json", R"({"invalid-identifier": true})"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONMissingES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import {missing} from './test.json'
			)"},
			{"/test.json", R"({"present": true})"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_compile_log = R"(entry.js: ERROR: No matching export in "test.json" for import "missing"
)",
	});
}

TEST(BundlerLoader, TestLoaderTextCommonJSAndES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				const x_txt = require('./x.txt')
				import y_txt from './y.txt'
				console.log(x_txt, y_txt)
			)"},
			{"/x.txt", "x"},
			{"/y.txt", "y"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderBase64CommonJSAndES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				const x_b64 = require('./x.b64')
				import y_b64 from './y.b64'
				console.log(x_b64, y_b64)
			)"},
			{"/x.b64", "x"},
			{"/y.b64", "y"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".b64", guchho::config::Loader::kBase64},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLCommonJSAndES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				const x_url = require('./x.txt')
				import y_url from './y.txt'
				console.log(x_url, y_url)
			)"},
			{"/x.txt", "x"},
			{"/y.txt", "y"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kDataURL},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileCommonJSAndES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				const x_url = require('./x.txt')
				import y_url from './y.txt'
				console.log(x_url, y_url)
			)"},
			{"/x.txt", "x"},
			{"/y.txt", "y"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileRelativePathJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.js", R"(
				import x from '../images/image.png'
				console.log(x)
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".png", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileRelativePathCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.css", R"(
				div {
					background: url(../images/image.png);
				}
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".png", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileRelativePathAssetNamesJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.js", R"(
				import x from '../images/image.png'
				console.log(x)
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".png", guchho::config::Loader::kFile},
			},
			.AssetPathTemplate = {
				{.Data = "", .Placeholder = guchho::config::PathPlaceholder::kDir},
				{.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
				{.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileExtPathAssetNamesJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.js", R"(
				import x from '../images/image.png'
				import y from '../uploads/file.txt'
				console.log(x, y)
			)"},
			{"/src/images/image.png", "x"},
			{"/src/uploads/file.txt", "y"},
		},
		.entry_paths = {"/src/entries/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".png", guchho::config::Loader::kFile},
				{".txt", guchho::config::Loader::kFile},
			},
			.AssetPathTemplate = {
				{.Data = "", .Placeholder = guchho::config::PathPlaceholder::kExt},
				{.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
				{.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileRelativePathAssetNamesCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.css", R"(
				div {
					background: url(../images/image.png);
				}
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".png", guchho::config::Loader::kFile},
			},
			.AssetPathTemplate = {
				{.Data = "", .Placeholder = guchho::config::PathPlaceholder::kDir},
				{.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
				{.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFilePublicPathJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.js", R"(
				import x from '../images/image.png'
				console.log(x)
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".png", guchho::config::Loader::kFile},
			},
			.PublicPath = "https://example.com",
		},
	});
}

TEST(BundlerLoader, TestLoaderFilePublicPathCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.css", R"(
				div {
					background: url(../images/image.png);
				}
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".png", guchho::config::Loader::kFile},
			},
			.PublicPath = "https://example.com",
		},
	});
}

TEST(BundlerLoader, TestLoaderFilePublicPathAssetNamesJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.js", R"(
				import x from '../images/image.png'
				console.log(x)
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".png", guchho::config::Loader::kFile},
			},
			.PublicPath = "https://example.com",
			.AssetPathTemplate = {
				{.Data = "", .Placeholder = guchho::config::PathPlaceholder::kDir},
				{.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
				{.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFilePublicPathAssetNamesCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.css", R"(
				div {
					background: url(../images/image.png);
				}
			)"},
			{"/src/images/image.png", "x"},
		},
		.entry_paths = {"/src/entries/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".png", guchho::config::Loader::kFile},
			},
			.PublicPath = "https://example.com",
			.AssetPathTemplate = {
				{.Data = "", .Placeholder = guchho::config::PathPlaceholder::kDir},
				{.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
				{.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileOneSourceTwoDifferentOutputPathsJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.js", R"(
				import '../shared/common.js'
			)"},
			{"/src/entries/other/entry.js", R"(
				import '../../shared/common.js'
			)"},
			{"/src/shared/common.js", R"(
				import x from './common.png'
				console.log(x)
			)"},
			{"/src/shared/common.png", "x"},
		},
		.entry_paths = {
			"/src/entries/entry.js",
			"/src/entries/other/entry.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".png", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFileOneSourceTwoDifferentOutputPathsCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entries/entry.css", R"(
				@import "../shared/common.css";
			)"},
			{"/src/entries/other/entry.css", R"(
				@import "../../shared/common.css";
			)"},
			{"/src/shared/common.css", R"(
				div {
					background: url(common.png);
				}
			)"},
			{"/src/shared/common.png", "x"},
		},
		.entry_paths = {
			"/src/entries/entry.css",
			"/src/entries/other/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".png", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONNoBundle) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/test.json", R"({"test": 123, "invalid-identifier": true})"},
		},
		.entry_paths = {"/test.json"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONNoBundleES6) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/test.json", R"({"test": 123, "invalid-identifier": true})"},
		},
		.entry_paths = {"/test.json"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONNoBundleES6ArbitraryModuleNamespaceNames) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/test.json", R"({"test": 123, "invalid-identifier": true})"},
		},
		.entry_paths = {"/test.json"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONNoBundleCommonJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/test.json", R"({"test": 123, "invalid-identifier": true})"},
		},
		.entry_paths = {"/test.json"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONNoBundleIIFE) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/test.json", R"({"test": 123, "invalid-identifier": true})"},
		},
		.entry_paths = {"/test.json"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONSharedWithMultipleEntriesIssue413) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"(
				import data from './data.json'
				console.log('a:', data)
			)"},
			{"/b.js", R"(
				import data from './data.json'
				console.log('b:', data)
			)"},
			{"/data.json", R"({"test": 123})"},
		},
		.entry_paths = {"/a.js", "/b.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerLoader, TestLoaderFileWithQueryParameter) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				// Each of these should have a separate identity (i.e. end up in the output file twice)
				import foo from './file.txt?foo'
				import bar from './file.txt?bar'
				console.log(foo, bar)
			)"},
			{"/file.txt", "This is some text"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kFile},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderFromExtensionWithQueryParameter) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from './file.abc?query.xyz'
				console.log(foo)
			)"},
			{"/file.abc", "This should not be base64 encoded"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".abc", guchho::config::Loader::kText},
				{".xyz", guchho::config::Loader::kBase64},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLTextCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.css", R"(
				@import "data:text/css,body{color:%72%65%64}";
				@import "data:text/css;base64,Ym9keXtiYWNrZ3JvdW5kOmJsdWV9";
				@import "data:text/css;charset=UTF-8,body{color:%72%65%64}";
				@import "data:text/css;charset=UTF-8;base64,Ym9keXtiYWNrZ3JvdW5kOmJsdWV9";
			)"},
		},
		.entry_paths = {"/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLTextCSSCannotImport) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.css", R"(
				@import "data:text/css,@import './other.css';";
			)"},
			{"/other.css", R"(
				div { should-not-be-imported: true }
			)"},
		},
		.entry_paths = {"/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
		.expected_scan_log = R"(<data:text/css,@import './other.css';>: ERROR: Could not resolve "./other.css"
)",
	});
}

TEST(BundlerLoader, TestLoaderDataURLTextJavaScript) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"JS(
				import "data:text/javascript,console.log('%31%32%33')";
				import "data:text/javascript;base64,Y29uc29sZS5sb2coMjM0KQ==";
				import "data:text/javascript;charset=UTF-8,console.log(%31%32%33)";
				import "data:text/javascript;charset=UTF-8;base64,Y29uc29sZS5sb2coMjM0KQ==";
			)JS"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLTextJavaScriptCannotImport) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import "data:text/javascript,import './other.js'"
			)"},
			{"/other.js", R"(
				shouldNotBeImported = true
			)"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
		.expected_scan_log = R"(<data:text/javascript,import './other.js'>: ERROR: Could not resolve "./other.js"
)",
	});
}

// The "+" character must not be interpreted as a " " character
TEST(BundlerLoader, TestLoaderDataURLTextJavaScriptPlusCharacter) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"JS(
				import "data:text/javascript,console.log(1+2)";
			)JS"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLApplicationJSON) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import a from 'data:application/json,"%31%32%33"';
				import b from 'data:application/json;base64,eyJ3b3JrcyI6dHJ1ZX0=';
				import c from 'data:application/json;charset=UTF-8,%31%32%33';
				import d from 'data:application/json;charset=UTF-8;base64,eyJ3b3JrcyI6dHJ1ZX0=';
				console.log([
					a, b, c, d,
				])
			)"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLUnknownMIME) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import a from 'data:some/thing;what,someData%31%32%33';
				import b from 'data:other/thing;stuff;base64,c29tZURhdGEyMzQ=';
				console.log(a, b)
			)"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLExtensionBasedMIME) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.foo", R"(
				export { default as css }   from "./example.css"
				export { default as eot }   from "./example.eot"
				export { default as gif }   from "./example.gif"
				export { default as htm }   from "./example.htm"
				export { default as html }  from "./example.html"
				export { default as jpeg }  from "./example.jpeg"
				export { default as jpg }   from "./example.jpg"
				export { default as js }    from "./example.js"
				export { default as json }  from "./example.json"
				export { default as mjs }   from "./example.mjs"
				export { default as otf }   from "./example.otf"
				export { default as pdf }   from "./example.pdf"
				export { default as png }   from "./example.png"
				export { default as sfnt }  from "./example.sfnt"
				export { default as svg }   from "./example.svg"
				export { default as ttf }   from "./example.ttf"
				export { default as wasm }  from "./example.wasm"
				export { default as webp }  from "./example.webp"
				export { default as woff }  from "./example.woff"
				export { default as woff2 } from "./example.woff2"
				export { default as xml }   from "./example.xml"
			)"},
			{"/example.css",   "css"},
			{"/example.eot",   "eot"},
			{"/example.gif",   "gif"},
			{"/example.htm",   "htm"},
			{"/example.html",  "html"},
			{"/example.jpeg",  "jpeg"},
			{"/example.jpg",   "jpg"},
			{"/example.js",    "js"},
			{"/example.json",  "json"},
			{"/example.mjs",   "mjs"},
			{"/example.otf",   "otf"},
			{"/example.pdf",   "pdf"},
			{"/example.png",   "png"},
			{"/example.sfnt",  "sfnt"},
			{"/example.svg",   "svg"},
			{"/example.ttf",   "ttf"},
			{"/example.wasm",  "wasm"},
			{"/example.webp",  "webp"},
			{"/example.woff",  "woff"},
			{"/example.woff2", "woff2"},
			{"/example.xml",   "xml"},
		},
		.entry_paths = {"/entry.foo"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".foo",   guchho::config::Loader::kJS},
				{".css",   guchho::config::Loader::kDataURL},
				{".eot",   guchho::config::Loader::kDataURL},
				{".gif",   guchho::config::Loader::kDataURL},
				{".htm",   guchho::config::Loader::kDataURL},
				{".html",  guchho::config::Loader::kDataURL},
				{".jpeg",  guchho::config::Loader::kDataURL},
				{".jpg",   guchho::config::Loader::kDataURL},
				{".js",    guchho::config::Loader::kDataURL},
				{".json",  guchho::config::Loader::kDataURL},
				{".mjs",   guchho::config::Loader::kDataURL},
				{".otf",   guchho::config::Loader::kDataURL},
				{".pdf",   guchho::config::Loader::kDataURL},
				{".png",   guchho::config::Loader::kDataURL},
				{".sfnt",  guchho::config::Loader::kDataURL},
				{".svg",   guchho::config::Loader::kDataURL},
				{".ttf",   guchho::config::Loader::kDataURL},
				{".wasm",  guchho::config::Loader::kDataURL},
				{".webp",  guchho::config::Loader::kDataURL},
				{".woff",  guchho::config::Loader::kDataURL},
				{".woff2", guchho::config::Loader::kDataURL},
				{".xml",   guchho::config::Loader::kDataURL},
			},
		},
	});
}

// Percent-encoded data URLs should switch over to base64
// data URLs if it would result in a smaller size
TEST(BundlerLoader, TestLoaderDataURLBase64VsPercentEncoding) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import a from './shouldUsePercent_1.txt'
				import b from './shouldUsePercent_2.txt'
				import c from './shouldUseBase64_1.txt'
				import d from './shouldUseBase64_2.txt'
				console.log(
					a,
					b,
					c,
					d,
				)
			)"},
			{"/shouldUsePercent_1.txt", "\n\n\n"},
			{"/shouldUsePercent_2.txt", "\n\n\n\n"},
			{"/shouldUseBase64_1.txt",  "\n\n\n\n\n"},
			{"/shouldUseBase64_2.txt",  "\n\n\n\n\n\n"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kDataURL},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLBase64InvalidUTF8) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import a from './binary.txt'
				console.log(a)
			)"},
			{"/binary.txt", "\xFF"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kDataURL},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderDataURLEscapePercents) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import a from './percents.txt'
				console.log(a)
			)"},
			{"/percents.txt", R"(
%, %3, %33, %333
%, %e, %ee, %eee
%, %E, %EE, %EEE
)"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".txt", guchho::config::Loader::kDataURL},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyWithBundleFromJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"(
				import x from "../assets/some.file"
				console.log(x)
			)"},
			{"/Users/user/project/assets/some.file", "stuff"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/Users/user/project",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".file", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyWithBundleFromCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.css", R"(
				body {
					background: url(../assets/some.file);
				}
			)"},
			{"/Users/user/project/assets/some.file", "stuff"},
		},
		.entry_paths = {"/Users/user/project/src/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/Users/user/project",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".file", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyWithBundleEntryPoint) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"(
				import x from "../assets/some.file"
				console.log(x)
			)"},
			{"/Users/user/project/src/entry.css", R"(
				body {
					background: url(../assets/some.file);
				}
			)"},
			{"/Users/user/project/assets/some.file", "stuff"},
		},
		.entry_paths = {
			"/Users/user/project/src/entry.js",
			"/Users/user/project/src/entry.css",
			"/Users/user/project/assets/some.file",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.NeedsMetafile = true,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/Users/user/project",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".css", guchho::config::Loader::kCSS},
				{".file", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyWithTransform) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js",     "console.log('entry')"},
			{"/Users/user/project/assets/some.file", "stuff"},
		},
		.entry_paths = {
			"/Users/user/project/src/entry.js",
			"/Users/user/project/assets/some.file",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/Users/user/project",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".file", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyWithFormat) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js",     "console.log('entry')"},
			{"/Users/user/project/assets/some.file", "stuff"},
		},
		.entry_paths = {
			"/Users/user/project/src/entry.js",
			"/Users/user/project/assets/some.file",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/Users/user/project",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".file", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestJSXAutomaticNoNameCollision) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"(
				import { Link } from "@remix-run/react"
				const x = <Link {...y} key={z} />
			)"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.JSX = guchho::config::JSXOptions{
				.AutomaticRuntime = true,
			},
		},
	});
}

TEST(BundlerLoader, TestAssertTypeJSONWrongLoader) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from './foo.json' assert { type: 'json' }
				console.log(foo)
			)"},
			{"/foo.json", "{}"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".json", guchho::config::Loader::kJS},
			},
		},
		.expected_scan_log = R"(entry.js: ERROR: The file "foo.json" was loaded with the "js" loader
entry.js: NOTE: This import assertion requires the loader to be "json" instead:
NOTE: Either reconfigure Guchho to use the "json" loader for this file, or remove this import assertion.
)",
	});
}

TEST(BundlerLoader, TestWithTypeJSONOverrideLoader) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from './foo.js' with { type: 'json' }
				console.log(foo)
			)"},
			{"/foo.js", R"({ "this is json not js": true })"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestWithTypeJSONOverrideLoaderGlob) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import("./foo" + bar, { with: { type: 'json' } }).then(console.log)
			)"},
			{"/foo.js", R"({ "this is json not js": true })"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestWithTypeBytesOverrideLoader) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from './foo.js' with { type: 'bytes' }
				console.log(foo)
			)"},
			{"/foo.js", "export default 'js'"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestWithTypeBytesOverrideLoaderGlob) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import("./foo" + bar, { with: { type: 'bytes' } }).then(console.log)
			)"},
			{"/foo.js", "export default 'js'"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestWithTypeTextOverrideLoader) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from './foo.js' with { type: 'text' }
				console.log(foo)
			)"},
			{"/foo.js", "export default 'js'"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestWithTypeTextOverrideLoaderGlob) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import("./foo" + bar, { with: { type: 'text' } }).then(console.log)
			)"},
			{"/foo.js", "export default 'js'"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestWithBadType) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from './foo.json' with { type: '' }
				import bar from './foo.json' with { type: 'garbage' }
				console.log(bar)
			)"},
			{"/foo.json", "{}"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
		},
		.expected_scan_log = R"(entry.js: ERROR: Importing with a type attribute of "" is not supported
entry.js: ERROR: Importing with a type attribute of "garbage" is not supported
)",
	});
}

TEST(BundlerLoader, TestWithBadAttribute) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from './foo.json' with { '': 'json' }
				import bar from './foo.json' with { garbage: 'json' }
				console.log(bar)
			)"},
			{"/foo.json", "{}"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
		},
		.expected_scan_log = R"(entry.js: ERROR: Importing with the "" attribute is not supported
entry.js: ERROR: Importing with the "garbage" attribute is not supported
)",
	});
}

TEST(BundlerLoader, TestEmptyLoaderJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import './a.empty'
				import * as ns from './b.empty'
				import def from './c.empty'
				import { named } from './d.empty'
				console.log(ns, def, named)
			)"},
			{"/a.empty", "throw 'FAIL'"},
			{"/b.empty", "throw 'FAIL'"},
			{"/c.empty", "throw 'FAIL'"},
			{"/d.empty", "throw 'FAIL'"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.NeedsMetafile = true,
			.SourceMapData = guchho::config::SourceMap::kExternalWithoutComment,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".empty", guchho::config::Loader::kEmpty},
			},
		},
		.expected_compile_log = R"(entry.js: WARNING: Import "named" will always be undefined because the file "d.empty" has no exports
)",
	});
}

TEST(BundlerLoader, TestEmptyLoaderCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.css", R"(
				@import 'a.empty';
				a { background: url(b.empty) }
			)"},
			{"/a.empty", "body { color: fail }"},
			{"/b.empty", "fail"},
		},
		.entry_paths = {"/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.NeedsMetafile = true,
			.SourceMapData = guchho::config::SourceMap::kExternalWithoutComment,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".empty", guchho::config::Loader::kEmpty},
			},
		},
	});
}

TEST(BundlerLoader, TestExtensionlessLoaderJS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import './what'
			)"},
			{"/what", "foo()"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{"", guchho::config::Loader::kJS},
			},
		},
	});
}

TEST(BundlerLoader, TestExtensionlessLoaderCSS) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.css", R"(
				@import './what';
			)"},
			{"/what", ".foo { color: red }"},
		},
		.entry_paths = {"/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{"", guchho::config::Loader::kCSS},
			},
		},
	});
}

// Make sure custom entry point output names are respected for the copy loader
TEST(BundlerLoader, TestLoaderCopyEntryPointAdvanced) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"(
				import xyz from './xyz.copy'
				console.log(xyz)
			)"},
			{"/project/TEST FAILED.copy", "some stuff"},
			{"/project/xyz.copy",         "more stuff"},
		},
		.entry_paths_advanced = {
			{
				.InputPath = "/project/entry.js",
				.OutputPath = "js/input/path",
				.InputPathInFileNamespace = true,
			},
			{
				.InputPath = "/project/TEST FAILED.copy",
				.OutputPath = "copy/input/path",
				.InputPathInFileNamespace = true,
			},
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".copy", guchho::config::Loader::kCopy},
			},
		},
	});
}

// Make sure we don't turn "src/index.copy" into "src.copy" for files copied
// via the file loader. This is sometimes done for JS files to try to generate
// more useful names because lots of developers name their code "index.js" due
// to node's implicit "index.js" path resolution logic.
TEST(BundlerLoader, TestLoaderCopyUseIndex) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.copy", "some stuff"},
		},
		.entry_paths = {"/Users/user/project/src/index.copy"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".copy", guchho::config::Loader::kCopy},
			},
		},
	});
}

// Make sure that if "outfile" is used, a file copied with the copy loader is
// written out to that path. We don't want the file name to come from the
// original source name instead of the "outfile" name, for example.
TEST(BundlerLoader, TestLoaderCopyExplicitOutputFile) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/TEST FAILED.copy", "some stuff"},
		},
		.entry_paths = {"/project/TEST FAILED.copy"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out/this.worked",
			.ExtensionToLoader = {
				{".copy", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyStartsWithDotAbsPath) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/src/.htaccess", "some stuff"},
			{"/project/src/entry.js",  "some.stuff()"},
			{"/project/src/.ts",       "foo as number"},
		},
		.entry_paths = {
			"/project/src/.htaccess",
			"/project/src/entry.js",
			"/project/src/.ts",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".ts", guchho::config::Loader::kTS},
				{".htaccess", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyStartsWithDotRelPath) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/src/.htaccess", "some stuff"},
			{"/project/src/entry.js",  "some.stuff()"},
			{"/project/src/.ts",       "foo as number"},
		},
		.entry_paths = {
			"./.htaccess",
			"./entry.js",
			"./.ts",
		},
		.abs_working_dir = "/project/src",
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".ts", guchho::config::Loader::kTS},
				{".htaccess", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderCopyWithInjectedFileNoBundle) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entry.ts",  "console.log('in entry.ts')"},
			{"/src/inject.js", "console.log('in inject.js')"},
		},
		.entry_paths = {"/src/entry.ts"},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".ts", guchho::config::Loader::kTS},
				{".js", guchho::config::Loader::kCopy},
			},
			.InjectPaths = {"/src/inject.js"},
		},
		.expected_scan_log = R"(ERROR: Cannot inject "src/inject.js" with the "copy" loader without bundling enabled
)",
	});
}

TEST(BundlerLoader, TestLoaderCopyWithInjectedFileBundle) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entry.ts",  "console.log('in entry.ts')"},
			{"/src/inject.js", "console.log('in inject.js')"},
		},
		.entry_paths = {"/src/entry.ts"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".ts", guchho::config::Loader::kTS},
				{".js", guchho::config::Loader::kCopy},
			},
			.InjectPaths = {"/src/inject.js"},
		},
	});
}

TEST(BundlerLoader, TestLoaderBundleWithImportAttributes) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import x from "./data.json"
				import y from "./data.json" assert { type: 'json' }
				import z from "./data.json" with { type: 'json' }
				console.log(x === y, x !== z)
			)"},
			{"/data.json", R"({ "works": true })"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderBundleWithUnknownImportAttributesAndJSLoader) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from "./foo.js" with { type: 'js' }
				import bar from "./bar.js" with { js: 'true' }
				import foo2 from "data:text/javascript,foo" with { type: 'js' }
				import bar2 from "data:text/javascript,bar" with { js: 'true' }
				console.log(foo, bar, foo2, bar2)
			)"},
			{"/foo.js", "..."},
			{"/bar.js", ",,,"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"(entry.js: ERROR: Importing with a type attribute of "js" is not supported
entry.js: ERROR: Importing with the "js" attribute is not supported
entry.js: ERROR: Importing with a type attribute of "js" is not supported
entry.js: ERROR: Importing with the "js" attribute is not supported
)",
	});
}

TEST(BundlerLoader, TestLoaderBundleWithUnknownImportAttributesAndCopyLoader) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import foo from "./foo.thing" with { type: 'whatever' }
				import bar from "./bar.thing" with { whatever: 'true' }
				console.log(foo, bar)
			)"},
			{"/foo.thing", "..."},
			{"/bar.thing", ",,,"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".thing", guchho::config::Loader::kCopy},
			},
		},
	});
}

TEST(BundlerLoader, TestLoaderBundleWithTypeJSONOnlyDefaultExport) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import x, {foo as x2} from "./data.json"
				import y, {foo as y2} from "./data.json" with { type: 'json' }
			)"},
			{"/data.json", R"({ "foo": 123 })"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_compile_log = R"(entry.js: ERROR: No matching export in "data.json with { type: 'json' }" for import "foo"
)",
	});
}

TEST(BundlerLoader, TestLoaderJSONPrototype) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import data from "./data.json"
				console.log(data)
			)"},
			{"/data.json", R"({
				"": "The property below should be converted to a computed property:",
				"__proto__": { "foo": "bar" }
			})"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONPrototypeES5) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import data from "./data.json"
				console.log(data)
			)"},
			{"/data.json", R"({
				"": "The property below should NOT be converted to a computed property for ES5:",
				"__proto__": { "foo": "bar" }
			})"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
			.UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures(
				{{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {5, 0, 0}}}}),
		},
	});
}

TEST(BundlerLoader, TestLoaderJSONWithBigInt) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import data from "./data.json"
				console.log(data)
			)"},
			{"/data.json", R"({
				"invalid": [123n]
			})"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
		.expected_scan_log = R"(data.json: ERROR: Unexpected "123n" in JSON
)",
	});
}

TEST(BundlerLoader, TestLoaderTextUTF8BOM) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"(
				import data1 from "./data1.txt"
				import data2 from "./data2.txt"
				console.log(data1, data2)
			)"},
			{"/data1.txt", "\xEF\xBB\xBFtext"},
			{"/data2.txt", "text\xEF\xBB\xBF"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerLoader, TestLoaderInlineSourceMapAbsolutePathIssue4075Unix) {
	loader_suite.ExpectBundledUnix(Bundled{
		.files = {
			{"/home/user/project/src/entry.css", R"(
				@import "./styles1.css";
				@import "./styles2.css";
			)"},
			{"/home/user/project/src/styles1.css", R"CSS(/* You can add global styles to this file, and also import other style files */
			* {
				content: "foo";
			}

			/*# sourceMappingURL=data:application/json;charset=utf-8,%7B%22version%22:3,%22sourceRoot%22:%22%22,%22sources%22:%5B%22file%3A%2F%2F%2Fout%2Fsrc%2Fstyles1.scss%22%5D,%22names%22:%5B%5D,%22mappings%22:%22AAAA;AACA;EACE,SAAS%22,%22file%22:%22out%22,%22sourcesContent%22:%5B%22/*%20You%20can%20add%20global%20styles%20to%20this%20file,%20and%20also%20import%20other%20style%20files%20%2A/%5Cn*%20%7B%5Cn%20%20content:%20%5C%22foo%5C%22%5Cn%7D%5Cn%22%5D%7D */)CSS"},
			{"/home/user/project/src/styles2.css", R"CSS(/* You can add global styles to this file, and also import other style files */
			* {
				content: "bar";
			}

			/*# sourceMappingURL=data:application/json;charset=utf-8,%7B%22version%22:3,%22sourceRoot%22:%22%22,%22sources%22:%5B%22%2Fout%2Fsrc%2Fstyles2.scss%22%5D,%22names%22:%5B%5D,%22mappings%22:%22AAAA;AACA;EACE,SAAS%22,%22file%22:%22out%22,%22sourcesContent%22:%5B%22/*%20You%20can%20add%20global%20styles%20to%20this%20file,%20and%20also%20import%20other%20style%20files%20%2A/%5Cn*%20%7B%5Cn%20%20content:%20%5C%22bar%5C%22%5Cn%7D%5Cn%22%5D%7D */)CSS"},
		},
		.entry_paths = {"/home/user/project/src/entry.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.SourceMapData = guchho::config::SourceMap::kLinkedWithComment,
			.AbsOutputDir = "/out",
		},
	});
}


TEST(BundlerLoader, TestLoaderInlineSourceMapAbsolutePathIssue4075Windows) {
	loader_suite.ExpectBundledWindows(Bundled{
		.files = {
			{R"(C:\home\user\project\src\entry.css)", R"(
				@import "./styles1.css";
				@import "./styles2.css";
			)"},
			{R"(C:\home\user\project\src\styles1.css)", R"CSS(/* You can add global styles to this file, and also import other style files */
			* {
				content: "foo";
			}

			/*# sourceMappingURL=data:application/json;charset=utf-8,%7B%22version%22:3,%22sourceRoot%22:%22%22,%22sources%22:%5B%22file%3A%2F%2F%2FC%3A%2Fout%2Fsrc%2Fstyles1.scss%22%5D,%22names%22:%5B%5D,%22mappings%22:%22AAAA;AACA;EACE,SAAS%22,%22file%22:%22out%22,%22sourcesContent%22:%5B%22/*%20You%20can%20add%20global%20styles%20to%20this%20file,%20and%20also%20import%20other%20style%20files%20%2A/%5Cn*%20%7B%5Cn%20%20content:%20%5C%22foo%5C%22%5Cn%7D%5Cn%22%5D%7D */)CSS"},
			{R"(C:\home\user\project\src\styles2.css)", R"CSS(/* You can add global styles to this file, and also import other style files */
			* {
				content: "bar";
			}

			/*# sourceMappingURL=data:application/json;charset=utf-8,%7B%22version%22:3,%22sourceRoot%22:%22%22,%22sources%22:%5B%22C%3A%5C%5Cout%5C%5Csrc%5C%5Cstyles2.scss%22%5D,%22names%22:%5B%5D,%22mappings%22:%22AAAA;AACA;EACE,SAAS%22,%22file%22:%22out%22,%22sourcesContent%22:%5B%22/*%20You%20can%20add%20global%20styles%20to%20this%20file,%20and%20also%20import%20other%20style%20files%20%2A/%5Cn*%20%7B%5Cn%20%20content:%20%5C%22bar%5C%22%5Cn%7D%5Cn%22%5D%7D */)CSS"},
		},
		.entry_paths = {R"(C:\home\user\project\src\entry.css)"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.SourceMapData = guchho::config::SourceMap::kLinkedWithComment,
			.AbsOutputDir = R"(C:\out)",
		},
	});
}


TEST(BundlerLoader, TestLoaderDataURLHashSuffixIssue4370) {
	loader_suite.ExpectBundled(Bundled{
		.files = {
			{"/icons.css", R"(
				.triangle {
					width: 10px;
					height: 10px;
					background: currentColor;
					clip-path: url(./triangle.svg#x);
				}
			)"},
			{"/triangle.svg", R"(<svg xmlns="http://www.w3.org/2000/svg"><defs><clipPath id="x"><path d="M0 0H10V10Z"/></clipPath></defs></svg>)"},
		},
		.entry_paths = {"/icons.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out/",
			.ExtensionToLoader = {
				{".css", guchho::config::Loader::kCSS},
				{".svg", guchho::config::Loader::kDataURL},
			},
		},
	});
}

} // namespace bundler::test