#pragma once
#include <cstdint>
namespace sim {
inline constexpr std::uint16_t TYPE_WORKER   = 1;
inline constexpr std::uint16_t TYPE_HQ       = 2;
inline constexpr std::uint16_t TYPE_RESOURCE = 3;
inline constexpr std::uint16_t TYPE_SOLDIER  = 4;   // reserved for 2c
inline constexpr std::uint32_t MINE_TIME   = 16;
inline constexpr std::int32_t  LOAD        = 5;
inline constexpr std::int32_t  WORKER_COST = 50;
inline constexpr std::uint32_t BUILD_TIME  = 48;
inline constexpr std::int32_t  NODE_AMOUNT = 500;
inline constexpr std::int32_t  SOLDIER_HP    = 50;
inline constexpr std::int32_t  SOLDIER_DMG   = 10;
inline constexpr int           SOLDIER_RANGE = 4;     // Chebyshev cells
inline constexpr std::uint32_t SOLDIER_CD    = 12;    // attack cooldown (ticks)
inline constexpr int           ACQUIRE_RANGE = 7;     // cells
inline constexpr std::int32_t  HQ_HP         = 200;
inline constexpr std::int32_t  SOLDIER_COST       = 75;   // mineral cost to train a soldier
inline constexpr std::uint32_t SOLDIER_BUILD_TIME = 72;   // ticks to train a soldier (~3s @ 24Hz)
}
