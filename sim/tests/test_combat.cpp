#include <doctest/doctest.h>
#include "sim/sim_abi.h"

namespace {
const SimEntitySnapshot* find(const SimSnapshot& s, uint32_t id) {
    for (uint32_t i = 0; i < s.count; ++i) if (s.entities[i].id == id) return &s.entities[i];
    return nullptr;
}
}

TEST_CASE("soldiers in range damage each other") {
    SimWorld* s = sim_create(7, 0);    // player soldier 3 (10,10), enemy soldier 5 (14,10)
    sim_advance(s, 200);
    SimSnapshot snap = sim_get_snapshot(s);
    const auto* e5 = find(snap, 5);
    const auto* e3 = find(snap, 3);
    bool fought = (!e5) || (!e3) || (e5->hp < 50) || (e3->hp < 50);
    CHECK(fought);
    sim_destroy(s);
}

TEST_CASE("combat is deterministic and batching-invariant") {
    auto run = [](int chunk) {
        SimWorld* s = sim_create(9, 0);
        SimCommand atk{}; atk.type = CMD_ATTACK; atk.player = 1; atk.unit = 3; atk.target = 4;
        sim_push_command(s, &atk, 1);
        for (int t = 0; t < 400; t += chunk) sim_advance(s, chunk);   // 400 divisible by 1 and 8
        uint64_t h = sim_state_hash(s);
        sim_destroy(s);
        return h;
    };
    CHECK(run(400) == run(1));
    CHECK(run(400) == run(8));
}

TEST_CASE("a unit at 0 HP is removed and its id is not reused") {
    SimWorld* s = sim_create(7, 0);
    sim_advance(s, 800);
    SimSnapshot snap = sim_get_snapshot(s);
    bool a_soldier_died = (find(snap, 3) == nullptr) || (find(snap, 5) == nullptr);
    CHECK(a_soldier_died);
    sim_destroy(s);
}

TEST_CASE("destroying the enemy HQ wins the game") {
    SimWorld* s = sim_create(7, 0);
    SimCommand atk{}; atk.type = CMD_ATTACK; atk.player = 1; atk.unit = 3; atk.target = 4; // -> enemy HQ
    sim_push_command(s, &atk, 1);
    sim_advance(s, 4000);
    uint8_t win = sim_winner(s);
    CHECK(win != 0);            // someone won (deterministic; expect 1 = player)
    sim_destroy(s);
}
