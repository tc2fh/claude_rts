#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// World units are fixed-point Q32.32 in int64 across the ABI. The view converts to
// float for rendering ONLY — it never feeds float back into the sim.
typedef int64_t sim_fix;

typedef struct {            // one per live entity per published frame (POD, AoS)
    uint32_t id;            // stable; not recycled within a match
    uint16_t type;
    uint8_t  owner;         // 0 = neutral
    uint8_t  state;         // bitflags
    sim_fix  x, y;
    uint16_t facing;        // 0..65535 = full turn
    int32_t  hp, hp_max;
} SimEntitySnapshot;

// SimEntitySnapshot.state — combinable bitflags (e.g., MOVING|CARRYING = worker hauling).
enum {
    SIM_STATE_IDLE       = 0,
    SIM_STATE_MOVING     = 1u << 0,
    SIM_STATE_ATTACKING  = 1u << 1,
    SIM_STATE_HARVESTING = 1u << 2,
    SIM_STATE_CARRYING   = 1u << 3,
    SIM_STATE_BUILDING   = 1u << 4,
    SIM_STATE_DEAD       = 1u << 5
};

typedef struct {            // LIFETIME: valid only until the next sim_advance().
    uint64_t tick;
    const SimEntitySnapshot* entities;
    uint32_t count;
    int32_t  resources[8];
} SimSnapshot;

typedef enum { CMD_MOVE, CMD_ATTACK, CMD_HARVEST, CMD_BUILD, CMD_TRAIN, CMD_STOP } SimCommandType;
typedef struct {            // POD, view -> sim
    SimCommandType type;
    uint8_t  player;
    uint32_t unit;
    uint32_t target;
    sim_fix  tx, ty;
    uint16_t param;
} SimCommand;

typedef struct SimWorld SimWorld;   // opaque handle

SimWorld*   sim_create(uint64_t seed, uint32_t map_id);
void        sim_destroy(SimWorld*);
void        sim_advance(SimWorld*, uint32_t ticks);
uint64_t    sim_current_tick(const SimWorld*);
void        sim_push_command(SimWorld*, const SimCommand*, uint64_t exec_tick);
SimSnapshot sim_get_snapshot(const SimWorld*);
uint64_t    sim_state_hash(const SimWorld*);

#ifdef __cplusplus
}
#endif
