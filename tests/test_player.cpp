#include <gtest/gtest.h>

#include "lineup/player.hpp"
#include "lineup/roster.hpp"

using namespace lineup;

TEST(Projection, ParsesWholeNumbers) {
    int cp = 0;
    ASSERT_TRUE(parse_projection("18", cp));
    EXPECT_EQ(cp, 1800);
}

TEST(Projection, ParsesOneAndTwoDecimalPlaces) {
    int cp = 0;
    ASSERT_TRUE(parse_projection("18.4", cp));
    EXPECT_EQ(cp, 1840);
    ASSERT_TRUE(parse_projection("18.45", cp));
    EXPECT_EQ(cp, 1845);
}

TEST(Projection, IsExactWhereBinaryFloatingPointIsNot) {
    // 18.4 has no exact double representation; the fixed-point path must still
    // land on 1840 so that pruning comparisons stay order-independent.
    int a = 0, b = 0, c = 0;
    ASSERT_TRUE(parse_projection("18.4", a));
    ASSERT_TRUE(parse_projection("0.1", b));
    ASSERT_TRUE(parse_projection("0.2", c));
    EXPECT_EQ(a, 1840);
    EXPECT_EQ(b + c, 30);
}

TEST(Projection, RoundsHalfAwayFromZero) {
    int cp = 0;
    ASSERT_TRUE(parse_projection("1.005", cp));
    EXPECT_EQ(cp, 101);
    ASSERT_TRUE(parse_projection("1.004", cp));
    EXPECT_EQ(cp, 100);
}

TEST(Projection, HandlesSigns) {
    int cp = 0;
    ASSERT_TRUE(parse_projection("-3.5", cp));
    EXPECT_EQ(cp, -350);
    ASSERT_TRUE(parse_projection("+3.5", cp));
    EXPECT_EQ(cp, 350);
}

TEST(Projection, RejectsGarbage) {
    int cp = 0;
    EXPECT_FALSE(parse_projection("", cp));
    EXPECT_FALSE(parse_projection("abc", cp));
    EXPECT_FALSE(parse_projection("1.2.3", cp));
    EXPECT_FALSE(parse_projection("-", cp));
    EXPECT_FALSE(parse_projection("12x", cp));
}

TEST(PositionParsing, RoundTripsEveryPosition) {
    for (const char* text : {"QB", "RB", "WR", "TE", "DST"}) {
        Position p{};
        ASSERT_TRUE(parse_position(text, p)) << text;
        EXPECT_STREQ(to_string(p), text);
    }
}

TEST(PositionParsing, RejectsUnknownPositions) {
    Position p{};
    EXPECT_FALSE(parse_position("K", p));
    EXPECT_FALSE(parse_position("qb", p));  // case-sensitive by contract
    EXPECT_FALSE(parse_position("", p));
}

TEST(Roster, DraftKingsClassicHasNineSlotsAndAFlex) {
    const auto rules = draftkings_nfl_classic();
    ASSERT_EQ(rules.size(), 9u);
    EXPECT_EQ(rules.salary_cap, 50000);

    const Slot& flex = rules.slots[7];
    EXPECT_EQ(flex.label, "FLEX");
    EXPECT_TRUE(flex.allows(Position::RB));
    EXPECT_TRUE(flex.allows(Position::WR));
    EXPECT_TRUE(flex.allows(Position::TE));
    EXPECT_FALSE(flex.allows(Position::QB));
    EXPECT_FALSE(flex.allows(Position::DST));
}

TEST(Roster, DedicatedSlotsAcceptOnlyTheirPosition) {
    const auto rules = draftkings_nfl_classic();
    EXPECT_TRUE(rules.slots[0].allows(Position::QB));
    EXPECT_FALSE(rules.slots[0].allows(Position::RB));
    EXPECT_TRUE(rules.slots[8].allows(Position::DST));
    EXPECT_FALSE(rules.slots[8].allows(Position::WR));
}
