#include <catch2/catch_test_macros.hpp>
#include "resources/adlib_bank.h"

TEST_CASE("BUMPY.BNK decodes the AdLib instrument bank") {
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    REQUIRE(bank.size() == 160);

    // Name index: name[0] = "rol000" -> instrument slot 27.
    const auto* rol000 = bank.by_name("rol000");
    REQUIRE(rol000 != nullptr);
    REQUIRE(rol000 == &bank.instrument(27));

    // rol000 raw fields (confirmed from the file).
    REQUIRE(rol000->mode == 0);
    REQUIRE(rol000->mod.ksl == 1);
    REQUIRE(rol000->mod.mult == 1);
    REQUIRE(rol000->mod.feedback == 3);
    REQUIRE(rol000->mod.attack == 11);
    REQUIRE(rol000->mod.level == 15);
    REQUIRE(rol000->mod.connection == 1);
    REQUIRE(rol000->car.ksl == 0);
    REQUIRE(rol000->car.feedback == 12);
    REQUIRE(rol000->car.attack == 9);
    REQUIRE(rol000->car.decay == 2);
    REQUIRE(rol000->wave_mod == 0);
    REQUIRE(rol000->wave_car == 0);

    // A missing name returns nullptr.
    REQUIRE(bank.by_name("nope__") == nullptr);
}

TEST_CASE("BUMPY.BNK name index honors the per-record used flag") {
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    REQUIRE(bank.size() == 160);

    // The header's used count is 129, and every used name record maps to a
    // distinct instrument slot, so exactly 129 instruments carry a name.
    std::size_t named = 0;
    for (std::size_t i = 0; i < bank.size(); ++i) {
        if (!bank.instrument(i).name.empty()) ++named;
    }
    REQUIRE(named == 129);

    // Records 21, 22 and 51 are deleted (used==0) but still hold mangled names
    // in the file ("-ol015_", "-ol016_", "-ol048_"). They must stay unnamed and
    // must not be resolvable by their mangled name.
    REQUIRE(bank.instrument(21).name.empty());
    REQUIRE(bank.instrument(22).name.empty());
    REQUIRE(bank.instrument(51).name.empty());
    REQUIRE(bank.by_name("-ol015_") == nullptr);
    REQUIRE(bank.by_name("-ol016_") == nullptr);
    REQUIRE(bank.by_name("-ol048_") == nullptr);
}
