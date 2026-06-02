/// @file PlatformTime.h
/// @brief Private framework-neutral timing fallback for the SCD41 core.
#pragma once

#include <cstdint>

namespace SCD41 {
namespace platform {

inline uint32_t nowMs() {
  // Successful begin() requires Config::nowMs. This inert fallback keeps
  // pre-init diagnostic paths framework-neutral without including Arduino or ESP-IDF headers.
  return 0U;
}

inline uint32_t nowUs() {
  return 0U;
}

inline void cooperativeYield() {
}

}  // namespace platform
}  // namespace SCD41
