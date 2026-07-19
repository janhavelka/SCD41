/**
 * @file BusDiag.h
 * @brief Simple example-only I2C bus diagnostics.
 */

#pragma once

#include "common/I2cScanner.h"

namespace bus_diag {

inline void scan(uint8_t preferredAddress = 0x62) {
  i2c::scan(preferredAddress);
}

}  // namespace bus_diag
