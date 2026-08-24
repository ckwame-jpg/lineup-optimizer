#include <gtest/gtest.h>

#include <sstream>

#include "lineup/csv.hpp"

using namespace lineup;

namespace {
CsvResult parse(const std::string& text) {
    std::istringstream in(text);
    return read_players(in);
}
}  // namespace

TEST(Csv, ReadsAWellFormedFile) {
    const auto r = parse(
        "name,position,salary,projection\n"
        "Alpha,QB,7000,21.5\n"
        "Bravo,RB,6200,15.25\n");

    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.players.size(), 2u);
    EXPECT_EQ(r.players[0].name, "Alpha");
    EXPECT_EQ(r.players[0].position, Position::QB);
    EXPECT_EQ(r.players[0].salary, 7000);
    EXPECT_EQ(r.players[0].projection_centipoints, 2150);
    EXPECT_EQ(r.players[1].projection_centipoints, 1525);
}

TEST(Csv, AcceptsColumnsInAnyOrder) {
    const auto r = parse(
        "projection,name,team,salary,position\n"
        "18.0,Charlie,KC,5400,WR\n");

    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.players.size(), 1u);
    EXPECT_EQ(r.players[0].name, "Charlie");
    EXPECT_EQ(r.players[0].team, "KC");
    EXPECT_EQ(r.players[0].position, Position::WR);
    EXPECT_EQ(r.players[0].projection_centipoints, 1800);
}

TEST(Csv, HeaderIsCaseInsensitive) {
    const auto r = parse("Name,POSITION,Salary,Projection\nDelta,TE,4000,9.5\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.players[0].position, Position::TE);
}

TEST(Csv, KeepsCommasInsideQuotedNames) {
    const auto r = parse(
        "name,position,salary,projection\n"
        "\"Echo, Jr.\",RB,5000,12.0\n");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.players[0].name, "Echo, Jr.");
    EXPECT_EQ(r.players[0].salary, 5000);
}

TEST(Csv, UnescapesDoubledQuotes) {
    const auto r = parse(
        "name,position,salary,projection\n"
        "\"Foxtrot \"\"The Wall\"\"\",DST,2800,7.0\n");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.players[0].name, "Foxtrot \"The Wall\"");
}

TEST(Csv, SkipsBlankLines) {
    const auto r = parse(
        "name,position,salary,projection\n"
        "\n"
        "Golf,QB,7100,20.0\n"
        "   \n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.players.size(), 1u);
}

TEST(Csv, AcceptsDEFAsAnAliasForDST) {
    const auto r = parse("name,position,salary,projection\nHotel,DEF,2400,6.0\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.players[0].position, Position::DST);
}

TEST(Csv, RejectsAMissingRequiredColumn) {
    const auto r = parse("name,position,salary\nIndia,QB,7000\n");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("projection"), std::string::npos);
}

TEST(Csv, ReportsTheOffendingLineNumber) {
    const auto r = parse(
        "name,position,salary,projection\n"
        "Juliet,QB,7000,20.0\n"
        "Kilo,PUNTER,3000,4.0\n");

    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("line 3"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("PUNTER"), std::string::npos) << r.error;
}

TEST(Csv, RejectsANonNumericSalary) {
    const auto r = parse("name,position,salary,projection\nLima,QB,seven,20.0\n");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("salary"), std::string::npos);
}

TEST(Csv, RejectsATruncatedRow) {
    const auto r = parse(
        "name,position,salary,projection\n"
        "Mike,QB,7000\n");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("columns"), std::string::npos) << r.error;
}

TEST(Csv, RejectsAnEmptyFile) {
    const auto r = parse("");
    EXPECT_FALSE(r.ok);
}
