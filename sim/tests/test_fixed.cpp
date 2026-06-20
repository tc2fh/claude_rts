#include <doctest/doctest.h>
#include "sim/fixed.h"

using namespace sim;

TEST_CASE("fix_from_int and back") {
    CHECK(fix_from_int(0) == 0);
    CHECK(fix_from_int(5) == (fix(5) << 32));
    CHECK(fix_to_float(fix_from_int(3)) == doctest::Approx(3.0f));
    CHECK(fix_to_float(fix_one / 2) == doctest::Approx(0.5f));
}

TEST_CASE("add/sub are exact") {
    fix a = fix_from_int(2), b = fix_from_int(3);
    CHECK(a + b == fix_from_int(5));
    CHECK(b - a == fix_from_int(1));
}

TEST_CASE("abs/clamp/sign") {
    CHECK(fix_abs(fix_from_int(-4)) == fix_from_int(4));
    CHECK(fix_clamp(fix_from_int(10), fix_from_int(0), fix_from_int(5)) == fix_from_int(5));
    CHECK(fix_clamp(fix_from_int(-1), fix_from_int(0), fix_from_int(5)) == fix_from_int(0));
    CHECK(fix_sign(fix_from_int(-7)) == -1);
    CHECK(fix_sign(fix_from_int(7)) == 1);
    CHECK(fix_sign(fix(0)) == 0);
}
