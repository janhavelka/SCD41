/**
 * @file I2cScanner.h
 * @brief Simple I2C bus scanner for example diagnostics.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "common/Log.h"

namespace i2c {

inline int scan(uint8_t preferredAddress = 0x62,
                uint16_t timeoutMs = 50) {
  LOGI("Scanning I2C bus...");

  // The driver leaves Wire's timeout at whatever its last transfer requested,
  // which can be as low as 1 ms. State the scanner's own bound so a probe
  // cannot time out and report a present sensor as absent.
  const uint16_t previousTimeout = Wire.getTimeOut();
  Wire.setTimeOut(timeoutMs);

  int count = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();

    if (error == 0U) {
      const char* marker = (address == preferredAddress) ? "  <target>" : "";
      LOG_SERIAL.printf("  Found device at 0x%02X%s\n", address, marker);
      count++;
    }
  }
  Wire.setTimeOut(previousTimeout);

  if (count == 0) {
    LOGW("No I2C devices found");
  } else {
    LOGI("Found %d device(s)", count);
  }

  return count;
}

}  // namespace i2c
