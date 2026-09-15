#include "test/guchho_test.hpp"
#include "guchho/entities.hpp"

#include <cstdint>
#include <string_view>

using guchho::entities::DecodeNamedEntity;
using guchho::entities::kEntities;
using guchho::entities::kEntityCount;
using guchho::entities::kMaxEntityCodePoints;
using guchho::entities::kMaxEntityNameLength;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

struct DecodeResult {
    size_t consumed = 0;
    uint32_t codepoints[2] = {};
    size_t cp_count = 0;
    bool matched_semicolon = false;
};

DecodeResult Decode(std::u16string_view input, bool in_attribute = false) {
    DecodeResult r;
    r.consumed = DecodeNamedEntity(input, in_attribute, r.codepoints,
                                   r.cp_count, r.matched_semicolon);
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// DecodeNamedEntity – basic matching
// ---------------------------------------------------------------------------

TEST(DecodeNamedEntityTest, EmptyInputReturnsZero)
{
    auto r = Decode(u"");
    EXPECT_EQ(r.consumed, 0u);
    EXPECT_EQ(r.cp_count, 0u);
}

TEST(DecodeNamedEntityTest, NoMatchReturnsZero)
{
    auto r = Decode(u"zzzzz");
    EXPECT_EQ(r.consumed, 0u);
}

TEST(DecodeNamedEntityTest, SimpleEntityWithSemicolon)
{
    auto r = Decode(u"amp;");
    EXPECT_EQ(r.consumed, 4u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x26u);
    EXPECT_TRUE(r.matched_semicolon);
}

TEST(DecodeNamedEntityTest, SimpleEntityWithoutSemicolon)
{
    auto r = Decode(u"amp");
    EXPECT_EQ(r.consumed, 3u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x26u);
    EXPECT_FALSE(r.matched_semicolon);
}

TEST(DecodeNamedEntityTest, LessThanEntity)
{
    auto r = Decode(u"lt;");
    EXPECT_EQ(r.consumed, 3u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x3Cu);
    EXPECT_TRUE(r.matched_semicolon);
}

TEST(DecodeNamedEntityTest, GreaterThanEntity)
{
    auto r = Decode(u"gt;");
    EXPECT_EQ(r.consumed, 3u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x3Eu);
    EXPECT_TRUE(r.matched_semicolon);
}

TEST(DecodeNamedEntityTest, QuotationMarkEntity)
{
    auto r = Decode(u"quot;");
    EXPECT_EQ(r.consumed, 5u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x22u);
    EXPECT_TRUE(r.matched_semicolon);
}

TEST(DecodeNamedEntityTest, ApostropheEntity)
{
    auto r = Decode(u"apos;");
    EXPECT_EQ(r.consumed, 5u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x27u);
    EXPECT_TRUE(r.matched_semicolon);
}

// ---------------------------------------------------------------------------
// DecodeNamedEntity – multi-codepoint entities
// ---------------------------------------------------------------------------

TEST(DecodeNamedEntityTest, LigatureEntity)
{
    auto r = Decode(u"fjlig;");
    EXPECT_EQ(r.consumed, 6u);
    EXPECT_EQ(r.cp_count, 2u);
    EXPECT_EQ(r.codepoints[0], 0x66u);
    EXPECT_EQ(r.codepoints[1], 0x6Au);
    EXPECT_TRUE(r.matched_semicolon);
}

// ---------------------------------------------------------------------------
// DecodeNamedEntity – longest match
// ---------------------------------------------------------------------------

TEST(DecodeNamedEntityTest, LongestMatchWins)
{
    auto r = Decode(u"not;");
    EXPECT_EQ(r.consumed, 4u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0xACu);
    EXPECT_TRUE(r.matched_semicolon);
}

TEST(DecodeNamedEntityTest, PartialInputMatch)
{
    auto r = Decode(u"amp is here");
    EXPECT_EQ(r.consumed, 3u);
    EXPECT_EQ(r.codepoints[0], 0x26u);
    EXPECT_FALSE(r.matched_semicolon);
}

// ---------------------------------------------------------------------------
// DecodeNamedEntity – attribute mode quirk
// ---------------------------------------------------------------------------

TEST(DecodeNamedEntityTest, AttributeModeRejectsSemicolonLessFollowedByEquals)
{
    auto r = Decode(u"amp=foo", true);
    EXPECT_EQ(r.consumed, 0u);
}

TEST(DecodeNamedEntityTest, AttributeModeRejectsSemicolonLessFollowedByAlpha)
{
    auto r = Decode(u"ampFoo", true);
    EXPECT_EQ(r.consumed, 0u);
}

TEST(DecodeNamedEntityTest, AttributeModeRejectsSemicolonLessFollowedByDigit)
{
    auto r = Decode(u"amp123", true);
    EXPECT_EQ(r.consumed, 0u);
}

TEST(DecodeNamedEntityTest, AttributeModeAcceptsSemicolonLessFollowedByNonAlpha)
{
    auto r = Decode(u"amp ", true);
    EXPECT_EQ(r.consumed, 3u);
    EXPECT_EQ(r.codepoints[0], 0x26u);
    EXPECT_FALSE(r.matched_semicolon);
}

TEST(DecodeNamedEntityTest, AttributeModeAcceptsWithSemicolon)
{
    auto r = Decode(u"amp;", true);
    EXPECT_EQ(r.consumed, 4u);
    EXPECT_EQ(r.codepoints[0], 0x26u);
    EXPECT_TRUE(r.matched_semicolon);
}

// ---------------------------------------------------------------------------
// DecodeNamedEntity – non-ASCII input skipped
// ---------------------------------------------------------------------------

TEST(DecodeNamedEntityTest, NonAsciiPrefixSkipped)
{
    auto r = Decode(u"\x00E9amp;");
    EXPECT_EQ(r.consumed, 0u);
}

// ---------------------------------------------------------------------------
// DecodeNamedEntity – table metadata
// ---------------------------------------------------------------------------

TEST(EntityTableTest, EntityCountIsPositive)
{
    EXPECT_GT(kEntityCount, 0u);
}

TEST(EntityTableTest, MaxEntityNameLengthIsPositive)
{
    EXPECT_GT(kMaxEntityNameLength, 0u);
}

TEST(EntityTableTest, MaxEntityCodePointsIsPositive)
{
    EXPECT_GT(kMaxEntityCodePoints, 0u);
}

TEST(EntityTableTest, MaxEntityCodePointsIsAtMostTwo)
{
    EXPECT_LE(kMaxEntityCodePoints, 2u);
}

TEST(EntityTableTest, FirstEntityIsValid)
{
    EXPECT_FALSE(kEntities[0].name.empty());
    EXPECT_GT(kEntities[0].count, 0u);
    EXPECT_LE(kEntities[0].count, 2u);
}

TEST(EntityTableTest, EntitiesAreSortedByName)
{
    for (size_t i = 1; i < kEntityCount; ++i) {
        EXPECT_TRUE(kEntities[i - 1].name < kEntities[i].name);
    }
}

// ---------------------------------------------------------------------------
// DecodeNamedEntity – various well-known entities
// ---------------------------------------------------------------------------

TEST(DecodeNamedEntityTest, NbspEntity)
{
    auto r = Decode(u"nbsp;");
    EXPECT_EQ(r.consumed, 5u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x00A0u);
}

TEST(DecodeNamedEntityTest, CopyEntity)
{
    auto r = Decode(u"copy;");
    EXPECT_EQ(r.consumed, 5u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x00A9u);
}

TEST(DecodeNamedEntityTest, RegEntity)
{
    auto r = Decode(u"reg;");
    EXPECT_EQ(r.consumed, 4u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x00AEu);
}

TEST(DecodeNamedEntityTest, EuroEntity)
{
    auto r = Decode(u"euro;");
    EXPECT_EQ(r.consumed, 5u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x20ACu);
}

TEST(DecodeNamedEntityTest, MdashEntity)
{
    auto r = Decode(u"mdash;");
    EXPECT_EQ(r.consumed, 6u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x2014u);
}

TEST(DecodeNamedEntityTest, LdquoEntity)
{
    auto r = Decode(u"ldquo;");
    EXPECT_EQ(r.consumed, 6u);
    EXPECT_EQ(r.cp_count, 1u);
    EXPECT_EQ(r.codepoints[0], 0x201Cu);
}

// ---------------------------------------------------------------------------
// DecodeNamedEntity – edge cases
// ---------------------------------------------------------------------------

TEST(DecodeNamedEntityTest, SingleCharacterInput)
{
    auto r = Decode(u"a");
    EXPECT_EQ(r.consumed, 0u);
}

TEST(DecodeNamedEntityTest, InputShorterThanEntity)
{
    auto r = Decode(u"am");
    EXPECT_EQ(r.consumed, 0u);
}
