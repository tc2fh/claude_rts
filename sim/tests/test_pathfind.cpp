#include <doctest/doctest.h>
#include "sim/pathfind.h"
using namespace sim;

TEST_CASE("straight path on open ground starts and ends correctly") {
    Map m(0);
    auto p = find_path(m, {0, 0}, {5, 0});
    REQUIRE_FALSE(p.empty());
    CHECK(p.front() == GridPos{0, 0});
    CHECK(p.back()  == GridPos{5, 0});
}

TEST_CASE("path routes around the wall through the gap") {
    Map m(0);
    auto p = find_path(m, {11, 10}, {13, 10});
    REQUIRE_FALSE(p.empty());
    CHECK(p.front() == GridPos{11, 10});
    CHECK(p.back()  == GridPos{13, 10});
    for (auto c : p) CHECK(m.passable(c.x, c.y));
}

TEST_CASE("unreachable goal returns empty") {
    Map m(0);
    auto p = find_path(m, {0, 0}, {12, 5});   // goal is on the wall (impassable)
    CHECK(p.empty());
}

TEST_CASE("start == goal returns a single cell") {
    Map m(0);
    auto p = find_path(m, {3, 3}, {3, 3});
    REQUIRE(p.size() == 1);
    CHECK(p.front() == GridPos{3, 3});
}

TEST_CASE("A* is deterministic") {
    Map m(0);
    auto a = find_path(m, {1, 1}, {20, 18});
    auto b = find_path(m, {1, 1}, {20, 18});
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
}
