/// @file Config.h
/// @brief Configuration structure for SCD41 driver
#pragma once

#include <cstddef>
#include <cstdint>

#include "SCD41/Status.h"

namespace SCD41 {

/// Transport capability flags
enum class TransportCapability : uint8_t {
  NONE = 0,
  READ_HEADER_NACK = 1 << 0, ///< Transport can reliably report read-header NACK
  TIMEOUT = 1 << 1,          ///< Transport can reliably report timeouts
  BUS_ERROR = 1 << 2         ///< Transport can reliably report bus errors
};

inline constexpr TransportCapability operator|(TransportCapability a, TransportCapability b) {
  return static_cast<TransportCapability>(
      static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline constexpr bool hasCapability(TransportCapability caps, TransportCapability cap) {
  return (static_cast<uint8_t>(caps) & static_cast<uint8_t>(cap)) != 0;
}

/// I2C write callback signature
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C read callback signature
/// @note The driver issues commands via i2cWrite() and later reads data via
///       i2cWriteRead() with txLen == 0. Combined write+read is not used.
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user);

/// Optional bus-reset callback
using BusResetFn = Status (*)(void* user);

/// Optional power-cycle callback
using PowerCycleFn = Status (*)(void* user);

/// Millisecond timestamp callback
using NowMsFn = uint32_t (*)(void* user);

/// Microsecond timestamp callback
using NowUsFn = uint32_t (*)(void* user);

/// Cooperative yield callback
using YieldFn = void (*)(void* user);

/// Single-shot request mode used by requestMeasurement()
enum class SingleShotMode : uint8_t {
  CO2_T_RH = 0, ///< Full single-shot measurement (5000 ms)
  T_RH_ONLY = 1 ///< Single-shot temperature/humidity only (50 ms, CO2 invalid)
};

/// Configuration for SCD41 driver
struct Config {
  // === I2C transport (required) ===
  I2cWriteFn i2cWrite = nullptr;         ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C read function pointer
  void* i2cUser = nullptr;               ///< User context for I2C callbacks
  BusResetFn busReset = nullptr;         ///< Optional I2C bus-reset callback
  PowerCycleFn powerCycle = nullptr;     ///< Optional sensor power-cycle callback
  void* controlUser = nullptr;           ///< User context for busReset/powerCycle

  // === Timing hooks (optional) ===
  NowMsFn nowMs = nullptr;            ///< Monotonic millisecond source
  NowUsFn nowUs = nullptr;            ///< Monotonic microsecond source
  YieldFn cooperativeYield = nullptr; ///< Cooperative scheduler hint
  void* timeUser = nullptr;           ///< User context for timing hooks

  // === Transport settings ===
  uint8_t i2cAddress = 0x62; ///< Fixed 7-bit SCD41 I2C address
  uint32_t i2cTimeoutMs = 50;
  TransportCapability transportCapabilities = TransportCapability::NONE;

  // === Driver behavior ===
  SingleShotMode singleShotMode = SingleShotMode::CO2_T_RH;
  uint16_t commandDelayMs = 1;      ///< Minimum inter-command gap, 1..1000 ms
  uint16_t powerUpDelayMs = 30;     ///< Delay after wake/power-cycle, 0..1000 ms
  uint32_t periodicFetchMarginMs = 0; ///< 0 = auto, otherwise 0..5000 ms
  uint32_t dataReadyRetryMs = 250;  ///< Retry delay after not-ready response
  uint32_t recoverBackoffMs = 100;  ///< Backoff between recover() attempts
  uint8_t offlineThreshold = 5;     ///< Consecutive I2C failures before OFFLINE
  bool strictVariantCheck = true;   ///< Fail begin() if serial-number variant != SCD41
};

} // namespace SCD41
