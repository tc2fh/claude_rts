#include <doctest/doctest.h>
#include "sim/rng.h"

using namespace sim;

TEST_CASE("same seed yields same sequence") {
    Rng a{1234}, b{1234};
    for (int i = 0; i < 100; ++i) CHECK(a.next() == b.next());
}

TEST_CASE("different seeds diverge") {
    Rng a{1}, b{2};
    CHECK(a.next() != b.next());
}

TEST_CASE("range is bounded and reproducible") {
    Rng a{42}, b{42};
    CHECK(a.range(10) == b.range(10));
    CHECK(a.range(10) < 10u);
}
