#include "sim/world.h"
#include "sim/components.h"
#include "sim/hash.h"
#include "sim/pathfind.h"
#include <algorithm>
#include <utility>

namespace sim {

World::World(std::uint64_t seed, std::uint32_t map_id) : map_(map_id), rng_(seed) {
    spawn_initial();
    publish_snapshot();
}

EntityId World::spawn(CPos pos, CMobile mob, CUnit unit) {
    auto e = reg_.create();
    EntityId id = next_id_++;
    reg_.emplace<CId>(e, CId{id});
    reg_.emplace<CPos>(e, pos);
    reg_.emplace<CMobile>(e, std::move(mob));
    reg_.emplace<CUnit>(e, unit);
    return id;
}

void World::spawn_initial() {
    const fix speed = fix_one / 8;   // 0.125 cell/tick
    CUnit u{/*type*/1, /*owner*/1, /*state*/SIM_STATE_IDLE, /*facing*/0, /*hp*/100, /*hp_max*/100};
    spawn(CPos{Map::cell_to_world(2), Map::cell_to_world(2)}, CMobile{speed, {}, 0}, u);
    spawn(CPos{Map::cell_to_world(3), Map::cell_to_world(3)}, CMobile{speed, {}, 0}, u);
}

void World::advance(std::uint32_t ticks) {
    for (std::uint32_t i = 0; i < ticks; ++i) step();
}

void World::step() {
    ++tick_;
    apply_commands_for(tick_);
    sys_movement();
    publish_snapshot();
}

void World::sys_movement() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CMobile>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& m = reg_.get<CMobile>(e);
        auto& p = reg_.get<CPos>(e);
        auto& u = reg_.get<CUnit>(e);
        if (m.next >= m.path.size()) { u.state = SIM_STATE_IDLE; continue; }
        const fix tx = Map::cell_to_world(m.path[m.next].x);
        const fix ty = Map::cell_to_world(m.path[m.next].y);
        const fix dx = tx - p.x, dy = ty - p.y;
        if (fix_abs(dx) <= m.speed && fix_abs(dy) <= m.speed) {
            p.x = tx; p.y = ty;
            ++m.next;
            if (m.next >= m.path.size()) u.state = SIM_STATE_IDLE;
        } else {
            p.x += fix_clamp(dx, -m.speed, m.speed);
            p.y += fix_clamp(dy, -m.speed, m.speed);
            u.state = SIM_STATE_MOVING;
        }
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
            for (auto e : reg_.view<CId, CMobile>())
                if (reg_.get<CId>(e).id == c.unit) {
                    auto& m = reg_.get<CMobile>(e);
                    m.path.clear(); m.next = 0;
                }
        }
        else if (c.type == CMD_MOVE) {
            for (auto e : reg_.view<CId, CMobile, CPos>()) {
                if (reg_.get<CId>(e).id != c.unit) continue;
                const auto& p = reg_.get<CPos>(e);
                GridPos start{ Map::world_to_cell(p.x), Map::world_to_cell(p.y) };
                GridPos goal { Map::world_to_cell(c.tx), Map::world_to_cell(c.ty) };
                auto& m = reg_.get<CMobile>(e);
                m.path = find_path(map_, start, goal);
                m.next = m.path.size() > 1 ? 1 : m.path.size();   // skip the start cell
                break;
            }
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
    auto& reg = const_cast<entt::registry&>(reg_);
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
