#include "test/guchho_test.hpp"
#include "guchho/compiler.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace guchho::compiler;

// ---------------------------------------------------------------------------
// ImportKindToStringForMetafile
// ---------------------------------------------------------------------------

TEST(ImportKindToStringForMetafileTest, EntryPoint)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kEntryPoint), "entry-point");
}

TEST(ImportKindToStringForMetafileTest, Stmt)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kStmt), "import-statement");
}

TEST(ImportKindToStringForMetafileTest, Require)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kRequire), "require-call");
}

TEST(ImportKindToStringForMetafileTest, Dynamic)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kDynamic), "dynamic-import");
}

TEST(ImportKindToStringForMetafileTest, RequireResolve)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kRequireResolve), "require-resolve");
}

TEST(ImportKindToStringForMetafileTest, Internal)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kInternal), "");
}

TEST(ImportKindToStringForMetafileTest, At)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kAt), "import-rule");
}

TEST(ImportKindToStringForMetafileTest, ComposesFrom)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kComposesFrom), "composes-from");
}

TEST(ImportKindToStringForMetafileTest, Url)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kUrl), "url-token");
}

TEST(ImportKindToStringForMetafileTest, File)
{
    EXPECT_EQ(ImportKindToStringForMetafile(ImportKind::kFile), "file-loader");
}

// ---------------------------------------------------------------------------
// AssertOrWithKeywordToString
// ---------------------------------------------------------------------------

TEST(AssertOrWithKeywordToStringTest, Assert)
{
    EXPECT_EQ(AssertOrWithKeywordToString(AssertOrWithKeyword::kAssert), "assert");
}

TEST(AssertOrWithKeywordToStringTest, With)
{
    EXPECT_EQ(AssertOrWithKeywordToString(AssertOrWithKeyword::kWith), "with");
}

// ---------------------------------------------------------------------------
// FindAssertOrWithEntry (const)
// ---------------------------------------------------------------------------

TEST(FindAssertOrWithEntryTest, ConstFound)
{
    std::vector<AssertOrWithEntry> assertions;
    AssertOrWithEntry entry;
    entry.key = u"type";
    entry.value = u"json";
    assertions.push_back(entry);

    const auto& vec = assertions;
    const AssertOrWithEntry* result = FindAssertOrWithEntry(vec, "type");
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->value, u"json");
}

TEST(FindAssertOrWithEntryTest, ConstNotFound)
{
    std::vector<AssertOrWithEntry> assertions;
    AssertOrWithEntry entry;
    entry.key = u"type";
    entry.value = u"json";
    assertions.push_back(entry);

    const auto& vec = assertions;
    const AssertOrWithEntry* result = FindAssertOrWithEntry(vec, "loader");
    EXPECT_EQ(result, nullptr);
}

TEST(FindAssertOrWithEntryTest, ConstFindsFirstMatch)
{
    std::vector<AssertOrWithEntry> assertions;
    AssertOrWithEntry first;
    first.key = u"type";
    first.value = u"json";
    assertions.push_back(first);
    AssertOrWithEntry second;
    second.key = u"type";
    second.value = u"css";
    assertions.push_back(second);

    const auto& vec = assertions;
    const AssertOrWithEntry* result = FindAssertOrWithEntry(vec, "type");
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->value, u"json");
}

TEST(FindAssertOrWithEntryTest, ConstEmptyVector)
{
    std::vector<AssertOrWithEntry> assertions;
    const auto& vec = assertions;
    const AssertOrWithEntry* result = FindAssertOrWithEntry(vec, "type");
    EXPECT_EQ(result, nullptr);
}

// ---------------------------------------------------------------------------
// FindAssertOrWithEntry (non-const)
// ---------------------------------------------------------------------------

TEST(FindAssertOrWithEntryTest, MutableFound)
{
    std::vector<AssertOrWithEntry> assertions;
    AssertOrWithEntry entry;
    entry.key = u"type";
    entry.value = u"json";
    assertions.push_back(entry);

    AssertOrWithEntry* result = FindAssertOrWithEntry(assertions, "type");
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->value, u"json");
    result->value = u"css";
    EXPECT_EQ(assertions[0].value, u"css");
}

TEST(FindAssertOrWithEntryTest, MutableNotFound)
{
    std::vector<AssertOrWithEntry> assertions;
    AssertOrWithEntry entry;
    entry.key = u"type";
    entry.value = u"json";
    assertions.push_back(entry);

    AssertOrWithEntry* result = FindAssertOrWithEntry(assertions, "loader");
    EXPECT_EQ(result, nullptr);
}

// ---------------------------------------------------------------------------
// Symbol::MergeContentsWith
// ---------------------------------------------------------------------------

TEST(SymbolMergeContentsWithTest, UseCountAccumulates)
{
    Symbol old_sym;
    old_sym.use_count_estimate = 5;

    Symbol new_sym;
    new_sym.use_count_estimate = 3;

    new_sym.MergeContentsWith(old_sym);
    EXPECT_EQ(new_sym.use_count_estimate, 8u);
}

TEST(SymbolMergeContentsWithTest, MustNotBeRenamedPropagates)
{
    Symbol old_sym;
    old_sym.use_count_estimate = 1;
    old_sym.original_name = "global";
    old_sym.flags = SymbolFlags::kMustNotBeRenamed;

    Symbol new_sym;
    new_sym.use_count_estimate = 1;

    new_sym.MergeContentsWith(old_sym);
    EXPECT_TRUE(Has(new_sym.flags, SymbolFlags::kMustNotBeRenamed));
    EXPECT_EQ(new_sym.original_name, "global");
}

TEST(SymbolMergeContentsWithTest, MustNotBeRenamedNotOverwritten)
{
    Symbol old_sym;
    old_sym.use_count_estimate = 1;

    Symbol new_sym;
    new_sym.use_count_estimate = 1;
    new_sym.original_name = "myName";
    new_sym.flags = SymbolFlags::kMustNotBeRenamed;

    new_sym.MergeContentsWith(old_sym);
    EXPECT_TRUE(Has(new_sym.flags, SymbolFlags::kMustNotBeRenamed));
    EXPECT_EQ(new_sym.original_name, "myName");
}

TEST(SymbolMergeContentsWithTest, JSXCapitalizePropagates)
{
    Symbol old_sym;
    old_sym.use_count_estimate = 1;
    old_sym.flags = SymbolFlags::kMustStartWithCapitalLetterForJSX;

    Symbol new_sym;
    new_sym.use_count_estimate = 1;

    new_sym.MergeContentsWith(old_sym);
    EXPECT_TRUE(Has(new_sym.flags, SymbolFlags::kMustStartWithCapitalLetterForJSX));
}

// ---------------------------------------------------------------------------
// Symbol::SlotNamespace
// ---------------------------------------------------------------------------

TEST(SymbolSlotNamespaceTest, UnboundReturnsMustNotBeRenamed)
{
    Symbol sym;
    sym.kind = SymbolKind::kUnbound;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kMustNotBeRenamed);
}

TEST(SymbolSlotNamespaceTest, MustNotBeRenamedFlag)
{
    Symbol sym;
    sym.kind = SymbolKind::kOther;
    sym.flags = SymbolFlags::kMustNotBeRenamed;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kMustNotBeRenamed);
}

TEST(SymbolSlotNamespaceTest, PrivateFieldReturnsPrivateName)
{
    Symbol sym;
    sym.kind = SymbolKind::kPrivateField;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kPrivateName);
}

TEST(SymbolSlotNamespaceTest, PrivateMethodReturnsPrivateName)
{
    Symbol sym;
    sym.kind = SymbolKind::kPrivateMethod;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kPrivateName);
}

TEST(SymbolSlotNamespaceTest, LabelReturnsLabel)
{
    Symbol sym;
    sym.kind = SymbolKind::kLabel;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kLabel);
}

TEST(SymbolSlotNamespaceTest, MangledPropReturnsMangledProp)
{
    Symbol sym;
    sym.kind = SymbolKind::kMangledProp;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kMangledProp);
}

TEST(SymbolSlotNamespaceTest, OtherReturnsDefault)
{
    Symbol sym;
    sym.kind = SymbolKind::kOther;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kDefault);
}

TEST(SymbolSlotNamespaceTest, HoistedReturnsDefault)
{
    Symbol sym;
    sym.kind = SymbolKind::kHoisted;
    EXPECT_EQ(sym.SlotNamespace(), SlotNamespace::kDefault);
}

// ---------------------------------------------------------------------------
// SlotCounts::UnionMax
// ---------------------------------------------------------------------------

TEST(SlotCountsUnionMaxTest, OtherLargerReplaces)
{
    SlotCounts a;
    a.data = {2, 1, 0, 3};
    SlotCounts b;
    b.data = {1, 4, 0, 2};

    a.UnionMax(b);
    EXPECT_EQ(a.data[0], 2u);
    EXPECT_EQ(a.data[1], 4u);
    EXPECT_EQ(a.data[2], 0u);
    EXPECT_EQ(a.data[3], 3u);
}

TEST(SlotCountsUnionMaxTest, ThisLargerUnchanged)
{
    SlotCounts a;
    a.data = {5, 5, 5, 5};
    SlotCounts b;
    b.data = {1, 1, 1, 1};

    a.UnionMax(b);
    EXPECT_EQ(a.data[0], 5u);
    EXPECT_EQ(a.data[1], 5u);
    EXPECT_EQ(a.data[2], 5u);
    EXPECT_EQ(a.data[3], 5u);
}

TEST(SlotCountsUnionMaxTest, EqualValuesUnchanged)
{
    SlotCounts a;
    a.data = {3, 3, 3, 3};
    SlotCounts b;
    b.data = {3, 3, 3, 3};

    a.UnionMax(b);
    EXPECT_EQ(a.data[0], 3u);
    EXPECT_EQ(a.data[1], 3u);
    EXPECT_EQ(a.data[2], 3u);
    EXPECT_EQ(a.data[3], 3u);
}

// ---------------------------------------------------------------------------
// CharFreq::Scan
// ---------------------------------------------------------------------------

TEST(CharFreqScanTest, LowercaseLettersIncrement)
{
    CharFreq freq;
    freq.Scan("abc", +1);
    EXPECT_EQ(freq.freq[0], 1);   // a
    EXPECT_EQ(freq.freq[1], 1);   // b
    EXPECT_EQ(freq.freq[2], 1);   // c
    EXPECT_EQ(freq.freq[3], 0);   // d
}

TEST(CharFreqScanTest, UppercaseLettersIncrement)
{
    CharFreq freq;
    freq.Scan("ABC", +1);
    EXPECT_EQ(freq.freq[26], 1);  // A
    EXPECT_EQ(freq.freq[27], 1);  // B
    EXPECT_EQ(freq.freq[28], 1);  // C
}

TEST(CharFreqScanTest, DigitsIncrement)
{
    CharFreq freq;
    freq.Scan("012", +1);
    EXPECT_EQ(freq.freq[52], 1);  // 0
    EXPECT_EQ(freq.freq[53], 1);  // 1
    EXPECT_EQ(freq.freq[54], 1);  // 2
}

TEST(CharFreqScanTest, UnderscoreAndDollar)
{
    CharFreq freq;
    freq.Scan("_$", +1);
    EXPECT_EQ(freq.freq[62], 1);  // _
    EXPECT_EQ(freq.freq[63], 1);  // $
}

TEST(CharFreqScanTest, DeltaNegativeDecrements)
{
    CharFreq freq;
    freq.Scan("aaa", +1);
    EXPECT_EQ(freq.freq[0], 3);
    freq.Scan("aa", -1);
    EXPECT_EQ(freq.freq[0], 1);
}

TEST(CharFreqScanTest, DeltaZeroIsNoop)
{
    CharFreq freq;
    freq.Scan("abc", 0);
    EXPECT_EQ(freq.freq[0], 0);
    EXPECT_EQ(freq.freq[1], 0);
    EXPECT_EQ(freq.freq[2], 0);
}

TEST(CharFreqScanTest, NonAlphanumericIgnored)
{
    CharFreq freq;
    freq.Scan("!@# ", +1);
    for (size_t i = 0; i < 64; i++) {
        EXPECT_EQ(freq.freq[i], 0);
    }
}

// ---------------------------------------------------------------------------
// CharFreq::Include
// ---------------------------------------------------------------------------

TEST(CharFreqIncludeTest, CombinesFrequencies)
{
    CharFreq a;
    a.Scan("foo", +1);
    CharFreq b;
    b.Scan("bar", +1);

    a.Include(b);
    EXPECT_EQ(a.freq[5], 1);   // f
    EXPECT_EQ(a.freq[14], 2);  // o
    EXPECT_EQ(a.freq[1], 1);   // b
    EXPECT_EQ(a.freq[0], 1);   // a
    EXPECT_EQ(a.freq[17], 1);  // r
}

TEST(CharFreqIncludeTest, EmptySourceAddsNothing)
{
    CharFreq a;
    a.Scan("test", +1);
    CharFreq b;

    a.Include(b);
    EXPECT_EQ(a.freq[19], 2);  // t (appears twice in "test")
    EXPECT_EQ(a.freq[4], 1);   // e
    EXPECT_EQ(a.freq[18], 1);  // s
}

// ---------------------------------------------------------------------------
// NameMinifier::NumberToMinifiedName
// ---------------------------------------------------------------------------

TEST(NameMinifierNumberToMinifiedNameTest, ZeroReturnsFirstHeadChar)
{
    NameMinifier minifier;
    minifier.head = "abc";
    minifier.tail = "xyz";
    EXPECT_EQ(minifier.NumberToMinifiedName(0), "a");
}

TEST(NameMinifierNumberToMinifiedNameTest, WithinHeadSize)
{
    NameMinifier minifier;
    minifier.head = "abc";
    minifier.tail = "xyz";
    EXPECT_EQ(minifier.NumberToMinifiedName(1), "b");
    EXPECT_EQ(minifier.NumberToMinifiedName(2), "c");
}

TEST(NameMinifierNumberToMinifiedNameTest, BeyondHeadSize)
{
    NameMinifier minifier;
    minifier.head = "ab";
    minifier.tail = "xy";
    // i=0 => a, i=1 => b, i=2 => ax, i=3 => bx
    EXPECT_EQ(minifier.NumberToMinifiedName(2), "ax");
    EXPECT_EQ(minifier.NumberToMinifiedName(3), "bx");
}

TEST(NameMinifierNumberToMinifiedNameTest, LargeIndex)
{
    NameMinifier minifier;
    minifier.head = "a";
    minifier.tail = "b";
    // All names: a, ab, abb, abbb, ...
    EXPECT_EQ(minifier.NumberToMinifiedName(0), "a");
    EXPECT_EQ(minifier.NumberToMinifiedName(1), "ab");
    EXPECT_EQ(minifier.NumberToMinifiedName(2), "abb");
    EXPECT_EQ(minifier.NumberToMinifiedName(3), "abbb");
}

// ---------------------------------------------------------------------------
// NameMinifier::ShuffleByCharFreq
// ---------------------------------------------------------------------------

TEST(NameMinifierShuffleByCharFreqTest, FrequentCharsComeFirst)
{
    NameMinifier minifier;
    minifier.head = "ab";
    minifier.tail = "ab";

    CharFreq freq;
    freq.freq[0] = 100;  // a
    freq.freq[1] = 1;    // b

    NameMinifier shuffled = minifier.ShuffleByCharFreq(freq);
    EXPECT_EQ(shuffled.head[0], 'a');
    EXPECT_EQ(shuffled.tail[0], 'a');
}

TEST(NameMinifierShuffleByCharFreqTest, ZeroFrequencyPreservesOrder)
{
    NameMinifier minifier;
    minifier.head = "ab";
    minifier.tail = "ab";

    CharFreq freq;

    NameMinifier shuffled = minifier.ShuffleByCharFreq(freq);
    EXPECT_EQ(shuffled.head[0], 'a');
    EXPECT_EQ(shuffled.head[1], 'b');
}

// ---------------------------------------------------------------------------
// Default name minifiers
// ---------------------------------------------------------------------------

TEST(DefaultNameMinifierTest, JSTailContainsDigits)
{
    EXPECT_TRUE(kDefaultNameMinifierJS.tail.find('0') != std::string::npos);
    EXPECT_TRUE(kDefaultNameMinifierJS.tail.find('9') != std::string::npos);
}

TEST(DefaultNameMinifierTest, JSTailContainsDollar)
{
    EXPECT_TRUE(kDefaultNameMinifierJS.tail.find('$') != std::string::npos);
}

TEST(DefaultNameMinifierTest, CSSHeadExcludesDigits)
{
    EXPECT_TRUE(kDefaultNameMinifierCSS.head.find('0') == std::string::npos);
    EXPECT_TRUE(kDefaultNameMinifierCSS.head.find('9') == std::string::npos);
}

TEST(DefaultNameMinifierTest, CSSHeadExcludesDollar)
{
    EXPECT_TRUE(kDefaultNameMinifierCSS.head.find('$') == std::string::npos);
}

TEST(DefaultNameMinifierTest, CSSTailExcludesDollar)
{
    EXPECT_TRUE(kDefaultNameMinifierCSS.tail.find('$') == std::string::npos);
}

// ---------------------------------------------------------------------------
// NewSymbolMap
// ---------------------------------------------------------------------------

TEST(NewSymbolMapTest, CreatesMapWithCorrectSourceCount)
{
    SymbolMap map = NewSymbolMap(5);
    EXPECT_EQ(map.symbols_for_source.size(), 5u);
}

TEST(NewSymbolMapTest, EmptySourceCount)
{
    SymbolMap map = NewSymbolMap(0);
    EXPECT_TRUE(map.symbols_for_source.empty());
}

// ---------------------------------------------------------------------------
// FollowSymbols
// ---------------------------------------------------------------------------

TEST(FollowSymbolsTest, NoLinkReturnsSelf)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym;
    sym.link = kInvalidRef;
    map.symbols_for_source[0].push_back(sym);

    Ref result = FollowSymbols(map, Ref{0, 0});
    EXPECT_EQ(result.source_index, 0u);
    EXPECT_EQ(result.inner_index, 0u);
}

TEST(FollowSymbolsTest, SingleLinkFollowed)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym_a;
    sym_a.link = kInvalidRef;
    Symbol sym_b;
    sym_b.link = kInvalidRef;
    map.symbols_for_source[0].push_back(sym_a);
    map.symbols_for_source[0].push_back(sym_b);

    map.symbols_for_source[0][0].link = Ref{0, 1};

    Ref result = FollowSymbols(map, Ref{0, 0});
    EXPECT_EQ(result.source_index, 0u);
    EXPECT_EQ(result.inner_index, 1u);
}

TEST(FollowSymbolsTest, ChainOfLinksCompressed)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym_a;
    sym_a.link = kInvalidRef;
    Symbol sym_b;
    sym_b.link = kInvalidRef;
    Symbol sym_c;
    sym_c.link = kInvalidRef;
    map.symbols_for_source[0].push_back(sym_a);
    map.symbols_for_source[0].push_back(sym_b);
    map.symbols_for_source[0].push_back(sym_c);

    map.symbols_for_source[0][0].link = Ref{0, 1};
    map.symbols_for_source[0][1].link = Ref{0, 2};

    Ref result = FollowSymbols(map, Ref{0, 0});
    EXPECT_EQ(result.source_index, 0u);
    EXPECT_EQ(result.inner_index, 2u);
    // Path compression: first symbol now points directly to final target
    EXPECT_EQ(map.symbols_for_source[0][0].link.source_index, 0u);
    EXPECT_EQ(map.symbols_for_source[0][0].link.inner_index, 2u);
}

// ---------------------------------------------------------------------------
// FollowAllSymbols
// ---------------------------------------------------------------------------

TEST(FollowAllSymbolsTest, ResolvesAllLinks)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym_a;
    sym_a.link = kInvalidRef;
    Symbol sym_b;
    sym_b.link = kInvalidRef;
    Symbol sym_c;
    sym_c.link = kInvalidRef;
    map.symbols_for_source[0].push_back(sym_a);
    map.symbols_for_source[0].push_back(sym_b);
    map.symbols_for_source[0].push_back(sym_c);

    map.symbols_for_source[0][0].link = Ref{0, 1};
    map.symbols_for_source[0][1].link = Ref{0, 2};

    FollowAllSymbols(map);
    // After full resolution, all symbols should point to index 2
    EXPECT_EQ(map.symbols_for_source[0][0].link.inner_index, 2u);
    EXPECT_EQ(map.symbols_for_source[0][1].link.inner_index, 2u);
}

// ---------------------------------------------------------------------------
// MergeSymbols
// ---------------------------------------------------------------------------

TEST(MergeSymbolsTest, SameRefReturnsNewRef)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym;
    sym.link = kInvalidRef;
    map.symbols_for_source[0].push_back(sym);

    Ref result = MergeSymbols(map, Ref{0, 0}, Ref{0, 0});
    EXPECT_EQ(result.source_index, 0u);
    EXPECT_EQ(result.inner_index, 0u);
}

TEST(MergeSymbolsTest, BothUnlinkedOldLinksToNew)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym_a;
    sym_a.link = kInvalidRef;
    sym_a.use_count_estimate = 3;
    Symbol sym_b;
    sym_b.link = kInvalidRef;
    sym_b.use_count_estimate = 5;
    map.symbols_for_source[0].push_back(sym_a);
    map.symbols_for_source[0].push_back(sym_b);

    Ref result = MergeSymbols(map, Ref{0, 0}, Ref{0, 1});
    EXPECT_EQ(result.source_index, 0u);
    EXPECT_EQ(result.inner_index, 1u);
    EXPECT_EQ(map.symbols_for_source[0][0].link.inner_index, 1u);
    // Metadata merged into new_sym
    EXPECT_EQ(map.symbols_for_source[0][1].use_count_estimate, 8u);
}

TEST(MergeSymbolsTest, OldAlreadyLinkedRedirects)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym_a;
    sym_a.link = kInvalidRef;
    Symbol sym_b;
    sym_b.link = kInvalidRef;
    Symbol sym_c;
    sym_c.link = kInvalidRef;
    map.symbols_for_source[0].push_back(sym_a);
    map.symbols_for_source[0].push_back(sym_b);
    map.symbols_for_source[0].push_back(sym_c);

    // a -> b
    map.symbols_for_source[0][0].link = Ref{0, 1};

    // Merge a with c: should follow a's link and merge b with c
    Ref result = MergeSymbols(map, Ref{0, 0}, Ref{0, 2});
    EXPECT_EQ(result.source_index, 0u);
    EXPECT_EQ(result.inner_index, 2u);
}

TEST(MergeSymbolsTest, NewAlreadyLinkedRedirects)
{
    SymbolMap map = NewSymbolMap(1);
    Symbol sym_a;
    sym_a.link = kInvalidRef;
    Symbol sym_b;
    sym_b.link = kInvalidRef;
    Symbol sym_c;
    sym_c.link = kInvalidRef;
    map.symbols_for_source[0].push_back(sym_a);
    map.symbols_for_source[0].push_back(sym_b);
    map.symbols_for_source[0].push_back(sym_c);

    // b -> c
    map.symbols_for_source[0][1].link = Ref{0, 2};

    // Merge a with b: should follow b's link and merge a with c
    Ref result = MergeSymbols(map, Ref{0, 0}, Ref{0, 1});
    EXPECT_EQ(result.source_index, 0u);
    EXPECT_EQ(result.inner_index, 2u);
}
