/*
 * sim/sim_abi.h — claude_rts model<->view seam (C ABI).
 *
 * CONTRACT OWNER: B (sim lane). Single source of truth for the snapshot/command
 * POD layouts and lifecycle calls the view (gdext/) consumes. Materialized by T
 * to bootstrap the build (mailbox B-4/B-5 ABI v0 + B-7 canonical state bitflags
 * and this namespaced path); B's sim/ PR refines it IN PLACE. T owns only the
 * marshaling that consumes this header, never the contract.
 *
 * Include as:  #include <sim/sim_abi.h>   (add sim/include to the include path)
 *
 * Determinism: world units are fixed-point Q32.32 in int64 — sim-internal AND
 * across this ABI. The view converts fix64 -> float for RENDERING ONLY and must
 * never feed a float back into the sim.
 */
#ifndef CLAUDE_RTS_SIM_ABI_H
#define CLAUDE_RTS_SIM_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t fix64_t;          /* Q32.32 fixed-point */
#define SIM_FIX_SHIFT 32

/* SimEntitySnapshot.state — combinable bitflags (canonical, mailbox B-7).
 * Combos are intentional: e.g. MOVING|CARRYING = a worker hauling. Map anims
 * off the bits. M0 emits IDLE/MOVING/ATTACKING/HARVESTING/CARRYING/DEAD;
 * BUILDING arrives with production. */
enum {
  SIM_STATE_IDLE       = 0,
  SIM_STATE_MOVING     = 1u << 0,
  SIM_STATE_ATTACKING  = 1u << 1,
  SIM_STATE_HARVESTING = 1u << 2,
  SIM_STATE_CARRYING   = 1u << 3,
  SIM_STATE_BUILDING   = 1u << 4,  /* structure under construction / worker constructing */
  SIM_STATE_DEAD       = 1u << 5
};

typedef struct {                  /* one per live entity per published frame (AoS, POD) */
  uint32_t id;                    /* stable entity id — not recycled within a match */
  uint16_t type;                  /* unit/structure/resource type id */
  uint8_t  owner;                 /* player id (0 = neutral) */
  uint8_t  state;                 /* SIM_STATE_* bitflags */
  fix64_t  x, y;                  /* position (Q32.32) */
  uint16_t facing;                /* 0..65535 = one full turn (view interpolates) */
  int32_t  hp, hp_max;
} SimEntitySnapshot;

typedef struct {                  /* valid only until the next sim_advance() — view must COPY (B-5) */
  uint64_t tick;
  const SimEntitySnapshot* entities;
  uint32_t count;
  int32_t  resources[8];          /* per-player primary resource (M0: minerals) */
} SimSnapshot;

typedef enum {
  CMD_MOVE, CMD_ATTACK, CMD_HARVEST, CMD_BUILD, CMD_TRAIN, CMD_STOP
} SimCommandType;

typedef struct {                  /* POD, view -> sim */
  SimCommandType type;
  uint8_t  player;
  uint32_t unit;                  /* primary actor (0 = none) */
  uint32_t target;                /* target entity (0 = none) */
  fix64_t  tx, ty;                /* target position (move/build) */
  uint16_t param;                 /* unit/structure type for train/build */
} SimCommand;

typedef struct {            /* static per match; query once after sim_create. */
    uint16_t w, h;
    const uint8_t* passable; /* w*h bytes, row-major (y*w + x): 1 = open, 0 = blocked. Sim-owned; do not free. */
} SimMapInfo;

typedef struct SimWorld SimWorld; /* opaque handle */

SimWorld*  sim_create(uint64_t seed, uint32_t map_id);
void       sim_destroy(SimWorld*);
void       sim_advance(SimWorld*, uint32_t ticks);                  /* N deterministic ticks (~24 Hz) */
uint64_t   sim_current_tick(const SimWorld*);
void       sim_push_command(SimWorld*, const SimCommand*, uint64_t exec_tick); /* exec_tick = lockstep input-delay */
SimSnapshot sim_get_snapshot(const SimWorld*);                      /* latest published, read-only */
uint64_t   sim_state_hash(const SimWorld*);                         /* determinism / desync oracle + test target */
SimMapInfo sim_get_map_info(const SimWorld*);                       /* static map; query once after sim_create */
uint8_t    sim_winner(const SimWorld*);                             /* 0=ongoing, 1=player1 wins, 2=player2 wins */

#ifdef __cplusplus
}
#endif

#endif /* CLAUDE_RTS_SIM_ABI_H */
