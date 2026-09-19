#include "test/guchho_test.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"

#include <string>
#include <unordered_map>
#include <vector>

using namespace guchho::compat;
using namespace guchho::css;

// ===========================================================================
// IsBrowser
// ===========================================================================

TEST(IsBrowserTest, BrowserEngines)
{
    EXPECT_TRUE(IsBrowser(Engine::kChrome));
    EXPECT_TRUE(IsBrowser(Engine::kEdge));
    EXPECT_TRUE(IsBrowser(Engine::kFirefox));
    EXPECT_TRUE(IsBrowser(Engine::kIE));
    EXPECT_TRUE(IsBrowser(Engine::kIOS));
    EXPECT_TRUE(IsBrowser(Engine::kOpera));
    EXPECT_TRUE(IsBrowser(Engine::kSafari));
}

TEST(IsBrowserTest, RuntimeEngines)
{
    EXPECT_FALSE(IsBrowser(Engine::kDeno));
    EXPECT_FALSE(IsBrowser(Engine::kES));
    EXPECT_FALSE(IsBrowser(Engine::kHermes));
    EXPECT_FALSE(IsBrowser(Engine::kNode));
    EXPECT_FALSE(IsBrowser(Engine::kRhino));
}

// ===========================================================================
// Version
// ===========================================================================

TEST(VersionTest, DefaultIsAllZeros)
{
    Version v{};
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST(VersionTest, EqualComponents)
{
    EXPECT_EQ((Version{120, 0, 0}), (Version{120, 0, 0}));
    EXPECT_NE((Version{120, 0, 0}), (Version{120, 1, 0}));
    EXPECT_NE((Version{120, 0, 0}), (Version{121, 0, 0}));
    EXPECT_NE((Version{120, 0, 1}), (Version{120, 0, 0}));
}

// ===========================================================================
// Semver
// ===========================================================================

TEST(SemverToStringTest, FullVersion)
{
    Semver s;
    s.parts = {1, 2, 3};
    EXPECT_EQ(s.ToString(), "1.2.3");
}

TEST(SemverToStringTest, WithPreRelease)
{
    Semver s;
    s.parts = {1, 2, 3};
    s.pre_release = "-beta.1";
    EXPECT_EQ(s.ToString(), "1.2.3-beta.1");
}

TEST(SemverToStringTest, EmptyParts)
{
    Semver s;
    EXPECT_EQ(s.ToString(), "");
    s.pre_release = "-rc";
    EXPECT_EQ(s.ToString(), "-rc");
}

TEST(SemverToStringTest, SinglePart)
{
    Semver s;
    s.parts = {2015};
    EXPECT_EQ(s.ToString(), "2015");
    s.parts = {0, 1, 0};
    EXPECT_EQ(s.ToString(), "0.1.0");
}

// ===========================================================================
// CompareVersions
// ===========================================================================

TEST(CompareVersionsTest, EqualVersions)
{
    EXPECT_EQ((CompareVersions(Version{1, 2, 3}, Semver{.parts = {1, 2, 3}, .pre_release = {}})), 0);
}

TEST(CompareVersionsTest, NewerVersion)
{
    EXPECT_GT((CompareVersions(Version{1, 2, 4}, Semver{.parts = {1, 2, 3}, .pre_release = {}})), 0);
    EXPECT_GT((CompareVersions(Version{2, 0, 0}, Semver{.parts = {1, 9, 9}, .pre_release = {}})), 0);
    EXPECT_GT((CompareVersions(Version{1, 2, 0}, Semver{.parts = {1, 1, 5}, .pre_release = {}})), 0);
}

TEST(CompareVersionsTest, OlderVersion)
{
    EXPECT_LT((CompareVersions(Version{1, 2, 3}, Semver{.parts = {1, 2, 4}, .pre_release = {}})), 0);
    EXPECT_LT((CompareVersions(Version{1, 0, 0}, Semver{.parts = {2, 0, 0}, .pre_release = {}})), 0);
}

TEST(CompareVersionsTest, MissingPartsTreatedAsZero)
{
    EXPECT_EQ((CompareVersions(Version{0, 0, 0}, Semver{.parts = {}, .pre_release = {}})), 0);
    EXPECT_GT((CompareVersions(Version{1, 0, 0}, Semver{.parts = {}, .pre_release = {}})), 0);
    EXPECT_GT((CompareVersions(Version{1, 0, 0}, Semver{.parts = {0, 9}, .pre_release = {}})), 0);
}

TEST(CompareVersionsTest, PreReleaseLosesTieBreak)
{
    EXPECT_EQ((CompareVersions(Version{1, 0, 0}, Semver{.parts = {1, 0, 0}, .pre_release = "alpha"})), 1);
}

// ===========================================================================
// IsVersionSupported
// ===========================================================================

TEST(IsVersionSupportedTest, InsideRange)
{
    const std::vector<VersionRange> ranges = {
        {Version{1, 0, 0}, Version{2, 0, 0}},
    };
    EXPECT_TRUE((IsVersionSupported(ranges, Semver{.parts = {1, 5, 0}, .pre_release = {}})));
    EXPECT_TRUE((IsVersionSupported(ranges, Semver{.parts = {1, 0, 0}, .pre_release = {}})));
}

TEST(IsVersionSupportedTest, ExclusiveUpperBound)
{
    const std::vector<VersionRange> ranges = {
        {Version{1, 0, 0}, Version{2, 0, 0}},
    };
    EXPECT_FALSE((IsVersionSupported(ranges, Semver{.parts = {2, 0, 0}, .pre_release = {}})));
    EXPECT_FALSE((IsVersionSupported(ranges, Semver{.parts = {0, 9, 0}, .pre_release = {}})));
}

TEST(IsVersionSupportedTest, UnboundedRange)
{
    const std::vector<VersionRange> ranges = {
        {Version{1, 0, 0}, Version{}},
    };
    EXPECT_TRUE((IsVersionSupported(ranges, Semver{.parts = {99, 0, 0}, .pre_release = {}})));
    EXPECT_FALSE((IsVersionSupported(ranges, Semver{.parts = {0, 9, 0}, .pre_release = {}})));
}

TEST(IsVersionSupportedTest, EmptyRanges)
{
    EXPECT_FALSE((IsVersionSupported({}, Semver{.parts = {1, 0, 0}, .pre_release = {}})));
}

TEST(IsVersionSupportedTest, MultipleRangesShortCircuit)
{
    const std::vector<VersionRange> ranges = {
        {Version{3, 0, 0}, Version{4, 0, 0}},
        {Version{1, 0, 0}, Version{2, 0, 0}},
    };
    EXPECT_TRUE((IsVersionSupported(ranges, Semver{.parts = {3, 5, 0}, .pre_release = {}})));
    EXPECT_TRUE((IsVersionSupported(ranges, Semver{.parts = {1, 5, 0}, .pre_release = {}})));
    EXPECT_FALSE((IsVersionSupported(ranges, Semver{.parts = {2, 5, 0}, .pre_release = {}})));
}

TEST(IsVersionSupportedTest, PreReleaseBelowRangeStart)
{
    const std::vector<VersionRange> ranges = {
        {Version{1, 0, 0}, Version{}},
    };
    EXPECT_FALSE((IsVersionSupported(ranges, Semver{.parts = {1, 0, 0}, .pre_release = "alpha"})));
}

// ===========================================================================
// JSFeature bitmask helpers
// ===========================================================================

TEST(JSFeatureTest, CombineAndTest)
{
    JSFeature f = JSFeature::kArrow | JSFeature::kAsyncAwait;
    EXPECT_TRUE(Has(f, JSFeature::kArrow));
    EXPECT_TRUE(Has(f, JSFeature::kAsyncAwait));
    EXPECT_FALSE(Has(f, JSFeature::kClass));
    EXPECT_EQ(f, JSFeature::kArrow | JSFeature::kAsyncAwait);
}

TEST(JSFeatureTest, BitwiseAnd)
{
    EXPECT_EQ((JSFeature::kArrow | JSFeature::kClass) & JSFeature::kArrow, JSFeature::kArrow);
}

TEST(JSFeatureTest, BitwiseNot)
{
    EXPECT_EQ((JSFeature::kArrow | JSFeature::kAsyncAwait) & ~JSFeature::kArrow, JSFeature::kAsyncAwait);
}

TEST(JSFeatureTest, OrAssign)
{
    JSFeature f = JSFeature::kArrow;
    f |= JSFeature::kAsyncAwait;
    EXPECT_TRUE(Has(f, JSFeature::kArrow));
    EXPECT_TRUE(Has(f, JSFeature::kAsyncAwait));
}

TEST(JSFeatureTest, HasRequiresAllRequestedBits)
{
    JSFeature f = JSFeature::kArrow | JSFeature::kClass;
    EXPECT_TRUE(Has(f, f));
    EXPECT_TRUE(Has(f, JSFeature::kArrow | JSFeature::kClass));
}

TEST(JSFeatureTest, Ordering)
{
    EXPECT_TRUE(JSFeature::kArrow < JSFeature::kAsyncAwait);
    EXPECT_TRUE(JSFeature::kClass < JSFeature::kClassField);
    EXPECT_FALSE(JSFeature::kAsyncAwait < JSFeature::kArrow);
    EXPECT_FALSE(JSFeature::kArrow < JSFeature::kArrow);
}

TEST(JSFeatureTest, ApplyOverrides)
{
    JSFeature features = JSFeature::kArrow | JSFeature::kClass;
    JSFeature overrides = JSFeature::kAsyncAwait | JSFeature::kClass;
    JSFeature mask = JSFeature::kClass;
    EXPECT_EQ(ApplyOverrides(features, overrides, mask), JSFeature::kArrow | JSFeature::kClass);
}

TEST(JSFeatureTest, ApplyOverridesZeroMask)
{
    JSFeature features = JSFeature::kArrow;
    EXPECT_EQ(ApplyOverrides(features, JSFeature::kClass, static_cast<JSFeature>(0)), features);
}

// ===========================================================================
// CSSFeature bitmask helpers
// ===========================================================================

TEST(CSSFeatureTest, CombineAndTest)
{
    CSSFeature f = CSSFeature::kNesting | CSSFeature::kHWB;
    EXPECT_TRUE(Has(f, CSSFeature::kNesting));
    EXPECT_TRUE(Has(f, CSSFeature::kHWB));
    EXPECT_FALSE(Has(f, CSSFeature::kHexRGBA));
}

TEST(CSSFeatureTest, BitwiseAndNot)
{
    CSSFeature f = CSSFeature::kNesting | CSSFeature::kHWB;
    EXPECT_EQ(f & ~CSSFeature::kNesting, CSSFeature::kHWB);
    EXPECT_EQ((CSSFeature::kNesting | CSSFeature::kHexRGBA) & CSSFeature::kNesting, CSSFeature::kNesting);
}

TEST(CSSFeatureTest, OrAssign)
{
    CSSFeature f = CSSFeature::kNesting;
    f |= CSSFeature::kHWB;
    EXPECT_TRUE(Has(f, CSSFeature::kNesting));
    EXPECT_TRUE(Has(f, CSSFeature::kHWB));
}

TEST(CSSFeatureTest, ApplyOverrides)
{
    CSSFeature features = CSSFeature::kNesting | CSSFeature::kHWB;
    CSSFeature overrides = CSSFeature::kHexRGBA | CSSFeature::kHWB;
    CSSFeature mask = CSSFeature::kHWB;
    EXPECT_EQ(ApplyOverrides(features, overrides, mask), CSSFeature::kNesting | CSSFeature::kHWB);
    EXPECT_EQ(ApplyOverrides(features, overrides, static_cast<CSSFeature>(0)), features);
}

// ===========================================================================
// CSSPrefix bitmask helpers
// ===========================================================================

TEST(CSSPrefixTest, CombineAndTest)
{
    CSSPrefix p = CSSPrefix::kWebkitPrefix | CSSPrefix::kMozPrefix;
    EXPECT_TRUE(Has(p, CSSPrefix::kWebkitPrefix));
    EXPECT_TRUE(Has(p, CSSPrefix::kMozPrefix));
    EXPECT_FALSE(Has(p, CSSPrefix::kMsPrefix));
    EXPECT_TRUE(Has(p, CSSPrefix::kWebkitPrefix | CSSPrefix::kMozPrefix));
}

TEST(CSSPrefixTest, NoPrefixNeverPresent)
{
    CSSPrefix p = CSSPrefix::kWebkitPrefix;
    EXPECT_FALSE(Has(p, CSSPrefix::kNoPrefix));
    EXPECT_EQ(p, p | CSSPrefix::kNoPrefix);
}

TEST(CSSPrefixTest, BitwiseOperations)
{
    CSSPrefix p = CSSPrefix::kWebkitPrefix | CSSPrefix::kMozPrefix;
    EXPECT_EQ(p & ~CSSPrefix::kWebkitPrefix, CSSPrefix::kMozPrefix);
    EXPECT_EQ((CSSPrefix::kWebkitPrefix | CSSPrefix::kMsPrefix) & CSSPrefix::kMsPrefix, CSSPrefix::kMsPrefix);
    p |= CSSPrefix::kMsPrefix;
    EXPECT_TRUE(Has(p, CSSPrefix::kMsPrefix));
}

// ===========================================================================
// SymbolFeature
// ===========================================================================

TEST(SymbolFeatureTest, InstancePrivateMembers)
{
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateField), JSFeature::kClassPrivateField);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateMethod), JSFeature::kClassPrivateMethod);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateGet), JSFeature::kClassPrivateAccessor);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateSet), JSFeature::kClassPrivateAccessor);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateGetSetPair), JSFeature::kClassPrivateAccessor);
}

TEST(SymbolFeatureTest, StaticPrivateMembers)
{
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateStaticField), JSFeature::kClassPrivateStaticField);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateStaticMethod), JSFeature::kClassPrivateStaticMethod);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateStaticGet), JSFeature::kClassPrivateStaticAccessor);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateStaticSet), JSFeature::kClassPrivateStaticAccessor);
    EXPECT_EQ(SymbolFeature(guchho::compiler::SymbolKind::kPrivateStaticGetSetPair), JSFeature::kClassPrivateStaticAccessor);
}

TEST(SymbolFeatureTest, NonPrivateReturnsNoFeature)
{
    EXPECT_FALSE(Has(SymbolFeature(guchho::compiler::SymbolKind::kHoisted), JSFeature::kArrow));
    EXPECT_FALSE(Has(SymbolFeature(guchho::compiler::SymbolKind::kUnbound), JSFeature::kClass));
    EXPECT_FALSE(Has(SymbolFeature(guchho::compiler::SymbolKind::kClass), JSFeature::kClass));
}

// ===========================================================================
// StringToJSFeature
// ===========================================================================

TEST(StringToJSFeatureTest, Lookup)
{
    EXPECT_EQ(StringToJSFeature.at("arrow"), JSFeature::kArrow);
    EXPECT_EQ(StringToJSFeature.at("async-await"), JSFeature::kAsyncAwait);
    EXPECT_EQ(StringToJSFeature.at("class-private-static-field"), JSFeature::kClassPrivateStaticField);
    EXPECT_EQ(StringToJSFeature.at("using"), JSFeature::kUsing);
    EXPECT_EQ(StringToJSFeature.at("inline-script"), JSFeature::kInlineScript);
    EXPECT_EQ(StringToJSFeature.at("arbitrary-module-namespace-names"), JSFeature::kArbitraryModuleNamespaceNames);
}

// ===========================================================================
// StringToCSSFeature
// ===========================================================================

TEST(StringToCSSFeatureTest, Lookup)
{
    EXPECT_EQ(StringToCSSFeature.at("nesting"), CSSFeature::kNesting);
    EXPECT_EQ(StringToCSSFeature.at("hwb"), CSSFeature::kHWB);
    EXPECT_EQ(StringToCSSFeature.at("media-range"), CSSFeature::kMediaRange);
    EXPECT_EQ(StringToCSSFeature.at("color-functions"), CSSFeature::kColorFunctions);
    EXPECT_EQ(StringToCSSFeature.at("hex-rgba"), CSSFeature::kHexRGBA);
}

// ===========================================================================
// UnsupportedJSFeatures
// ===========================================================================

TEST(UnsupportedJSFeaturesTest, EmptyConstraints)
{
    EXPECT_EQ(UnsupportedJSFeatures({}), static_cast<JSFeature>(0));
}

TEST(UnsupportedJSFeaturesTest, VeryOldChrome)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kChrome, Semver{.parts = {48, 0, 0}, .pre_release = {}}},
    };
    JSFeature unsupported = UnsupportedJSFeatures(constraints);
    EXPECT_TRUE(Has(unsupported, JSFeature::kArrow));
    EXPECT_TRUE(Has(unsupported, JSFeature::kUsing));
    EXPECT_FALSE(Has(unsupported, JSFeature::kInlineScript));
}

TEST(UnsupportedJSFeaturesTest, ModernChrome)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kChrome, Semver{.parts = {140, 0, 0}, .pre_release = {}}},
    };
    JSFeature unsupported = UnsupportedJSFeatures(constraints);
    EXPECT_FALSE(Has(unsupported, JSFeature::kArrow));
    EXPECT_TRUE(Has(unsupported, JSFeature::kUsing));
}

TEST(UnsupportedJSFeaturesTest, ModernNode)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kNode, Semver{.parts = {26, 0, 0}, .pre_release = {}}},
    };
    JSFeature unsupported = UnsupportedJSFeatures(constraints);
    EXPECT_FALSE(Has(unsupported, JSFeature::kArrow));
    EXPECT_FALSE(Has(unsupported, JSFeature::kUsing));
}

// ===========================================================================
// UnsupportedCSSFeatures
// ===========================================================================

TEST(UnsupportedCSSFeaturesTest, EmptyConstraints)
{
    EXPECT_EQ(UnsupportedCSSFeatures({}), static_cast<CSSFeature>(0));
}

TEST(UnsupportedCSSFeaturesTest, NonBrowserIgnored)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kNode, Semver{.parts = {26, 0, 0}, .pre_release = {}}},
    };
    EXPECT_EQ(UnsupportedCSSFeatures(constraints), static_cast<CSSFeature>(0));
}

TEST(UnsupportedCSSFeaturesTest, Chrome120SupportsAll)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kChrome, Semver{.parts = {120, 0, 0}, .pre_release = {}}},
    };
    EXPECT_EQ(UnsupportedCSSFeatures(constraints), static_cast<CSSFeature>(0));
}

TEST(UnsupportedCSSFeaturesTest, Chrome119LacksNesting)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kChrome, Semver{.parts = {119, 0, 0}, .pre_release = {}}},
    };
    EXPECT_EQ(UnsupportedCSSFeatures(constraints), CSSFeature::kNesting);
}

// ===========================================================================
// CSSPrefixData
// ===========================================================================

TEST(CSSPrefixDataTest, EmptyConstraints)
{
    EXPECT_TRUE(CSSPrefixData({}).empty());
}

TEST(CSSPrefixDataTest, NonBrowserIgnored)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kNode, Semver{.parts = {26, 0, 0}, .pre_release = {}}},
    };
    EXPECT_TRUE(CSSPrefixData(constraints).empty());
}

TEST(CSSPrefixDataTest, Chrome80StillNeedsWebkit)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kChrome, Semver{.parts = {80, 0, 0}, .pre_release = {}}},
    };
    auto result = CSSPrefixData(constraints);
    EXPECT_EQ(result.at(kDAppearance), CSSPrefix::kWebkitPrefix);
    EXPECT_EQ(result.at(kDWidth), CSSPrefix::kWebkitPrefix);
    EXPECT_EQ(result.at(kDPrintColorAdjust), CSSPrefix::kWebkitPrefix);
    EXPECT_TRUE(result.find(kDUserSelect) == result.end());
}

TEST(CSSPrefixDataTest, Firefox40NeedsMoz)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kFirefox, Semver{.parts = {40, 0, 0}, .pre_release = {}}},
    };
    auto result = CSSPrefixData(constraints);
    EXPECT_EQ(result.at(kDAppearance), CSSPrefix::kMozPrefix);
    EXPECT_EQ(result.at(kDTabSize), CSSPrefix::kMozPrefix);
    EXPECT_EQ(result.at(kDUserSelect), CSSPrefix::kMozPrefix);
    EXPECT_EQ(result.at(kDWidth), CSSPrefix::kWebkitPrefix);
    EXPECT_TRUE(result.find(kDTextDecorationColor) == result.end());
}

TEST(CSSPrefixDataTest, IE11AlwaysMs)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kIE, Semver{.parts = {11, 0, 0}, .pre_release = {}}},
    };
    auto result = CSSPrefixData(constraints);
    EXPECT_EQ(result.at(kDHyphens), CSSPrefix::kMsPrefix);
    EXPECT_EQ(result.at(kDUserSelect), CSSPrefix::kMsPrefix);
}

TEST(CSSPrefixDataTest, MultipleEnginesCombine)
{
    const std::unordered_map<Engine, Semver> constraints = {
        {Engine::kFirefox, Semver{.parts = {40, 0, 0}, .pre_release = {}}},
        {Engine::kChrome, Semver{.parts = {80, 0, 0}, .pre_release = {}}},
    };
    auto result = CSSPrefixData(constraints);
    EXPECT_EQ(result.at(kDAppearance), CSSPrefix::kMozPrefix | CSSPrefix::kWebkitPrefix);
    EXPECT_EQ(result.at(kDWidth), CSSPrefix::kWebkitPrefix);
}