#include <catch2/catch_test_macros.hpp>

#include "game/high_scores.h"

#include <array>
#include <string>

using bumpy::HighScoreTable;

namespace {
std::string name_of(const HighScoreTable& t, std::size_t row) {
    const auto& n = t.entry(row).name;
    return std::string(n.begin(), n.end());
}
}  // namespace

TEST_CASE("the default table is the 7 baked entries in descending order") {
    const HighScoreTable t;
    REQUIRE(t.entries().size() == 7);
    REQUIRE(name_of(t, 0) == "BIG JIM.");
    REQUIRE(t.entry(0).score == 5000000u);
    REQUIRE(name_of(t, 2) == "STEVE...");
    REQUIRE(t.entry(2).score == 1000000u);
    REQUIRE(name_of(t, 6) == "MIKE....");
    REQUIRE(t.entry(6).score == 500u);
}

TEST_CASE("qualifies finds the first row the score beats, strictly") {
    const HighScoreTable t;
    REQUIRE(t.qualifies(499) == -1);        // below the last entry (500)
    REQUIRE(t.qualifies(500) == -1);        // ties do not insert (strict >)
    REQUIRE(t.qualifies(501) == 6);         // beats MIKE only
    REQUIRE(t.qualifies(30001) == 4);       // beats JOHNNY (30000)
    REQUIRE(t.qualifies(6000000) == 0);     // beats the top
}

TEST_CASE("insert shifts lower rows down, seeds AAAAAAAA, drops the last") {
    HighScoreTable t;
    const int row = t.insert(2000000);      // beats STEVE (row 2)
    REQUIRE(row == 2);
    REQUIRE(name_of(t, 2) == "AAAAAAAA");
    REQUIRE(t.entry(2).score == 2000000u);
    REQUIRE(name_of(t, 3) == "STEVE...");   // STEVE pushed to row 3
    REQUIRE(t.entry(3).score == 1000000u);
    REQUIRE(name_of(t, 6) == "FRANK...");   // old row 6 (MIKE) dropped
    REQUIRE(t.entry(6).score == 4000u);
}

TEST_CASE("insert at the last row replaces it and shifts nothing") {
    HighScoreTable t;
    const int row = t.insert(1000);         // beats MIKE (500) only
    REQUIRE(row == 6);
    REQUIRE(name_of(t, 6) == "AAAAAAAA");
    REQUIRE(t.entry(6).score == 1000u);
    REQUIRE(name_of(t, 5) == "FRANK...");   // untouched
}

TEST_CASE("insert of a non-qualifying score is a no-op returning -1") {
    HighScoreTable t;
    REQUIRE(t.insert(100) == -1);
    REQUIRE(name_of(t, 6) == "MIKE....");
    REQUIRE(t.entry(6).score == 500u);
}
