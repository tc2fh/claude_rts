#include "sim/world.h"
#include "sim/components.h"
#include "sim/hash.h"
#include <algorithm>

namespace sim {

World::World(std::uint64_t seed, std::uint32_t /*map_id*/) : rng_(seed) {
    spawn_initial();
    publish_snapshot();
}

EntityId World::spawn(CPos pos, CVel vel, CUnit unit) {
    auto e = reg_.create();
    EntityId id = next_id_++;
    reg_.emplace<CId>(e, CId{id});
    reg_.emplace<CPos>(e, pos);
    reg_.emplace<CVel>(e, vel);
    reg_.emplace<CUnit>(e, unit);
    return id;
}

void World::spawn_initial() {
    for (int i = 0; i < 3; ++i) {
        fix vx = static_cast<fix>(rng_.next() & 0xFFFF);
        fix vy = static_cast<fix>(rng_.next() & 0xFFFF);
        spawn(CPos{fix_from_int(i), fix_from_int(i)},
              CVel{vx, vy},
              CUnit{/*type*/1, /*owner*/1, /*state*/0, /*facing*/0, /*hp*/100, /*hp_max*/100});
    }
}

void World::advance(std::uint32_t ticks) {
    for (std::uint32_t i = 0; i < ticks; ++i) step();
}

void World::step() {
    ++tick_;
    apply_commands_for(tick_);
    sys_drift();
    publish_snapshot();
}

void World::sys_drift() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& p = reg_.get<CPos>(e);
        const auto& v = reg_.get<CVel>(e);
        p.x += v.x;
        p.y += v.y;
    }
}

void World::push_command(const SimCommand& cmd, std::uint64_t exec_tick) {
    commands_.push_back({exec_tick, cmd});
}

void World::apply_commands_for(std::uint64_t t) {
    std::vector<SimCommand> due;
    for (auto& [et, c] : commands_) if (et == t) due.push_back(c);
    std::sort(due.begin(), due.end(), [](const SimCommand& a, const SimCommand& b) {
        if (a.player != b.player) return a.player < b.player;
        return a.unit < b.unit;
    });
    for (const auto& c : due) {
        if (c.type == CMD_STOP) {
            for (auto e : reg_.view<CId, CVel>())
                if (reg_.get<CId>(e).id == c.unit) reg_.get<CVel>(e) = CVel{0, 0};
        }
    }
}

void World::publish_snapshot() {
    int next = 1 - active_;
    auto& out = buf_[next];
    out.clear();
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        const auto& p = reg_.get<CPos>(e);
        const auto& u = reg_.get<CUnit>(e);
        out.push_back(SimEntitySnapshot{
            id, u.type, u.owner, u.state, p.x, p.y, u.facing, u.hp, u.hp_max});
    }
    front_.tick = tick_;
    front_.entities = out.data();
    front_.count = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 8; ++i) front_.resources[i] = 0;
    active_ = next;
}

std::uint64_t World::state_hash() const {
    auto& reg = const_cast<entt::registry&>(reg_);   // EnTT view()/get() are non-const
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg.view<CId>()) order.push_back({reg.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    Hasher h;
    h.add_u64(tick_);
    for (auto& [id, e] : order) {
        const auto& p = reg.get<CPos>(e);
        const auto& u = reg.get<CUnit>(e);
        h.add_u32(id);
        h.add_i64(p.x); h.add_i64(p.y);
        h.add_u32(u.type); h.add_u32(u.owner); h.add_u32(u.state); h.add_u32(u.facing);
        h.add_i32(u.hp);
    }
    return h.value;
}

} // namespace sim
