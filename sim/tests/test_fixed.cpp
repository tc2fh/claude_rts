#include <doctest/doctest.h>
#include "sim/world.h"

TEST_CASE("build smoke: abi_version is callable") {
    CHECK(sim::abi_version() == 0);
}
