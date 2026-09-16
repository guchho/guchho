#include "test/guchho_test.hpp"
#include "guchho/config.hpp"

#include <string>
#include <vector>

using namespace guchho::config;

// ---------------------------------------------------------------------------
// LoaderToString
// ---------------------------------------------------------------------------

TEST(LoaderToStringTest, JS)
{
    EXPECT_EQ(LoaderToString(Loader::kJS), "js");
}

TEST(LoaderToStringTest, TS)
{
    EXPECT_EQ(LoaderToString(Loader::kTS), "ts");
}

TEST(LoaderToStringTest, TSX)
{
    EXPECT_EQ(LoaderToString(Loader::kTSX), "tsx");
}

TEST(LoaderToStringTest, JSX)
{
    EXPECT_EQ(LoaderToString(Loader::kJSX), "jsx");
}

TEST(LoaderToStringTest, CSS)
{
    EXPECT_EQ(LoaderToString(Loader::kCSS), "css");
}

TEST(LoaderToStringTest, JSON)
{
    EXPECT_EQ(LoaderToString(Loader::kJSON), "json");
}

TEST(LoaderToStringTest, WithTypeJSON)
{
    EXPECT_EQ(LoaderToString(Loader::kWithTypeJSON), "json");
}

TEST(LoaderToStringTest, TSNoAmbiguousLessThan)
{
    EXPECT_EQ(LoaderToString(Loader::kTSNoAmbiguousLessThan), "ts");
}

TEST(LoaderToStringTest, None)
{
    EXPECT_EQ(LoaderToString(Loader::kNone), "none");
}

TEST(LoaderToStringTest, Base64)
{
    EXPECT_EQ(LoaderToString(Loader::kBase64), "base64");
}

TEST(LoaderToStringTest, Text)
{
    EXPECT_EQ(LoaderToString(Loader::kText), "text");
}

TEST(LoaderToStringTest, File)
{
    EXPECT_EQ(LoaderToString(Loader::kFile), "file");
}

TEST(LoaderToStringTest, Empty)
{
    EXPECT_EQ(LoaderToString(Loader::kEmpty), "empty");
}

TEST(LoaderToStringTest, Binary)
{
    EXPECT_EQ(LoaderToString(Loader::kBinary), "binary");
}

TEST(LoaderToStringTest, Copy)
{
    EXPECT_EQ(LoaderToString(Loader::kCopy), "copy");
}

TEST(LoaderToStringTest, DataURL)
{
    EXPECT_EQ(LoaderToString(Loader::kDataURL), "dataurl");
}

TEST(LoaderToStringTest, HTML)
{
    EXPECT_EQ(LoaderToString(Loader::kHTML), "html");
}

TEST(LoaderToStringTest, DefaultLoader)
{
    EXPECT_EQ(LoaderToString(Loader::kDefault), "default");
}

TEST(LoaderToStringTest, LocalCSS)
{
    EXPECT_EQ(LoaderToString(Loader::kLocalCSS), "local-css");
}

TEST(LoaderToStringTest, GlobalCSS)
{
    EXPECT_EQ(LoaderToString(Loader::kGlobalCSS), "global-css");
}

// ---------------------------------------------------------------------------
// IsTypeScript
// ---------------------------------------------------------------------------

TEST(IsTypeScriptTest, TS)
{
    EXPECT_TRUE(IsTypeScript(Loader::kTS));
}

TEST(IsTypeScriptTest, TSNoAmbiguousLessThan)
{
    EXPECT_TRUE(IsTypeScript(Loader::kTSNoAmbiguousLessThan));
}

TEST(IsTypeScriptTest, TSX)
{
    EXPECT_TRUE(IsTypeScript(Loader::kTSX));
}

TEST(IsTypeScriptTest, JS)
{
    EXPECT_FALSE(IsTypeScript(Loader::kJS));
}

TEST(IsTypeScriptTest, CSS)
{
    EXPECT_FALSE(IsTypeScript(Loader::kCSS));
}

TEST(IsTypeScriptTest, JSON)
{
    EXPECT_FALSE(IsTypeScript(Loader::kJSON));
}

TEST(IsTypeScriptTest, None)
{
    EXPECT_FALSE(IsTypeScript(Loader::kNone));
}

// ---------------------------------------------------------------------------
// IsCSS
// ---------------------------------------------------------------------------

TEST(IsCSSTest, CSS)
{
    EXPECT_TRUE(IsCSS(Loader::kCSS));
}

TEST(IsCSSTest, GlobalCSS)
{
    EXPECT_TRUE(IsCSS(Loader::kGlobalCSS));
}

TEST(IsCSSTest, LocalCSS)
{
    EXPECT_TRUE(IsCSS(Loader::kLocalCSS));
}

TEST(IsCSSTest, JS)
{
    EXPECT_FALSE(IsCSS(Loader::kJS));
}

TEST(IsCSSTest, TS)
{
    EXPECT_FALSE(IsCSS(Loader::kTS));
}

TEST(IsCSSTest, None)
{
    EXPECT_FALSE(IsCSS(Loader::kNone));
}

// ---------------------------------------------------------------------------
// CanHaveSourceMap
// ---------------------------------------------------------------------------

TEST(CanHaveSourceMapTest, JS)
{
    EXPECT_TRUE(CanHaveSourceMap(Loader::kJS));
}

TEST(CanHaveSourceMapTest, TS)
{
    EXPECT_TRUE(CanHaveSourceMap(Loader::kTS));
}

TEST(CanHaveSourceMapTest, TSX)
{
    EXPECT_TRUE(CanHaveSourceMap(Loader::kTSX));
}

TEST(CanHaveSourceMapTest, CSS)
{
    EXPECT_TRUE(CanHaveSourceMap(Loader::kCSS));
}

TEST(CanHaveSourceMapTest, JSON)
{
    EXPECT_TRUE(CanHaveSourceMap(Loader::kJSON));
}

TEST(CanHaveSourceMapTest, Text)
{
    EXPECT_TRUE(CanHaveSourceMap(Loader::kText));
}

TEST(CanHaveSourceMapTest, File)
{
    EXPECT_FALSE(CanHaveSourceMap(Loader::kFile));
}

TEST(CanHaveSourceMapTest, Binary)
{
    EXPECT_FALSE(CanHaveSourceMap(Loader::kBinary));
}

TEST(CanHaveSourceMapTest, Base64)
{
    EXPECT_FALSE(CanHaveSourceMap(Loader::kBase64));
}

TEST(CanHaveSourceMapTest, DataURL)
{
    EXPECT_FALSE(CanHaveSourceMap(Loader::kDataURL));
}

TEST(CanHaveSourceMapTest, Copy)
{
    EXPECT_FALSE(CanHaveSourceMap(Loader::kCopy));
}

// ---------------------------------------------------------------------------
// LoaderFromFileExtension
// ---------------------------------------------------------------------------

TEST(LoaderFromFileExtensionTest, MatchesExactExtension)
{
    std::unordered_map<std::string, Loader> extMap = {
        {".js", Loader::kJS},
        {".ts", Loader::kTS},
        {".tsx", Loader::kTSX},
    };
    EXPECT_EQ(LoaderFromFileExtension(extMap, "index.ts"), Loader::kTS);
}

TEST(LoaderFromFileExtensionTest, MatchesCompoundExtension)
{
    std::unordered_map<std::string, Loader> extMap = {
        {".tsx", Loader::kTSX},
        {".ts", Loader::kTS},
    };
    EXPECT_EQ(LoaderFromFileExtension(extMap, "component.tsx"), Loader::kTSX);
}

TEST(LoaderFromFileExtensionTest, FallsBackToEarlierExtension)
{
    std::unordered_map<std::string, Loader> extMap = {
        {".ts", Loader::kTS},
        {".d.ts", Loader::kTS},
    };
    EXPECT_EQ(LoaderFromFileExtension(extMap, "types.d.ts"), Loader::kTS);
}

TEST(LoaderFromFileExtensionTest, NoDotReturnsNone)
{
    std::unordered_map<std::string, Loader> extMap = {
        {".js", Loader::kJS},
    };
    EXPECT_EQ(LoaderFromFileExtension(extMap, "Makefile"), Loader::kNone);
}

TEST(LoaderFromFileExtensionTest, EmptyKeyMatchesNoDot)
{
    std::unordered_map<std::string, Loader> extMap = {
        {"", Loader::kDefault},
    };
    EXPECT_EQ(LoaderFromFileExtension(extMap, "Makefile"), Loader::kDefault);
}

TEST(LoaderFromFileExtensionTest, UnknownExtensionReturnsNone)
{
    std::unordered_map<std::string, Loader> extMap = {
        {".js", Loader::kJS},
    };
    EXPECT_EQ(LoaderFromFileExtension(extMap, "file.unknown"), Loader::kNone);
}

// ---------------------------------------------------------------------------
// FormatToString
// ---------------------------------------------------------------------------

TEST(FormatToStringTest, IIFE)
{
    EXPECT_EQ(FormatToString(Format::kIIFE), "iife");
}

TEST(FormatToStringTest, CommonJS)
{
    EXPECT_EQ(FormatToString(Format::kCommonJS), "cjs");
}

TEST(FormatToStringTest, ESModule)
{
    EXPECT_EQ(FormatToString(Format::kESModule), "esm");
}

TEST(FormatToStringTest, UMD)
{
    EXPECT_EQ(FormatToString(Format::kUMD), "umd");
}

TEST(FormatToStringTest, AMD)
{
    EXPECT_EQ(FormatToString(Format::kAMD), "amd");
}

TEST(FormatToStringTest, System)
{
    EXPECT_EQ(FormatToString(Format::kSystem), "system");
}

TEST(FormatToStringTest, Preserve)
{
    EXPECT_EQ(FormatToString(Format::kPreserve), "");
}

// ---------------------------------------------------------------------------
// FormatKeepESMImportExportSyntax
// ---------------------------------------------------------------------------

TEST(FormatKeepESMImportExportSyntaxTest, Preserve)
{
    EXPECT_TRUE(FormatKeepESMImportExportSyntax(Format::kPreserve));
}

TEST(FormatKeepESMImportExportSyntaxTest, ESModule)
{
    EXPECT_TRUE(FormatKeepESMImportExportSyntax(Format::kESModule));
}

TEST(FormatKeepESMImportExportSyntaxTest, CommonJS)
{
    EXPECT_FALSE(FormatKeepESMImportExportSyntax(Format::kCommonJS));
}

TEST(FormatKeepESMImportExportSyntaxTest, IIFE)
{
    EXPECT_FALSE(FormatKeepESMImportExportSyntax(Format::kIIFE));
}

// ---------------------------------------------------------------------------
// FormatAllowTopLevelAwait
// ---------------------------------------------------------------------------

TEST(FormatAllowTopLevelAwaitTest, Preserve)
{
    EXPECT_TRUE(FormatAllowTopLevelAwait(Format::kPreserve));
}

TEST(FormatAllowTopLevelAwaitTest, ESModule)
{
    EXPECT_TRUE(FormatAllowTopLevelAwait(Format::kESModule));
}

TEST(FormatAllowTopLevelAwaitTest, System)
{
    EXPECT_TRUE(FormatAllowTopLevelAwait(Format::kSystem));
}

TEST(FormatAllowTopLevelAwaitTest, CommonJS)
{
    EXPECT_FALSE(FormatAllowTopLevelAwait(Format::kCommonJS));
}

TEST(FormatAllowTopLevelAwaitTest, IIFE)
{
    EXPECT_FALSE(FormatAllowTopLevelAwait(Format::kIIFE));
}

// ---------------------------------------------------------------------------
// ShouldCallRuntimeRequire
// ---------------------------------------------------------------------------

TEST(ShouldCallRuntimeRequireTest, BundleWithESModule)
{
    EXPECT_TRUE(ShouldCallRuntimeRequire(Mode::kBundle, Format::kESModule));
}

TEST(ShouldCallRuntimeRequireTest, BundleWithCommonJS)
{
    EXPECT_FALSE(ShouldCallRuntimeRequire(Mode::kBundle, Format::kCommonJS));
}

TEST(ShouldCallRuntimeRequireTest, PassThroughWithESModule)
{
    EXPECT_FALSE(ShouldCallRuntimeRequire(Mode::kPassThrough, Format::kESModule));
}

TEST(ShouldCallRuntimeRequireTest, PassThroughWithCommonJS)
{
    EXPECT_FALSE(ShouldCallRuntimeRequire(Mode::kPassThrough, Format::kCommonJS));
}

TEST(ShouldCallRuntimeRequireTest, BundleWithIIFE)
{
    EXPECT_TRUE(ShouldCallRuntimeRequire(Mode::kBundle, Format::kIIFE));
}

// ---------------------------------------------------------------------------
// LegalCommentsHasExternalFile
// ---------------------------------------------------------------------------

TEST(LegalCommentsHasExternalFileTest, LinkedWithComment)
{
    EXPECT_TRUE(LegalCommentsHasExternalFile(LegalComments::kLinkedWithComment));
}

TEST(LegalCommentsHasExternalFileTest, ExternalWithoutComment)
{
    EXPECT_TRUE(LegalCommentsHasExternalFile(LegalComments::kExternalWithoutComment));
}

TEST(LegalCommentsHasExternalFileTest, Inline)
{
    EXPECT_FALSE(LegalCommentsHasExternalFile(LegalComments::kInline));
}

TEST(LegalCommentsHasExternalFileTest, None)
{
    EXPECT_FALSE(LegalCommentsHasExternalFile(LegalComments::kNone));
}

TEST(LegalCommentsHasExternalFileTest, EndOfFile)
{
    EXPECT_FALSE(LegalCommentsHasExternalFile(LegalComments::kEndOfFile));
}

// ---------------------------------------------------------------------------
// DefineFlags
// ---------------------------------------------------------------------------

TEST(DefineFlagsTest, HasReturnsTrueForPresentFlag)
{
    DefineFlags flags = DefineFlags::kCanBeRemovedIfUnused | DefineFlags::kCallCanBeUnwrappedIfUnused;
    EXPECT_TRUE(Has(flags, DefineFlags::kCanBeRemovedIfUnused));
}

TEST(DefineFlagsTest, HasReturnsFalseForAbsentFlag)
{
    DefineFlags flags = DefineFlags::kNone;
    EXPECT_FALSE(Has(flags, DefineFlags::kCanBeRemovedIfUnused));
}

TEST(DefineFlagsTest, BitwiseOr)
{
    DefineFlags flags = DefineFlags::kCanBeRemovedIfUnused | DefineFlags::kIsSymbolInstance;
    EXPECT_TRUE(Has(flags, DefineFlags::kCanBeRemovedIfUnused));
    EXPECT_TRUE(Has(flags, DefineFlags::kIsSymbolInstance));
    EXPECT_FALSE(Has(flags, DefineFlags::kCallCanBeUnwrappedIfUnused));
}

TEST(DefineFlagsTest, BitwiseAnd)
{
    DefineFlags a = DefineFlags::kCanBeRemovedIfUnused | DefineFlags::kCallCanBeUnwrappedIfUnused;
    DefineFlags b = DefineFlags::kCanBeRemovedIfUnused | DefineFlags::kIsSymbolInstance;
    DefineFlags result = a & b;
    EXPECT_TRUE(Has(result, DefineFlags::kCanBeRemovedIfUnused));
    EXPECT_FALSE(Has(result, DefineFlags::kCallCanBeUnwrappedIfUnused));
    EXPECT_FALSE(Has(result, DefineFlags::kIsSymbolInstance));
}

// ---------------------------------------------------------------------------
// TSUnusedImportFlags
// ---------------------------------------------------------------------------

TEST(TSUnusedImportFlagsTest, HasReturnsTrueForPresentFlag)
{
    TSUnusedImportFlags flags = TSUnusedImportFlags::kKeepStmt | TSUnusedImportFlags::kKeepValues;
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepStmt));
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepValues));
}

TEST(TSUnusedImportFlagsTest, HasReturnsFalseForAbsentFlag)
{
    TSUnusedImportFlags flags = TSUnusedImportFlags::kNone;
    EXPECT_FALSE(Has(flags, TSUnusedImportFlags::kKeepStmt));
}

TEST(TSUnusedImportFlagsTest, BitwiseOr)
{
    TSUnusedImportFlags flags = TSUnusedImportFlags::kKeepStmt | TSUnusedImportFlags::kKeepValues;
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepStmt));
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepValues));
}

// ---------------------------------------------------------------------------
// MaybeBool
// ---------------------------------------------------------------------------

TEST(MaybeBoolTest, DefaultIsUnspecified)
{
    MaybeBool val{};
    EXPECT_EQ(static_cast<uint8_t>(val), 0);
}

TEST(MaybeBoolTest, UnspecifiedIsNotTrueOrFalse)
{
    EXPECT_NE(MaybeBool::kUnspecified, MaybeBool::kTrue);
    EXPECT_NE(MaybeBool::kUnspecified, MaybeBool::kFalse);
}

// ---------------------------------------------------------------------------
// TSConfigApplyExtendedConfig
// ---------------------------------------------------------------------------

TEST(TSConfigApplyExtendedConfigTest, BaseOverridesUnspecifiedDerived)
{
    TSConfig derived;
    TSConfig base;
    base.ExperimentalDecorators = MaybeBool::kTrue;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.ExperimentalDecorators, MaybeBool::kTrue);
}

TEST(TSConfigApplyExtendedConfigTest, BaseOverwritesDerivedWhenSpecified)
{
    TSConfig derived;
    derived.ExperimentalDecorators = MaybeBool::kFalse;
    TSConfig base;
    base.ExperimentalDecorators = MaybeBool::kTrue;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.ExperimentalDecorators, MaybeBool::kTrue);
}

TEST(TSConfigApplyExtendedConfigTest, UnspecifiedBaseDoesNotOverwriteDerived)
{
    TSConfig derived;
    derived.ExperimentalDecorators = MaybeBool::kFalse;
    TSConfig base;
    base.ExperimentalDecorators = MaybeBool::kUnspecified;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.ExperimentalDecorators, MaybeBool::kFalse);
}

TEST(TSConfigApplyExtendedConfigTest, BaseTargetOverridesUnspecified)
{
    TSConfig derived;
    TSConfig base;
    base.Target = TSTarget::kAtOrAboveES2022;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.Target, TSTarget::kAtOrAboveES2022);
}

TEST(TSConfigApplyExtendedConfigTest, BaseTargetOverwritesDerivedTarget)
{
    TSConfig derived;
    derived.Target = TSTarget::kBelowES2022;
    TSConfig base;
    base.Target = TSTarget::kAtOrAboveES2022;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.Target, TSTarget::kAtOrAboveES2022);
}

TEST(TSConfigApplyExtendedConfigTest, UnspecifiedTargetLeavesDerived)
{
    TSConfig derived;
    derived.Target = TSTarget::kBelowES2022;
    TSConfig base;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.Target, TSTarget::kBelowES2022);
}

TEST(TSConfigApplyExtendedConfigTest, BaseImportsNotUsedAsValuesOverrides)
{
    TSConfig derived;
    TSConfig base;
    base.ImportsNotUsedAsValues = TSImportsNotUsedAsValues::kPreserve;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.ImportsNotUsedAsValues, TSImportsNotUsedAsValues::kPreserve);
}

TEST(TSConfigApplyExtendedConfigTest, BaseVerbatimModuleSyntaxOverrides)
{
    TSConfig derived;
    TSConfig base;
    base.VerbatimModuleSyntax = MaybeBool::kTrue;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.VerbatimModuleSyntax, MaybeBool::kTrue);
}

TEST(TSConfigApplyExtendedConfigTest, BaseUseDefineForClassFieldsOverrides)
{
    TSConfig derived;
    TSConfig base;
    base.UseDefineForClassFields = MaybeBool::kTrue;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.UseDefineForClassFields, MaybeBool::kTrue);
}

TEST(TSConfigApplyExtendedConfigTest, BasePreserveValueImportsOverrides)
{
    TSConfig derived;
    TSConfig base;
    base.PreserveValueImports = MaybeBool::kTrue;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.PreserveValueImports, MaybeBool::kTrue);
}

TEST(TSConfigApplyExtendedConfigTest, AllFieldsOverriddenByBase)
{
    TSConfig derived;
    TSConfig base;
    base.ExperimentalDecorators = MaybeBool::kTrue;
    base.ImportsNotUsedAsValues = TSImportsNotUsedAsValues::kPreserve;
    base.PreserveValueImports = MaybeBool::kTrue;
    base.Target = TSTarget::kAtOrAboveES2022;
    base.UseDefineForClassFields = MaybeBool::kTrue;
    base.VerbatimModuleSyntax = MaybeBool::kTrue;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.ExperimentalDecorators, MaybeBool::kTrue);
    EXPECT_EQ(derived.ImportsNotUsedAsValues, TSImportsNotUsedAsValues::kPreserve);
    EXPECT_EQ(derived.PreserveValueImports, MaybeBool::kTrue);
    EXPECT_EQ(derived.Target, TSTarget::kAtOrAboveES2022);
    EXPECT_EQ(derived.UseDefineForClassFields, MaybeBool::kTrue);
    EXPECT_EQ(derived.VerbatimModuleSyntax, MaybeBool::kTrue);
}

TEST(TSConfigApplyExtendedConfigTest, EmptyBaseLeavesDerivedUnchanged)
{
    TSConfig derived;
    derived.ExperimentalDecorators = MaybeBool::kTrue;
    derived.ImportsNotUsedAsValues = TSImportsNotUsedAsValues::kError;
    derived.Target = TSTarget::kBelowES2022;
    TSConfig base;
    TSConfigApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.ExperimentalDecorators, MaybeBool::kTrue);
    EXPECT_EQ(derived.ImportsNotUsedAsValues, TSImportsNotUsedAsValues::kError);
    EXPECT_EQ(derived.Target, TSTarget::kBelowES2022);
}

// ---------------------------------------------------------------------------
// TSConfigUnusedImportFlags
// ---------------------------------------------------------------------------

TEST(TSConfigUnusedImportFlagsTest, VerbatimModuleSyntaxTrue)
{
    TSConfig cfg;
    cfg.VerbatimModuleSyntax = MaybeBool::kTrue;
    auto flags = TSConfigUnusedImportFlags(cfg);
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepStmt));
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepValues));
}

TEST(TSConfigUnusedImportFlagsTest, PreserveValueImportsTrue)
{
    TSConfig cfg;
    cfg.PreserveValueImports = MaybeBool::kTrue;
    auto flags = TSConfigUnusedImportFlags(cfg);
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepValues));
    EXPECT_FALSE(Has(flags, TSUnusedImportFlags::kKeepStmt));
}

TEST(TSConfigUnusedImportFlagsTest, ImportsNotUsedAsValuesPreserve)
{
    TSConfig cfg;
    cfg.ImportsNotUsedAsValues = TSImportsNotUsedAsValues::kPreserve;
    auto flags = TSConfigUnusedImportFlags(cfg);
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepStmt));
    EXPECT_FALSE(Has(flags, TSUnusedImportFlags::kKeepValues));
}

TEST(TSConfigUnusedImportFlagsTest, ImportsNotUsedAsValuesError)
{
    TSConfig cfg;
    cfg.ImportsNotUsedAsValues = TSImportsNotUsedAsValues::kError;
    auto flags = TSConfigUnusedImportFlags(cfg);
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepStmt));
}

TEST(TSConfigUnusedImportFlagsTest, AllDefaultsReturnsNone)
{
    TSConfig cfg;
    auto flags = TSConfigUnusedImportFlags(cfg);
    EXPECT_FALSE(Has(flags, TSUnusedImportFlags::kKeepStmt));
    EXPECT_FALSE(Has(flags, TSUnusedImportFlags::kKeepValues));
}

TEST(TSConfigUnusedImportFlagsTest, BothPreserve)
{
    TSConfig cfg;
    cfg.PreserveValueImports = MaybeBool::kTrue;
    cfg.ImportsNotUsedAsValues = TSImportsNotUsedAsValues::kPreserve;
    auto flags = TSConfigUnusedImportFlags(cfg);
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepStmt));
    EXPECT_TRUE(Has(flags, TSUnusedImportFlags::kKeepValues));
}

// ---------------------------------------------------------------------------
// TSConfigJSXApplyExtendedConfig
// ---------------------------------------------------------------------------

TEST(TSConfigJSXApplyExtendedConfigTest, BaseFactoryOverridesEmpty)
{
    TSConfigJSX derived;
    TSConfigJSX base;
    base.JSXFactory = {"h"};
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSXFactory.size(), 1u);
    EXPECT_EQ(derived.JSXFactory[0], "h");
}

TEST(TSConfigJSXApplyExtendedConfigTest, BaseFactoryOverwritesDerived)
{
    TSConfigJSX derived;
    derived.JSXFactory = {"React.createElement"};
    TSConfigJSX base;
    base.JSXFactory = {"h"};
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSXFactory.size(), 1u);
    EXPECT_EQ(derived.JSXFactory[0], "h");
}

TEST(TSConfigJSXApplyExtendedConfigTest, EmptyBaseLeavesDerivedFactory)
{
    TSConfigJSX derived;
    derived.JSXFactory = {"React.createElement"};
    TSConfigJSX base;
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSXFactory.size(), 1u);
    EXPECT_EQ(derived.JSXFactory[0], "React.createElement");
}

TEST(TSConfigJSXApplyExtendedConfigTest, BaseFragmentOverridesEmpty)
{
    TSConfigJSX derived;
    TSConfigJSX base;
    base.JSXFragmentFactory = {"React.Fragment"};
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSXFragmentFactory.size(), 1u);
    EXPECT_EQ(derived.JSXFragmentFactory[0], "React.Fragment");
}

TEST(TSConfigJSXApplyExtendedConfigTest, BaseImportSourceOverridesEmpty)
{
    TSConfigJSX derived;
    TSConfigJSX base;
    base.JSXImportSource = "preact";
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_TRUE(derived.JSXImportSource.has_value());
    EXPECT_EQ(derived.JSXImportSource.value(), "preact");
}

TEST(TSConfigJSXApplyExtendedConfigTest, BaseImportSourceOverwritesDerived)
{
    TSConfigJSX derived;
    derived.JSXImportSource = "react";
    TSConfigJSX base;
    base.JSXImportSource = "preact";
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSXImportSource.value(), "preact");
}

TEST(TSConfigJSXApplyExtendedConfigTest, EmptyBaseLeavesDerivedImportSource)
{
    TSConfigJSX derived;
    derived.JSXImportSource = "react";
    TSConfigJSX base;
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSXImportSource.value(), "react");
}

TEST(TSConfigJSXApplyExtendedConfigTest, BaseJSXModeOverwritesDerived)
{
    TSConfigJSX derived;
    derived.JSX = TSJSX::kReact;
    TSConfigJSX base;
    base.JSX = TSJSX::kReactJSX;
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSX, TSJSX::kReactJSX);
}

TEST(TSConfigJSXApplyExtendedConfigTest, EmptyBaseLeavesDerivedJSXMode)
{
    TSConfigJSX derived;
    derived.JSX = TSJSX::kReact;
    TSConfigJSX base;
    TSConfigJSXApplyExtendedConfig(derived, base);
    EXPECT_EQ(derived.JSX, TSJSX::kReact);
}

// ---------------------------------------------------------------------------
// TSConfigJSXApplyTo
// ---------------------------------------------------------------------------

TEST(TSConfigJSXApplyToTest, ReactEnablesClassicRuntime)
{
    TSConfigJSX tsConfig;
    tsConfig.JSX = TSJSX::kReact;
    JSXOptions jsxOptions;
    TSConfigJSXApplyTo(tsConfig, jsxOptions);
    EXPECT_FALSE(jsxOptions.AutomaticRuntime);
    EXPECT_FALSE(jsxOptions.Development);
}

TEST(TSConfigJSXApplyToTest, ReactJSXEnablesAutomaticRuntime)
{
    TSConfigJSX tsConfig;
    tsConfig.JSX = TSJSX::kReactJSX;
    JSXOptions jsxOptions;
    TSConfigJSXApplyTo(tsConfig, jsxOptions);
    EXPECT_TRUE(jsxOptions.AutomaticRuntime);
    EXPECT_FALSE(jsxOptions.Development);
}

TEST(TSConfigJSXApplyToTest, ReactJSXDevEnablesDevelopmentMode)
{
    TSConfigJSX tsConfig;
    tsConfig.JSX = TSJSX::kReactJSXDev;
    JSXOptions jsxOptions;
    TSConfigJSXApplyTo(tsConfig, jsxOptions);
    EXPECT_TRUE(jsxOptions.AutomaticRuntime);
    EXPECT_TRUE(jsxOptions.Development);
}

TEST(TSConfigJSXApplyToTest, PreserveDoesNotChangeOptions)
{
    TSConfigJSX tsConfig;
    tsConfig.JSX = TSJSX::kPreserve;
    JSXOptions jsxOptions;
    TSConfigJSXApplyTo(tsConfig, jsxOptions);
    EXPECT_FALSE(jsxOptions.AutomaticRuntime);
}

TEST(TSConfigJSXApplyToTest, FactoryOverrideApplied)
{
    TSConfigJSX tsConfig;
    tsConfig.JSX = TSJSX::kReact;
    tsConfig.JSXFactory = {"h"};
    JSXOptions jsxOptions;
    TSConfigJSXApplyTo(tsConfig, jsxOptions);
    EXPECT_EQ(jsxOptions.Factory.Parts.size(), 1u);
    EXPECT_EQ(jsxOptions.Factory.Parts[0], "h");
}

TEST(TSConfigJSXApplyToTest, FragmentOverrideApplied)
{
    TSConfigJSX tsConfig;
    tsConfig.JSX = TSJSX::kReact;
    tsConfig.JSXFragmentFactory = {"Fragment"};
    JSXOptions jsxOptions;
    TSConfigJSXApplyTo(tsConfig, jsxOptions);
    EXPECT_EQ(jsxOptions.Fragment.Parts.size(), 1u);
    EXPECT_EQ(jsxOptions.Fragment.Parts[0], "Fragment");
}

TEST(TSConfigJSXApplyToTest, ImportSourceApplied)
{
    TSConfigJSX tsConfig;
    tsConfig.JSX = TSJSX::kReactJSX;
    tsConfig.JSXImportSource = "preact";
    JSXOptions jsxOptions;
    TSConfigJSXApplyTo(tsConfig, jsxOptions);
    EXPECT_EQ(jsxOptions.ImportSource, "preact");
}

// ---------------------------------------------------------------------------
// PathPlaceholder
// ---------------------------------------------------------------------------

TEST(PathPlaceholderTest, GetReturnsDirPointer)
{
    std::string dir = "dist";
    std::string name = "index";
    std::string hash = "abc123";
    std::string ext = "js";
    PathPlaceholders p{&dir, &name, &hash, &ext};
    EXPECT_EQ(p.Get(PathPlaceholder::kDir), &dir);
    EXPECT_EQ(p.Get(PathPlaceholder::kName), &name);
    EXPECT_EQ(p.Get(PathPlaceholder::kHash), &hash);
    EXPECT_EQ(p.Get(PathPlaceholder::kExt), &ext);
}

TEST(PathPlaceholderTest, GetReturnsNullptrForUnknown)
{
    PathPlaceholders p;
    EXPECT_EQ(p.Get(PathPlaceholder::kNoPlaceholder), nullptr);
}

// ---------------------------------------------------------------------------
// TemplateToString
// ---------------------------------------------------------------------------

TEST(TemplateToStringTest, SimpleLiteral)
{
    std::vector<PathTemplate> tmpl = {{"bundle.js"}};
    EXPECT_EQ(TemplateToString(tmpl), "bundle.js");
}

TEST(TemplateToStringTest, NamePlaceholder)
{
    std::vector<PathTemplate> tmpl = {
        {"", PathPlaceholder::kName},
        {".js"}
    };
    EXPECT_EQ(TemplateToString(tmpl), "[name].js");
}

TEST(TemplateToStringTest, MultiplePlaceholders)
{
    std::vector<PathTemplate> tmpl = {
        {"", PathPlaceholder::kName},
        {"-", PathPlaceholder::kNoPlaceholder},
        {"", PathPlaceholder::kHash},
        {".js"}
    };
    EXPECT_EQ(TemplateToString(tmpl), "[name]-[hash].js");
}

TEST(TemplateToStringTest, DirPrefix)
{
    std::vector<PathTemplate> tmpl = {
        {"dist/"},
        {"", PathPlaceholder::kName},
        {".js"}
    };
    EXPECT_EQ(TemplateToString(tmpl), "dist/[name].js");
}

// ---------------------------------------------------------------------------
// HasPlaceholder
// ---------------------------------------------------------------------------

TEST(HasPlaceholderTest, FindsName)
{
    std::vector<PathTemplate> tmpl = {
        {"", PathPlaceholder::kName},
        {".js"}
    };
    EXPECT_TRUE(HasPlaceholder(tmpl, PathPlaceholder::kName));
}

TEST(HasPlaceholderTest, FindsHash)
{
    std::vector<PathTemplate> tmpl = {
        {"", PathPlaceholder::kName},
        {"-"},
        {"", PathPlaceholder::kHash},
        {".js"}
    };
    EXPECT_TRUE(HasPlaceholder(tmpl, PathPlaceholder::kHash));
}

TEST(HasPlaceholderTest, ReturnsFalseWhenAbsent)
{
    std::vector<PathTemplate> tmpl = {{"bundle.js"}};
    EXPECT_FALSE(HasPlaceholder(tmpl, PathPlaceholder::kName));
}

// ---------------------------------------------------------------------------
// SubstituteTemplate
// ---------------------------------------------------------------------------

TEST(SubstituteTemplateTest, SubstitutesName)
{
    std::string name = "index";
    PathPlaceholders ph{nullptr, &name, nullptr, nullptr};
    std::vector<PathTemplate> tmpl = {
        {"", PathPlaceholder::kName},
        {".js"}
    };
    auto result = SubstituteTemplate(tmpl, ph);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].Data, "index.js");
    EXPECT_EQ(result[0].Placeholder, PathPlaceholder::kNoPlaceholder);
}

TEST(SubstituteTemplateTest, SubstitutesNameAndHash)
{
    std::string name = "index";
    std::string hash = "abc123";
    PathPlaceholders ph{nullptr, &name, &hash, nullptr};
    std::vector<PathTemplate> tmpl = {
        {"", PathPlaceholder::kName},
        {"-"},
        {"", PathPlaceholder::kHash},
        {".js"}
    };
    auto result = SubstituteTemplate(tmpl, ph);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].Data, "index-abc123.js");
}

TEST(SubstituteTemplateTest, NoSubstitutionNeeded)
{
    std::vector<PathTemplate> tmpl = {{"bundle.js"}};
    auto result = SubstituteTemplate(tmpl, PathPlaceholders{});
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].Data, "bundle.js");
}

TEST(SubstituteTemplateTest, NullPlaceholderLeftAsIs)
{
    std::vector<PathTemplate> tmpl = {
        {"", PathPlaceholder::kName},
        {".js"}
    };
    auto result = SubstituteTemplate(tmpl, PathPlaceholders{});
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].Placeholder, PathPlaceholder::kName);
    EXPECT_EQ(result[0].Data, "");
    EXPECT_EQ(result[1].Data, ".js");
}

// ---------------------------------------------------------------------------
// ExternalMatchers
// ---------------------------------------------------------------------------

TEST(ExternalMatchersTest, EmptyHasNoMatchers)
{
    ExternalMatchers m;
    EXPECT_FALSE(m.HasMatchers());
}

TEST(ExternalMatchersTest, ExactMatchHasMatchers)
{
    ExternalMatchers m;
    m.Exact["react"] = true;
    EXPECT_TRUE(m.HasMatchers());
}

TEST(ExternalMatchersTest, PatternHasMatchers)
{
    ExternalMatchers m;
    m.Patterns.push_back({"lodash/", ""});
    EXPECT_TRUE(m.HasMatchers());
}

// ---------------------------------------------------------------------------
// PathPlaceholders defaults
// ---------------------------------------------------------------------------

TEST(PathPlaceholdersTest, NullPointersByDefault)
{
    PathPlaceholders p;
    EXPECT_EQ(p.Dir, nullptr);
    EXPECT_EQ(p.Name, nullptr);
    EXPECT_EQ(p.Hash, nullptr);
    EXPECT_EQ(p.Ext, nullptr);
}

// ---------------------------------------------------------------------------
// DefineExpr
// ---------------------------------------------------------------------------

TEST(DefineExprTest, EmptyExprHasNoConstant)
{
    DefineExpr expr;
    EXPECT_FALSE(expr.HasConstant());
}

TEST(DefineExprTest, PartsOnlyHasNoConstant)
{
    DefineExpr expr;
    expr.Parts = {"process", "env"};
    EXPECT_FALSE(expr.HasConstant());
}

// ---------------------------------------------------------------------------
// StdinInfo defaults
// ---------------------------------------------------------------------------

TEST(StdinInfoTest, DefaultLoaderIsNone)
{
    StdinInfo info;
    EXPECT_EQ(info.Ldr, Loader::kNone);
}

// ---------------------------------------------------------------------------
// EntryPoint defaults
// ---------------------------------------------------------------------------

TEST(EntryPointTest, InputPathInFileNamespaceFalseByDefault)
{
    EntryPoint ep;
    EXPECT_FALSE(ep.InputPathInFileNamespace);
}

// ---------------------------------------------------------------------------
// CancelFlag
// ---------------------------------------------------------------------------

TEST(CancelFlagTest, NotCancelledByDefault)
{
    CancelFlag flag;
    EXPECT_FALSE(flag.DidCancel());
}

TEST(CancelFlagTest, CancelSetsFlag)
{
    CancelFlag flag;
    flag.Cancel();
    EXPECT_TRUE(flag.DidCancel());
}

// ---------------------------------------------------------------------------
// ResourceHintsConfig defaults
// ---------------------------------------------------------------------------

TEST(ResourceHintsConfigTest, AllDisabledByDefault)
{
    ResourceHintsConfig cfg;
    EXPECT_FALSE(cfg.enabled);
    EXPECT_FALSE(cfg.preload);
    EXPECT_FALSE(cfg.prefetch);
    EXPECT_FALSE(cfg.preconnect);
    EXPECT_FALSE(cfg.dns_prefetch);
    EXPECT_FALSE(cfg.fonts);
}

// ---------------------------------------------------------------------------
// Options defaults
// ---------------------------------------------------------------------------

TEST(OptionsTest, DefaultModeIsPassThrough)
{
    Options opts;
    EXPECT_EQ(opts.BuildMode, Mode::kPassThrough);
}

TEST(OptionsTest, DefaultFormatIsPreserve)
{
    Options opts;
    EXPECT_EQ(opts.OutputFormat, Format::kPreserve);
}

TEST(OptionsTest, DefaultPlatformIsBrowser)
{
    Options opts;
    EXPECT_EQ(opts.OutputPlatform, Platform::kBrowser);
}

TEST(OptionsTest, DefaultSourceMapIsNone)
{
    Options opts;
    EXPECT_EQ(opts.SourceMapData, SourceMap::kNone);
}

TEST(OptionsTest, DefaultMinifyWhitespaceIsFalse)
{
    Options opts;
    EXPECT_FALSE(opts.MinifyWhitespace);
}

TEST(OptionsTest, DefaultMinifyIdentifiersIsFalse)
{
    Options opts;
    EXPECT_FALSE(opts.MinifyIdentifiers);
}

TEST(OptionsTest, DefaultMinifySyntaxIsFalse)
{
    Options opts;
    EXPECT_FALSE(opts.MinifySyntax);
}

TEST(OptionsTest, DefaultTreeShakingIsFalse)
{
    Options opts;
    EXPECT_FALSE(opts.TreeShaking);
}

TEST(OptionsTest, DefaultStrictIsTrue)
{
    Options opts;
    EXPECT_TRUE(opts.Strict);
}

TEST(OptionsTest, DefaultCodeSplittingIsFalse)
{
    Options opts;
    EXPECT_FALSE(opts.CodeSplitting);
}

TEST(OptionsTest, DefaultPrettyPrintIsFalse)
{
    Options opts;
    EXPECT_FALSE(opts.PrettyPrint);
}

// ---------------------------------------------------------------------------
// Format enum values
// ---------------------------------------------------------------------------

TEST(FormatTest, Count)
{
    EXPECT_EQ(static_cast<uint8_t>(Format::kSystem), 6);
}

// ---------------------------------------------------------------------------
// Platform enum
// ---------------------------------------------------------------------------

TEST(PlatformTest, BrowserBeforeNode)
{
    EXPECT_LT(static_cast<uint8_t>(Platform::kBrowser), static_cast<uint8_t>(Platform::kNode));
}

TEST(PlatformTest, NodeBeforeNeutral)
{
    EXPECT_LT(static_cast<uint8_t>(Platform::kNode), static_cast<uint8_t>(Platform::kNeutral));
}

// ---------------------------------------------------------------------------
// SourceMap enum
// ---------------------------------------------------------------------------

TEST(SourceMapTest, KInlineAndExternalIsLast)
{
    EXPECT_EQ(static_cast<uint8_t>(SourceMap::kInlineAndExternal), 4);
}

// ---------------------------------------------------------------------------
// LegalComments enum
// ---------------------------------------------------------------------------

TEST(LegalCommentsTest, InlineIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(LegalComments::kInline), 0);
}

// ---------------------------------------------------------------------------
// Mode enum
// ---------------------------------------------------------------------------

TEST(ModeTest, PassThroughIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(Mode::kPassThrough), 0);
}

TEST(ModeTest, BundleIsTwo)
{
    EXPECT_EQ(static_cast<uint8_t>(Mode::kBundle), 2);
}

// ---------------------------------------------------------------------------
// TSJSX enum
// ---------------------------------------------------------------------------

TEST(TSJSXTest, NoneIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(TSJSX::kNone), 0);
}

TEST(TSJSXTest, ReactJSXIsFour)
{
    EXPECT_EQ(static_cast<uint8_t>(TSJSX::kReactJSX), 4);
}

// ---------------------------------------------------------------------------
// TSTarget enum
// ---------------------------------------------------------------------------

TEST(TSTargetTest, UnspecifiedIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(TSTarget::kUnspecified), 0);
}

// ---------------------------------------------------------------------------
// TSImportsNotUsedAsValues enum
// ---------------------------------------------------------------------------

TEST(TSImportsNotUsedAsValuesTest, NoneIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(TSImportsNotUsedAsValues::kNone), 0);
}

TEST(TSImportsNotUsedAsValuesTest, ErrorIsThree)
{
    EXPECT_EQ(static_cast<uint8_t>(TSImportsNotUsedAsValues::kError), 3);
}

// ---------------------------------------------------------------------------
// APICall enum
// ---------------------------------------------------------------------------

TEST(APICallTest, BuildCallIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(APICall::kBuildCall), 0);
}

TEST(APICallTest, TransformCallIsOne)
{
    EXPECT_EQ(static_cast<uint8_t>(APICall::kTransformCall), 1);
}

// ---------------------------------------------------------------------------
// MetafileFormat enum
// ---------------------------------------------------------------------------

TEST(MetafileFormatTest, UnminifiedIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(MetafileFormat::kUnminified), 0);
}

TEST(MetafileFormatTest, MinifiedIsOne)
{
    EXPECT_EQ(static_cast<uint8_t>(MetafileFormat::kMinified), 1);
}

// ---------------------------------------------------------------------------
// CSSLoadingStrategy enum
// ---------------------------------------------------------------------------

TEST(CSSLoadingStrategyTest, BlockingIsZero)
{
    EXPECT_EQ(static_cast<uint8_t>(CSSLoadingStrategy::kBlocking), 0);
}

TEST(CSSLoadingStrategyTest, NonBlockingIsOne)
{
    EXPECT_EQ(static_cast<uint8_t>(CSSLoadingStrategy::kNonBlocking), 1);
}

TEST(CSSLoadingStrategyTest, MediaSplitIsTwo)
{
    EXPECT_EQ(static_cast<uint8_t>(CSSLoadingStrategy::kMediaSplit), 2);
}
