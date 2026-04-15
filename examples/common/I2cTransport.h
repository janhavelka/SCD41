/**
 * @file I2cTransport.h
 * @brief Wire-based I2C transport adapter for examples.
 *
 * This is example-only glue. The library itself must not own Wire.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "common/DriverCompat.h"

namespace transport {

using app_driver::Err;
using app_driver::Status;

inline bool initWire(int sda, int scl, uint32_t freqHz, uint32_t timeoutMs) {
  Wire.begin(sda, scl);
  Wire.setClock(freqHz);
  Wire.setTimeOut(timeoutMs);
  return true;
}

inline Status wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                        uint32_t timeoutMs, void* user) {
  (void)timeoutMs;

  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Wire instance is null");
  }

  wire->beginTransmission(addr);
  const size_t written = wire->write(data, len);
  const uint8_t result = wire->endTransmission(true);

  if (result != 0U) {
    switch (result) {
      case 1: return Status::Error(Err::INVALID_PARAM, "I2C write too long", result);
      case 2: return Status::Error(Err::I2C_NACK_ADDR, "I2C NACK addr", result);
      case 3: return Status::Error(Err::I2C_NACK_DATA, "I2C NACK data", result);
      case 4: return Status::Error(Err::I2C_BUS, "I2C bus error", result);
      case 5: return Status::Error(Err::I2C_TIMEOUT, "I2C timeout", result);
      default: return Status::Error(Err::I2C_ERROR, "I2C write failed", result);
    }
  }

  if (written != len) {
    return Status::Error(Err::I2C_ERROR, "I2C write incomplete", static_cast<int32_t>(written));
  }

  return Status::Ok();
}

inline Status wireWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                            uint8_t* rxData, size_t rxLen,
                            uint32_t timeoutMs, void* user) {
  (void)timeoutMs;

  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Wire instance is null");
  }

  if (txLen > 0U) {
    return Status::Error(Err::INVALID_PARAM, "Combined write+read not supported");
  }

  if (rxLen == 0U) {
    return Status::Ok();
  }

  const size_t received = wire->requestFrom(addr, rxLen);
  if (received == 0U) {
    return Status::Error(Err::I2C_ERROR, "I2C read returned 0 bytes", 0);
  }
  if (received != rxLen) {
    for (size_t i = 0; i < received; ++i) {
      (void)wire->read();
    }
    return Status::Error(Err::I2C_ERROR, "I2C read incomplete", static_cast<int32_t>(received));
  }

  for (size_t i = 0; i < rxLen; ++i) {
    rxData[i] = static_cast<uint8_t>(wire->read());
  }

  return Status::Ok();
}

}  // namespace transport
