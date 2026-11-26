// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace hakios::metal::target {
constexpr uint32_t kChipId = 0x05030004;
constexpr uint32_t kRegidNone = 0xfc; // Mesa ir3 INVALID_REG = regid(63,0)
constexpr unsigned kGeneration = 5;
constexpr const char* kCacheNamespace = "lithium-a530-a5xx-v1";
}
