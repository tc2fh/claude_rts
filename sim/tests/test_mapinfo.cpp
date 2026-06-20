#include <doctest/doctest.h>
#include "sim/sim_abi.h"

TEST_CASE("ABI exposes the read-only static map") {
    SimWorld* s = sim_create(7, 0);
    SimMapInfo mi = sim_get_map_info(s);
    CHECK(mi.w == 24);
    CHECK(mi.h == 24);
    REQUIRE(mi.passable != nullptr);
    CHECK(mi.passable[10 * 24 + 12] == 1);   // the gap in the wall
    CHECK(mi.passable[5 * 24 + 12]  == 0);   // the wall
    CHECK(mi.passable[0]            == 1);   // open corner
    sim_destroy(s);
}
