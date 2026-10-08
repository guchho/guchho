// Tests for the four minification flags as the grammar reads them.
//
// There are four flags and three switches behind them, which makes eight
// combinations plus the ways of spelling "off", and the interesting property is
// not that any one of them works but that the three passes stay independent:
// naming one pass must never turn on the other two, because that is the whole
// reason the flags exist separately. A person who wants output they can still
// debug shortens nothing and collapses nothing else.
//
// These are asserted against the option struct rather than against output. The
// output tests live in cli_build_test.cpp, where a real build answers what the
// bytes look like; here the question is one step earlier, which is which switch
// was written, and a test that could only see the answer would pass for a
// grammar that set all three whenever any one was named.
//
// The "kOptMinify" assertions are the second half of the same property. It is
// the mark that keeps a project's guchho.json from overruleing a command line,
// so a flag that set a field without marking it would minify and then be
// minified back on by the config it was meant to beat.

#include "test/guchho_test.hpp"

#include "guchho/api.hpp"
#include "guchho/cli.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cli::test {

namespace {

// The three switches as one value, so a test can compare a whole combination
// rather than three separate expectations that would each pass on their own.
struct Passes {
    bool whitespace{};
    bool identifiers{};
    bool syntax{};

    bool operator==(const Passes& other) const
    {
        return whitespace == other.whitespace &&
               identifiers == other.identifiers &&
               syntax == other.syntax;
    }
};

// The switches a set of options has on, for comparison against a Passes.
Passes Of(const guchho::api::BuildOptions& opts)
{
    return Passes{opts.minify_whitespace, opts.minify_identifiers, opts.minify_syntax};
}

// What a combination was, in a sentence a failing assertion can print. Built out
// of the switches rather than out of the numbers, because "whitespace" says which
// pass went wrong and "0,1,0" does not.
std::string Describe(const Passes& passes)
{
    std::string names;
    auto add = [&names](const std::string& name) {
        if (!names.empty()) names += "+";
        names += name;
    };
    if (passes.whitespace)  add("whitespace");
    if (passes.identifiers) add("identifiers");
    if (passes.syntax)      add("syntax");
    return names.empty() ? "nothing" : names;
}

// The build options a command line produces, or an empty optional if the
// grammar complained about it.
std::optional<guchho::api::BuildOptions> BuildOptionsFor(
    const std::vector<std::string>& args)
{
    guchho::api::BuildOptions      build;
    guchho::cli::ParseOptionsExtras extras;

    std::optional<guchho::cli::ErrorWithNote> err =
        guchho::cli::parseOptionsImpl(args, &build, nullptr,
                                      guchho::cli::ParseOptionsKind::kExternal, extras);
    if (err) {
        return std::nullopt;
    }
    return build;
}

// The transform options a command line produces, or an empty optional if the
// grammar complained about it.
//
// The parser is handed one of the two structures, not both: which one it gets
// is what decides where a flag lands, and handing it a build as well would
// quietly answer that question in the test's favour. So "guchho transform" and
// "guchho build" reach different parsers here exactly as they reach different
// option structs in the program.
std::optional<guchho::api::TransformOptions> TransformOptionsFor(
    const std::vector<std::string>& args)
{
    guchho::api::TransformOptions  transform;
    guchho::cli::ParseOptionsExtras extras;

    std::optional<guchho::cli::ErrorWithNote> err =
        guchho::cli::parseOptionsImpl(args, nullptr, &transform,
                                      guchho::cli::ParseOptionsKind::kExternal, extras);
    if (err) {
        return std::nullopt;
    }
    return transform;
}

// The three switches as a Passes, for the transform side of the same question.
Passes Of(const guchho::api::TransformOptions& opts)
{
    return Passes{opts.minify_whitespace, opts.minify_identifiers, opts.minify_syntax};
}

} // namespace

// ---------------------------------------------------------------------------
// The defaults
// ---------------------------------------------------------------------------

// Nobody saying anything leaves all three off, which is the claim the help text
// makes in its "(default: off)" and the claim a build with no flags has to keep.
TEST(CliMinify, NothingSaidLeavesEveryPassOff) {
    auto opts = BuildOptionsFor({"entry.js"});
    ASSERT_TRUE(opts.has_value());

    const Passes wanted{false, false, false};
    const Passes got = Of(*opts);
    EXPECT_TRUE(got == wanted) << "a build with no minify flag came out as " << Describe(got);

    // The mark is what a config file is asked about, and a command line that
    // named nothing must not claim to have named minification.
    EXPECT_FALSE(opts->explicit_set && opts->explicit_set->Has(guchho::api::kOptMinify))
        << "a command line that named no minify flag marked one as asked for";
}

// ---------------------------------------------------------------------------
// The shorthand
// ---------------------------------------------------------------------------

// "--minify" is all three at once. It is the flag most people type, and the
// reason the three others can stay off by default: one switch for the whole of
// minification, and the parts are for the person who does not want all of it.
TEST(CliMinify, MinifyEnablesAllThreePasses) {
    auto opts = BuildOptionsFor({"--minify"});
    ASSERT_TRUE(opts.has_value());

    const Passes wanted{true, true, true};
    const Passes got = Of(*opts);
    EXPECT_TRUE(got == wanted) << "\"--minify\" came out as " << Describe(got);
}

// "--minify=false" is still a thing somebody can type, and it has to keep
// working against a project whose config turns minification on: that is the
// case the explicit record exists for.
TEST(CliMinify, MinifyFalseTurnsAllThreeOffAndStillCountsAsAsked) {
    auto opts = BuildOptionsFor({"--minify=false"});
    ASSERT_TRUE(opts.has_value());

    const Passes wanted{false, false, false};
    const Passes got = Of(*opts);
    EXPECT_TRUE(got == wanted) << "\"--minify=false\" came out as " << Describe(got);
    ASSERT_TRUE(opts->explicit_set != nullptr);
    EXPECT_TRUE(opts->explicit_set->Has(guchho::api::kOptMinify))
        << "\"--minify=false\" left no record, so a config could turn it back on";
}

// ---------------------------------------------------------------------------
// The three passes, one at a time
// ---------------------------------------------------------------------------

// Each of the three, alone. The two fields it does not name have to stay off:
// that is the difference between this flag and "--minify", and a grammar that
// answered all three for any one of them would pass every other test here.
TEST(CliMinify, EachPassFlagEnablesOnlyThatPass) {
    struct Case {
        std::string flag;
        Passes      wanted;
    };

    const Case cases[] = {
        {"--minify-whitespace",  Passes{true,  false, false}},
        {"--minify-identifiers", Passes{false, true,  false}},
        {"--minify-syntax",      Passes{false, false, true}},
    };

    for (const Case& one : cases) {
        auto opts = BuildOptionsFor({one.flag});
        ASSERT_TRUE(opts.has_value()) << one.flag;

        const Passes got = Of(*opts);
        EXPECT_TRUE(got == one.wanted) << one.flag << " came out as " << Describe(got);

        const Passes everything{true, true, true};
        EXPECT_FALSE(got == everything) << one.flag << " turned on all three passes";
    }
}

// The same three, spelled with a value. "--minify-whitespace=false" is how a
// person takes one pass back off after "--minify", and the other two have to
// survive it: the flag is about its own pass, not about the others.
TEST(CliMinify, OnePassCanBeTurnedOffWhileTheOtherTwoStayOn) {
    auto opts = BuildOptionsFor({"--minify", "--minify-whitespace=false"});
    ASSERT_TRUE(opts.has_value());

    const Passes wanted{false, true, true};
    const Passes got = Of(*opts);
    EXPECT_TRUE(got == wanted) << "taking whitespace back off came out as " << Describe(got);
}

// Two passes together, which is the combination that has no shorthand at all.
TEST(CliMinify, TwoPassFlagsTogetherEnableExactlyThoseTwo) {
    auto opts = BuildOptionsFor({"--minify-identifiers", "--minify-syntax"});
    ASSERT_TRUE(opts.has_value());

    const Passes wanted{false, true, true};
    const Passes got = Of(*opts);
    EXPECT_TRUE(got == wanted) << "two passes together came out as " << Describe(got);
}

// Every one of the four marks minification as asked for. Without the mark a
// project saying "minify": true would answer the command line, and the flag
// would be a request nobody heard.
TEST(CliMinify, EveryMinifyFlagMarksMinificationAsAsked) {
    const std::string flags[] = {"--minify",         "--minify=false",
                                 "--minify-whitespace", "--minify-identifiers",
                                 "--minify-syntax"};

    for (const std::string& flag : flags) {
        auto opts = BuildOptionsFor({flag});
        ASSERT_TRUE(opts.has_value()) << flag;
        ASSERT_TRUE(opts->explicit_set != nullptr) << flag;
        EXPECT_TRUE(opts->explicit_set->Has(guchho::api::kOptMinify))
            << flag << " set a switch and left no record of having said so";
    }
}

// ---------------------------------------------------------------------------
// The prefix collision
// ---------------------------------------------------------------------------

// "--minify-whitespace" starts with "--minify", and a grammar that tested the
// shorter name first would read it as that switch with a strange value. The
// complaint below is the visible half of the same thing: an argument nobody
// claims is reported rather than quietly ignored.
TEST(CliMinify, ALongerFlagIsNotTheShorterOneWithAValue) {
    auto longer = BuildOptionsFor({"--minify-whitespace"});
    ASSERT_TRUE(longer.has_value()) << "\"--minify-whitespace\" was not accepted";

    auto unknown = BuildOptionsFor({"--minify-nope"});
    EXPECT_FALSE(unknown.has_value()) << "a flag that does not exist was accepted";
}

// ---------------------------------------------------------------------------
// The transform
// ---------------------------------------------------------------------------

// A transform shares the grammar rather than having a second copy of it, so the
// four flags mean the same thing on both option structures. What differs is only
// which struct was filled.
TEST(CliMinify, ATransformTakesTheSameFourFlags) {
    auto transform = TransformOptionsFor({"--minify-identifiers"});
    ASSERT_TRUE(transform.has_value()) << "\"--minify-identifiers\" was not accepted";

    const Passes got = Of(*transform);
    const Passes wanted{false, true, false};
    EXPECT_TRUE(got == wanted)
        << "a transform that was asked to mangle came out as " << Describe(got);
}

// The same four flags and the same independence, on the other option struct.
// Two tests rather than a loop over both, because one of them failing should
// say which side of the grammar stopped agreeing with itself.
TEST(CliMinify, TheShorthandReachesEveryPassOnATransformToo) {
    auto transform = TransformOptionsFor({"--minify"});
    ASSERT_TRUE(transform.has_value());

    const Passes wanted{true, true, true};
    const Passes got = Of(*transform);
    EXPECT_TRUE(got == wanted)
        << "\"--minify\" reached a transform as " << Describe(got);
}

} // namespace cli::test
