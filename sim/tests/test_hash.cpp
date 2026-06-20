#include <doctest/doctest.h>
#include "sim/hash.h"
#include <cstdint>

using namespace sim;

TEST_CASE("fnv1a is order-sensitive and stable") {
    Hasher h1; h1.add_u64(1); h1.add_u64(2);
    Hasher h2; h2.add_u64(1); h2.add_u64(2);
    Hasher h3; h3.add_u64(2); h3.add_u64(1);
    CHECK(h1.value == h2.value);
    CHECK(h1.value != h3.value);
}

TEST_CASE("empty hash is the offset basis") {
    Hasher h;
    CHECK(h.value == 0xcbf29ce484222325ULL);
}
