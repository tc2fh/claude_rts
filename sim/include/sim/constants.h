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
}
