#include "sim/world.h"
#include "sim/components.h"
#include "sim/constants.h"
#include "sim/hash.h"
#include "sim/pathfind.h"
#include <algorithm>
#include <utility>

namespace sim {

static int cheb(int ax, int ay, int bx, int by) {
    const int dx = ax > bx ? ax - bx : bx - ax;
    const int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// ORD_STOP with the leash post (anchor) at the given cell.
static COrder stopped_at(int cx, int cy) {
    COrder o; o.anchor = GridPos{cx, cy}; return o;
}

World::World(std::uint64_t seed, std::uint32_t map_id) : map_(map_id), rng_(seed) {
    spawn_initial();
    publish_snapshot();
}

entt::entity World::spawn(CPos pos, CUnit unit) {
    auto e = reg_.create();
    reg_.emplace<CId>(e, CId{next_id_++});
    reg_.emplace<CPos>(e, pos);
    reg_.emplace<CUnit>(e, unit);
    return e;
}

void World::spawn_initial() {
    auto hq = spawn(CPos{Map::cell_to_world(4), Map::cell_to_world(4)},
                    CUnit{TYPE_HQ, 1, SIM_STATE_IDLE, 0, HQ_HP, HQ_HP});
    const EntityId hq_id = reg_.get<CId>(hq).id;
    reg_.emplace<CProducer>(hq, CProducer{});

    auto wk = spawn(CPos{Map::cell_to_world(5), Map::cell_to_world(5)},
                    CUnit{TYPE_WORKER, 1, SIM_STATE_IDLE, 0, 40, 40});
    reg_.emplace<CMobile>(wk, CMobile{fix_one / 8, {}, 0});
    reg_.emplace<CHarvester>(wk, CHarvester{HARV_IDLE, 0, 0, hq_id, 0});
    reg_.emplace<COrder>(wk, stopped_at(5, 5));

    auto node = spawn(CPos{Map::cell_to_world(8), Map::cell_to_world(8)},
                      CUnit{TYPE_RESOURCE, 0, SIM_STATE_IDLE, 0, 1, 1});
    reg_.emplace<CResource>(node, CResource{NODE_AMOUNT});

    // player soldier (id 3)
    auto ps = spawn(CPos{Map::cell_to_world(10), Map::cell_to_world(10)},
                    CUnit{TYPE_SOLDIER, 1, SIM_STATE_IDLE, 0, SOLDIER_HP, SOLDIER_HP});
    reg_.emplace<CMobile>(ps, CMobile{fix_one / 8, {}, 0});
    reg_.emplace<CWeapon>(ps, CWeapon{SOLDIER_DMG, SOLDIER_RANGE, SOLDIER_CD, 0, 0});
    reg_.emplace<COrder>(ps, stopped_at(10, 10));

    // enemy HQ (id 4)
    spawn(CPos{Map::cell_to_world(20), Map::cell_to_world(20)},
          CUnit{TYPE_HQ, 2, SIM_STATE_IDLE, 0, HQ_HP, HQ_HP});

    // enemy soldier (id 5) - a weak scout (hp 20, dies fast); standing attack order on player HQ
    auto es = spawn(CPos{Map::cell_to_world(14), Map::cell_to_world(10)},
                    CUnit{TYPE_SOLDIER, 2, SIM_STATE_IDLE, 0, 20, 20});
    reg_.emplace<CMobile>(es, CMobile{fix_one / 8, {}, 0});
    reg_.emplace<CWeapon>(es, CWeapon{SOLDIER_DMG, SOLDIER_RANGE, SOLDIER_CD, 0, 0});
    { COrder o; o.kind = ORD_ATTACK_TARGET; o.target = hq_id; reg_.emplace<COrder>(es, o); }
}

entt::entity World::find_by_id(EntityId id) {
    for (auto e : reg_.view<CId>()) if (reg_.get<CId>(e).id == id) return e;
    return entt::null;
}

void World::advance(std::uint32_t ticks) {
    for (std::uint32_t i = 0; i < ticks; ++i) step();
}

void World::step() {
    ++tick_;
    apply_commands_for(tick_);
    sys_harvest();
    sys_production();
    sys_combat();
    sys_movement();
    sys_death();
    publish_snapshot();
}

// Helper: complete the order for a unit that has just exhausted its path.
// For ORD_MOVE: always transition to ORD_STOP (path is only cleared by arrival).
// For ORD_ATTACK_MOVE/ORD_PATROL: only transition when the unit is actually at the
// destination cell - sys_combat may clear the path mid-flight (to fire in range)
// and we must not treat that as "arrived at goal".
static void maybe_complete_order(COrder& o, const CPos& p) {
    const int cx = Map::world_to_cell(p.x), cy = Map::world_to_cell(p.y);
    if (o.kind == ORD_MOVE) {
        o.kind = ORD_STOP; o.anchor = GridPos{cx, cy};
    } else if (o.kind == ORD_ATTACK_MOVE) {
        if (cx == o.dest.x && cy == o.dest.y) { o.kind = ORD_STOP; o.anchor = GridPos{cx, cy}; }
    } else if (o.kind == ORD_PATROL) {
        const GridPos ep = o.to_dest ? o.dest : o.anchor;
        if (cx == ep.x && cy == ep.y) o.to_dest = !o.to_dest;  // reached endpoint; head back next tick
    }
}

void World::sys_movement() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CMobile>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& m = reg_.get<CMobile>(e);
        auto& p = reg_.get<CPos>(e);
        auto& u = reg_.get<CUnit>(e);
        if (m.next >= m.path.size()) {
            u.state = SIM_STATE_IDLE;
            if (reg_.all_of<COrder>(e)) maybe_complete_order(reg_.get<COrder>(e), p);
            continue;
        }
        const fix tx = Map::cell_to_world(m.path[m.next].x);
        const fix ty = Map::cell_to_world(m.path[m.next].y);
        const fix dx = tx - p.x, dy = ty - p.y;
        if (fix_abs(dx) <= m.speed && fix_abs(dy) <= m.speed) {
            p.x = tx; p.y = ty;
            ++m.next;
            if (m.next >= m.path.size()) {
                u.state = SIM_STATE_IDLE;
                if (reg_.all_of<COrder>(e)) maybe_complete_order(reg_.get<COrder>(e), p);
            }
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
        auto set_order = [&](entt::entity e, const COrder& o) {
            if (reg_.all_of<COrder>(e)) reg_.get<COrder>(e) = o; else reg_.emplace<COrder>(e, o);
            if (reg_.all_of<CHarvester>(e)) reg_.get<CHarvester>(e).phase = HARV_IDLE;
        };

        if (c.type == CMD_STOP) {
            for (auto e : reg_.view<CId, CMobile, CPos>()) {
                if (reg_.get<CId>(e).id != c.unit) continue;
                const auto& p = reg_.get<CPos>(e);
                // Re-anchor: this spot becomes the new post.
                set_order(e, stopped_at(Map::world_to_cell(p.x), Map::world_to_cell(p.y)));
                auto& m = reg_.get<CMobile>(e);
                m.path.clear(); m.next = 0;
                break;
            }
        }
        else if (c.type == CMD_MOVE) {
            for (auto e : reg_.view<CId, CMobile, CPos>()) {
                if (reg_.get<CId>(e).id != c.unit) continue;
                const auto& p = reg_.get<CPos>(e);
                GridPos start{ Map::world_to_cell(p.x), Map::world_to_cell(p.y) };
                GridPos goal { Map::world_to_cell(c.tx), Map::world_to_cell(c.ty) };
                COrder o; o.kind = ORD_MOVE; o.dest = goal;
                set_order(e, o);
                auto& m = reg_.get<CMobile>(e);
                m.path = find_path(map_, start, goal);
                m.next = m.path.size() > 1 ? 1 : m.path.size();
                break;
            }
        }
        else if (c.type == CMD_TRAIN) {
            auto he = find_by_id(c.unit);
            if (he != entt::null && reg_.all_of<CProducer, CUnit>(he)) {
                auto& pr = reg_.get<CProducer>(he);
                const auto& hu = reg_.get<CUnit>(he);
                const std::uint16_t want = (c.param == 0) ? TYPE_WORKER : c.param;
                std::int32_t  cost  = 0;
                std::uint32_t build = 0;
                if (want == TYPE_WORKER)       { cost = WORKER_COST;  build = BUILD_TIME; }
                else if (want == TYPE_SOLDIER) { cost = SOLDIER_COST; build = SOLDIER_BUILD_TIME; }
                else continue;
                if (pr.train_type == 0 && resources_[hu.owner] >= cost) {
                    resources_[hu.owner] -= cost;
                    pr.train_type = want;
                    pr.timer = build;
                }
            }
        }
        else if (c.type == CMD_ATTACK) {
            auto ue = find_by_id(c.unit);
            if (ue != entt::null && reg_.all_of<CWeapon>(ue)) {
                COrder o; o.kind = ORD_ATTACK_TARGET; o.target = c.target;
                auto set_order_attack = [&](entt::entity e, const COrder& ord) {
                    if (reg_.all_of<COrder>(e)) reg_.get<COrder>(e) = ord; else reg_.emplace<COrder>(e, ord);
                    if (reg_.all_of<CHarvester>(e)) reg_.get<CHarvester>(e).phase = HARV_IDLE;
                };
                set_order_attack(ue, o);
            }
        }
        else if (c.type == CMD_ATTACK_MOVE) {
            for (auto e : reg_.view<CId, CMobile, CPos>()) {
                if (reg_.get<CId>(e).id != c.unit) continue;
                const auto& p = reg_.get<CPos>(e);
                GridPos start{ Map::world_to_cell(p.x), Map::world_to_cell(p.y) };
                GridPos goal { Map::world_to_cell(c.tx), Map::world_to_cell(c.ty) };
                COrder o; o.kind = ORD_ATTACK_MOVE; o.dest = goal;
                set_order(e, o);
                auto& m = reg_.get<CMobile>(e);
                m.path = find_path(map_, start, goal);
                m.next = m.path.size() > 1 ? 1 : m.path.size();
                break;
            }
        }
        else if (c.type == CMD_HOLD) {
            for (auto e : reg_.view<CId, CMobile>()) {
                if (reg_.get<CId>(e).id != c.unit) continue;
                COrder o; o.kind = ORD_HOLD;
                set_order(e, o);
                auto& m = reg_.get<CMobile>(e);
                m.path.clear(); m.next = 0;
                break;
            }
        }
        else if (c.type == CMD_PATROL) {
            for (auto e : reg_.view<CId, CMobile, CPos>()) {
                if (reg_.get<CId>(e).id != c.unit) continue;
                const auto& p = reg_.get<CPos>(e);
                GridPos start{ Map::world_to_cell(p.x), Map::world_to_cell(p.y) };
                GridPos goal { Map::world_to_cell(c.tx), Map::world_to_cell(c.ty) };
                COrder o; o.kind = ORD_PATROL; o.dest = goal; o.anchor = start; o.to_dest = true;
                set_order(e, o);
                auto& m = reg_.get<CMobile>(e);
                m.path = find_path(map_, start, goal);
                m.next = m.path.size() > 1 ? 1 : m.path.size();
                break;
            }
        }
        else if (c.type == CMD_HARVEST) {
            auto we = find_by_id(c.unit);
            auto ne = find_by_id(c.target);
            if (we != entt::null && ne != entt::null &&
                reg_.all_of<CHarvester, CMobile, CPos>(we) && reg_.all_of<CResource, CPos>(ne)) {
                auto& h = reg_.get<CHarvester>(we);
                auto& m = reg_.get<CMobile>(we);
                const auto& p  = reg_.get<CPos>(we);
                const auto& np = reg_.get<CPos>(ne);
                h.node = c.target; h.phase = HARV_TO_NODE;
                m.path = find_path(map_, {Map::world_to_cell(p.x), Map::world_to_cell(p.y)},
                                         {Map::world_to_cell(np.x), Map::world_to_cell(np.y)});
                m.next = m.path.size() > 1 ? 1 : m.path.size();
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
    for (int i = 0; i < 8; ++i) front_.resources[i] = resources_[i];
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
        if (reg.all_of<COrder>(e)) {
            const auto& o = reg.get<COrder>(e);
            h.add_u32(o.kind);
            h.add_u32(o.target);
        }
    }
    for (int i = 0; i < 8; ++i) h.add_i32(resources_[i]);
    h.add_u32(winner_);
    return h.value;
}

void World::emit_event(std::uint16_t type, std::uint32_t a, std::uint32_t b) {
    if (events_.size() < 4096) events_.push_back(SimEvent{type, a, b, tick_});
}

std::uint32_t World::drain_events(SimEvent* out, std::uint32_t max) {
    std::uint32_t n = static_cast<std::uint32_t>(events_.size());
    if (n > max) n = max;
    for (std::uint32_t i = 0; i < n; ++i) out[i] = events_[i];
    events_.erase(events_.begin(), events_.begin() + n);
    return n;
}

void World::sys_harvest() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CHarvester, CMobile, CPos, CUnit>())
        order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& h = reg_.get<CHarvester>(e);
        auto& m = reg_.get<CMobile>(e);
        auto& p = reg_.get<CPos>(e);
        auto& u = reg_.get<CUnit>(e);
        if (h.phase == HARV_IDLE) continue;  // orders cancelled harvest; skip
        const bool arrived = (m.next >= m.path.size());
        auto path_to = [&](EntityId target) {
            auto te = find_by_id(target);
            if (te == entt::null) return;
            const auto& tp = reg_.get<CPos>(te);
            m.path = find_path(map_, {Map::world_to_cell(p.x), Map::world_to_cell(p.y)},
                                     {Map::world_to_cell(tp.x), Map::world_to_cell(tp.y)});
            m.next = m.path.size() > 1 ? 1 : m.path.size();
        };
        switch (h.phase) {
            case HARV_TO_NODE:
                if (arrived) { h.phase = HARV_MINING; h.timer = MINE_TIME; }
                break;
            case HARV_MINING:
                if (h.timer > 0) --h.timer;
                if (h.timer == 0) {
                    auto ne = find_by_id(h.node);
                    if (ne != entt::null && reg_.all_of<CResource>(ne)) {
                        auto& res = reg_.get<CResource>(ne);
                        const std::int32_t take = res.amount < LOAD ? res.amount : LOAD;
                        res.amount -= take; h.carried = take;
                    }
                    path_to(h.hq);
                    h.phase = HARV_TO_HQ;
                }
                break;
            case HARV_TO_HQ:
                if (arrived) {
                    resources_[u.owner] += h.carried; h.carried = 0;
                    auto ne = find_by_id(h.node);
                    const std::int32_t remaining =
                        (ne != entt::null && reg_.all_of<CResource>(ne)) ? reg_.get<CResource>(ne).amount : 0;
                    if (remaining > 0) { path_to(h.node); h.phase = HARV_TO_NODE; }
                    else { h.phase = HARV_IDLE; }
                }
                break;
            default: break;
        }
    }
}

void World::sys_production() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CProducer, CPos, CUnit>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& pr = reg_.get<CProducer>(e);
        if (pr.train_type == 0) continue;
        if (pr.timer > 0) --pr.timer;
        if (pr.timer == 0) {
            const auto& hp = reg_.get<CPos>(e);
            const auto& hu = reg_.get<CUnit>(e);
            const int cx = Map::world_to_cell(hp.x) + 1, cy = Map::world_to_cell(hp.y) + 1;
            const CPos spawn_pos{Map::cell_to_world(cx), Map::cell_to_world(cy)};
            if (pr.train_type == TYPE_SOLDIER) {
                auto sdr = spawn(spawn_pos,
                                 CUnit{TYPE_SOLDIER, hu.owner, SIM_STATE_IDLE, 0, SOLDIER_HP, SOLDIER_HP});
                reg_.emplace<CMobile>(sdr, CMobile{fix_one / 8, {}, 0});
                reg_.emplace<CWeapon>(sdr, CWeapon{SOLDIER_DMG, SOLDIER_RANGE, SOLDIER_CD, 0, 0});
                reg_.emplace<COrder>(sdr, stopped_at(cx, cy));
                emit_event(SIM_EVT_TRAINED, id, reg_.get<CId>(sdr).id);
            } else {
                auto w = spawn(spawn_pos,
                               CUnit{TYPE_WORKER, hu.owner, SIM_STATE_IDLE, 0, 40, 40});
                reg_.emplace<CMobile>(w, CMobile{fix_one / 8, {}, 0});
                reg_.emplace<CHarvester>(w, CHarvester{HARV_IDLE, 0, 0, id, 0});
                reg_.emplace<COrder>(w, stopped_at(cx, cy));
                emit_event(SIM_EVT_TRAINED, id, reg_.get<CId>(w).id);
            }
            pr.train_type = 0;
        }
    }
}

void World::sys_combat() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CWeapon, CPos, CUnit, CMobile>())
        order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());

    std::vector<std::pair<EntityId, entt::entity>> cand;
    for (auto e : reg_.view<CId, CPos, CUnit>()) cand.push_back({reg_.get<CId>(e).id, e});
    std::sort(cand.begin(), cand.end());

    for (auto& [id, e] : order) {
        auto& w = reg_.get<CWeapon>(e);
        auto& m = reg_.get<CMobile>(e);
        const auto& p = reg_.get<CPos>(e);
        const auto& u = reg_.get<CUnit>(e);
        if (w.timer > 0) --w.timer;
        const int mx = Map::world_to_cell(p.x), my = Map::world_to_cell(p.y);

        const OrderKind kind = reg_.all_of<COrder>(e) ? reg_.get<COrder>(e).kind : ORD_STOP;

        // Passive move: ignore enemies; sys_movement drives toward dest.
        if (kind == ORD_MOVE) { w.target = 0; continue; }

        // Forced attack on a specific unit: chase + attack until it dies, then go defensive.
        if (kind == ORD_ATTACK_TARGET) {
            const EntityId tgt = reg_.get<COrder>(e).target;
            auto te = find_by_id(tgt);
            if (te == entt::null) {
                auto& o = reg_.get<COrder>(e); o.kind = ORD_STOP; o.target = 0; o.anchor = GridPos{mx, my};
                w.target = 0; m.path.clear(); m.next = 0; continue;
            }
            w.target = tgt;
            const auto& tp = reg_.get<CPos>(te);
            const int d = cheb(mx, my, Map::world_to_cell(tp.x), Map::world_to_cell(tp.y));
            if (d <= w.range_cells) {
                m.path.clear(); m.next = 0;
                if (w.timer == 0) { reg_.get<CUnit>(te).hp -= w.damage; w.timer = w.cooldown; emit_event(SIM_EVT_ATTACK, id, tgt); }
            } else {
                m.path = find_path(map_, {mx, my}, {Map::world_to_cell(tp.x), Map::world_to_cell(tp.y)});
                m.next = m.path.size() > 1 ? 1 : m.path.size();
            }
            continue;
        }

        // Aggressive move (attack-move / patrol): acquire + chase enemies in ACQUIRE_RANGE; when none, advance toward the order goal.
        if (kind == ORD_ATTACK_MOVE || kind == ORD_PATROL) {
            auto& o = reg_.get<COrder>(e);
            EntityId acquired = 0; int best = ACQUIRE_RANGE + 1;
            for (auto& [oid, oe] : cand) {
                const auto& ou = reg_.get<CUnit>(oe);
                if (ou.owner == u.owner || ou.owner == 0) continue;
                const auto& op = reg_.get<CPos>(oe);
                const int d = cheb(mx, my, Map::world_to_cell(op.x), Map::world_to_cell(op.y));
                if (d <= ACQUIRE_RANGE && d < best) { best = d; acquired = oid; }
            }
            if (acquired != 0) {
                w.target = acquired;
                auto te = find_by_id(acquired);
                const auto& tp = reg_.get<CPos>(te);
                const int d = cheb(mx, my, Map::world_to_cell(tp.x), Map::world_to_cell(tp.y));
                if (d <= w.range_cells) {
                    m.path.clear(); m.next = 0;
                    if (w.timer == 0) { reg_.get<CUnit>(te).hp -= w.damage; w.timer = w.cooldown; emit_event(SIM_EVT_ATTACK, id, acquired); }
                } else {
                    m.path = find_path(map_, {mx, my}, {Map::world_to_cell(tp.x), Map::world_to_cell(tp.y)});
                    m.next = m.path.size() > 1 ? 1 : m.path.size();
                }
            } else {
                w.target = 0;
                const GridPos goal = (kind == ORD_PATROL && !o.to_dest) ? o.anchor : o.dest;
                const bool heading = !m.path.empty() && m.path.back().x == goal.x && m.path.back().y == goal.y;
                if (!heading && (mx != goal.x || my != goal.y)) {
                    m.path = find_path(map_, {mx, my}, goal);
                    m.next = m.path.size() > 1 ? 1 : m.path.size();
                }
            }
            continue;
        }

        // Idle leash (ORD_STOP): step out to engage enemies in ACQUIRE_RANGE, chase up to
        // LEASH_RANGE (Chebyshev) from the post (anchor), otherwise return to the post.
        if (kind == ORD_STOP && reg_.all_of<COrder>(e)) {
            const auto& o = reg_.get<COrder>(e);
            const int dist_anchor = cheb(mx, my, o.anchor.x, o.anchor.y);
            EntityId acquired = 0; int best = ACQUIRE_RANGE + 1;
            for (auto& [oid, oe] : cand) {
                const auto& ou = reg_.get<CUnit>(oe);
                if (ou.owner == u.owner || ou.owner == 0) continue;
                const auto& op = reg_.get<CPos>(oe);
                const int d = cheb(mx, my, Map::world_to_cell(op.x), Map::world_to_cell(op.y));
                if (d <= ACQUIRE_RANGE && d < best) { best = d; acquired = oid; }
            }
            if (acquired != 0 && dist_anchor <= LEASH_RANGE) {
                w.target = acquired;
                auto te = find_by_id(acquired);
                const auto& tp = reg_.get<CPos>(te);
                const int d = cheb(mx, my, Map::world_to_cell(tp.x), Map::world_to_cell(tp.y));
                if (d <= w.range_cells) {
                    m.path.clear(); m.next = 0;
                    if (w.timer == 0) { reg_.get<CUnit>(te).hp -= w.damage; w.timer = w.cooldown; emit_event(SIM_EVT_ATTACK, id, acquired); }
                } else {
                    m.path = find_path(map_, {mx, my}, {Map::world_to_cell(tp.x), Map::world_to_cell(tp.y)});
                    m.next = m.path.size() > 1 ? 1 : m.path.size();
                }
            } else {
                w.target = 0;
                if (mx != o.anchor.x || my != o.anchor.y) {
                    const bool heading = !m.path.empty() && m.path.back().x == o.anchor.x && m.path.back().y == o.anchor.y;
                    if (!heading) {
                        m.path = find_path(map_, {mx, my}, o.anchor);
                        m.next = m.path.size() > 1 ? 1 : m.path.size();
                    }
                }
            }
            continue;
        }

        // Defensive (ORD_HOLD / no COrder): attack only what is already within
        // weapon range; never move toward an enemy.
        EntityId acquired = 0; int best = w.range_cells + 1;
        for (auto& [oid, oe] : cand) {
            const auto& ou = reg_.get<CUnit>(oe);
            if (ou.owner == u.owner || ou.owner == 0) continue;
            const auto& op = reg_.get<CPos>(oe);
            const int d = cheb(mx, my, Map::world_to_cell(op.x), Map::world_to_cell(op.y));
            if (d <= w.range_cells && d < best) { best = d; acquired = oid; }
        }
        w.target = acquired;
        if (acquired != 0 && w.timer == 0) {
            auto te = find_by_id(acquired);
            reg_.get<CUnit>(te).hp -= w.damage; w.timer = w.cooldown; emit_event(SIM_EVT_ATTACK, id, acquired);
        }
    }
}

void World::sys_death() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CUnit>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    std::vector<entt::entity> dead;
    for (auto& [id, e] : order) {
        const auto& u = reg_.get<CUnit>(e);
        if (u.hp <= 0) {
            if (u.type == TYPE_HQ && winner_ == 0) winner_ = (u.owner == 1) ? 2 : 1;
            dead.push_back(e);
            emit_event(SIM_EVT_DIED, id, 0);
        }
    }
    for (auto e : dead) reg_.destroy(e);
}

} // namespace sim
