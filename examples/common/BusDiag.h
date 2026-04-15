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

inline bool probe(uint8_t address = 0x62) {
  return i2c::checkAddress(address);
}

}  // namespace bus_diag
