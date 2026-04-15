/**
 * @file BoardConfig.h
 * @brief Example board configuration for ESP32-S2 / ESP32-S3 reference hardware.
 *
 * These are convenience defaults for reference designs only.
 * NOT part of the library API. Override them for your board.
 */

#pragma once

#include <stdint.h>

#include "common/I2cTransport.h"

namespace board {

static constexpr int I2C_SDA = 8;
static constexpr int I2C_SCL = 9;
static constexpr uint32_t I2C_FREQ_HZ = 400000;
static constexpr uint16_t I2C_TIMEOUT_MS = 50;
static constexpr int LED = 47;

inline bool initI2c() {
  return transport::initWire(I2C_SDA, I2C_SCL, I2C_FREQ_HZ, I2C_TIMEOUT_MS);
}

}  // namespace board
