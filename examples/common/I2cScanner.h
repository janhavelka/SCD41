/**
 * @file I2cScanner.h
 * @brief Simple I2C bus scanner for example diagnostics.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "common/Log.h"

namespace i2c {

inline int scan(uint8_t preferredAddress = 0x62) {
  LOGI("Scanning I2C bus...");

  int count = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();

    if (error == 0U) {
      const char* marker = (address == preferredAddress) ? "  <target>" : "";
      Serial.printf("  Found device at 0x%02X%s\n", address, marker);
      count++;
    }
  }

  if (count == 0) {
    LOGW("No I2C devices found");
  } else {
    LOGI("Found %d device(s)", count);
  }

  return count;
}

inline bool checkAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0U;
}

}  // namespace i2c
