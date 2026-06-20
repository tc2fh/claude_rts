#include <doctest/doctest.h>
#include "sim/sim_abi.h"
#include <vector>
#include <tuple>

// ---------------------------------------------------------------------------
// Test 1: combat emits SIM_EVT_ATTACK; enemy scout death emits SIM_EVT_DIED
// ---------------------------------------------------------------------------
TEST_CASE("combat emits SIM_EVT_ATTACK and a death emits SIM_EVT_DIED") {
    SimWorld* s = sim_create(7, 0);   // HQ=0, worker=1, node=2, player-soldier=3, enemy-HQ=4, enemy-scout=5

    std::vector<SimEvent> all;
    SimEvent buf[256];
    for (int t = 0; t < 120; ++t) {
        sim_advance(s, 1);
        uint32_t n = sim_drain_events(s, buf, 256);
        for (uint32_t i = 0; i < n; ++i) all.push_back(buf[i]);
    }

    bool got_attack = false;
    bool got_died   = false;
    for (const auto& ev : all) {
        if (ev.type == SIM_EVT_ATTACK) got_attack = true;
        if (ev.type == SIM_EVT_DIED)   got_died   = true;
    }

    CHECK(got_attack);
    CHECK(got_died);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 2: training emits SIM_EVT_TRAINED
// ---------------------------------------------------------------------------
TEST_CASE("training emits SIM_EVT_TRAINED") {
    SimWorld* s = sim_create(7, 0);

    // Harvest to accumulate resources
    SimCommand h{};
    h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);

    // Advance to accumulate >= 50 resources
    SimEvent buf[256];
    for (int t = 0; t < 1500; ++t) {
        sim_advance(s, 1);
        sim_drain_events(s, buf, 256);   // drain and discard
    }

    REQUIRE(sim_get_snapshot(s).resources[1] >= 50);

    // Issue train worker command
    SimCommand tr{};
    tr.type   = CMD_TRAIN;
    tr.player = 1;
    tr.unit   = 0;   // HQ id = 0
    tr.param  = 0;   // default -> worker
    sim_push_command(s, &tr, sim_current_tick(s) + 1);

    std::vector<SimEvent> all;
    for (int t = 0; t < 60; ++t) {
        sim_advance(s, 1);
        uint32_t n = sim_drain_events(s, buf, 256);
        for (uint32_t i = 0; i < n; ++i) all.push_back(buf[i]);
    }

    bool found = false;
    for (const auto& ev : all) {
        if (ev.type == SIM_EVT_TRAINED && ev.a == 0 && ev.b >= 6) {
            found = true;
            break;
        }
    }
    CHECK(found);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 3: drain clears the queue
// ---------------------------------------------------------------------------
TEST_CASE("drain clears the queue") {
    SimWorld* s = sim_create(7, 0);

    // Advance until we get at least one event
    SimEvent buf[256];
    uint32_t first = 0;
    for (int t = 0; t < 200 && first == 0; ++t) {
        sim_advance(s, 1);
        first = sim_drain_events(s, buf, 256);
    }

    REQUIRE(first > 0);

    // Immediate second drain must return 0
    uint32_t second = sim_drain_events(s, buf, 256);
    CHECK(second == 0);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 4: event stream is deterministic and batching-invariant
// ---------------------------------------------------------------------------
TEST_CASE("event stream is deterministic and batching-invariant") {
    using EvTuple = std::tuple<uint16_t, uint32_t, uint32_t, uint64_t>;

    auto run = [](int chunk) -> std::vector<EvTuple> {
        SimWorld* s = sim_create(9, 0);
        std::vector<EvTuple> events;
        SimEvent buf[256];
        int t = 0;
        while (t < 300) {
            int step = chunk;
            sim_advance(s, static_cast<uint32_t>(step));
            uint32_t n = sim_drain_events(s, buf, 256);
            for (uint32_t i = 0; i < n; ++i)
                events.emplace_back(buf[i].type, buf[i].a, buf[i].b, buf[i].tick);
            t += step;
        }
        sim_destroy(s);
        return events;
    };

    auto r1   = run(1);
    auto r3   = run(3);
    auto r5   = run(5);
    auto r300 = run(300);

    // 300 is divisible by 1, 3, 5 — all produce the same total ticks
    CHECK(r1   == r300);
    CHECK(r3   == r300);
    CHECK(r5   == r300);
}
