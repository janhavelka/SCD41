/// @file Config.h
/// @brief Configuration structure for SCD41 driver
#pragma once

#include <cstddef>
#include <cstdint>

#include "SCD41/Status.h"

namespace SCD41 {

/// Optional transport capabilities the driver can use for more precise error mapping.
enum class TransportCapability : uint8_t {
  NONE = 0,
  READ_HEADER_NACK = 1 << 0, ///< Transport can reliably report read-header NACK
  TIMEOUT = 1 << 1,          ///< Transport can reliably report timeouts
  BUS_ERROR = 1 << 2         ///< Transport can reliably report bus errors
};

/// Combine transport capability flags.
inline constexpr TransportCapability operator|(TransportCapability a, TransportCapability b) {
  return static_cast<TransportCapability>(
      static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/// Return true when the requested capability bit is present.
inline constexpr bool hasCapability(TransportCapability caps, TransportCapability cap) {
  return (static_cast<uint8_t>(caps) & static_cast<uint8_t>(cap)) != 0;
}

/// I2C write callback used for command-only and command-plus-data transfers.
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C read transaction callback used for data fetches after a command phase.
/// @note The current driver issues the command phase separately through `i2cWrite()` and then
///       performs the read phase with `txLen == 0`. Callers should not assume combined
///       write+read transactions are required.
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user);

/// Optional callback for recovering a shared I2C bus without power-cycling the sensor.
using BusResetFn = Status (*)(void* user);

/// Optional callback for power-cycling the sensor when bus recovery is insufficient.
using PowerCycleFn = Status (*)(void* user);

/// Monotonic millisecond time source used for `tick()` scheduling and bounded waits.
using NowMsFn = uint32_t (*)(void* user);

/// Monotonic microsecond time source used for the 1 ms inter-command guard.
using NowUsFn = uint32_t (*)(void* user);

/// Optional scheduler-friendly yield hook used inside bounded wait loops.
using YieldFn = void (*)(void* user);

/// Preferred single-shot mode used by `requestMeasurement()` while the sensor is idle.
enum class SingleShotMode : uint8_t {
  CO2_T_RH = 0, ///< Full single-shot measurement (5000 ms)
  T_RH_ONLY = 1 ///< Single-shot temperature/humidity only (50 ms, CO2 invalid)
};

/// Driver configuration.
/// @note The library never owns the I2C peripheral. The caller provides all transport and
///       optional recovery hooks through this structure.
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
  uint32_t i2cTimeoutMs = 50; ///< Timeout passed to transport callbacks
  TransportCapability transportCapabilities = TransportCapability::NONE; ///< Optional transport capability flags

  // === Driver behavior ===
  SingleShotMode singleShotMode = SingleShotMode::CO2_T_RH; ///< Preferred idle single-shot mode
  uint16_t commandDelayMs = 1;        ///< Minimum inter-command gap, 1..1000 ms
  uint16_t powerUpDelayMs = 30;       ///< Delay after wake/power-cycle, 0..1000 ms
  uint32_t periodicFetchMarginMs = 0; ///< 0 = auto, otherwise 0..5000 ms
  uint32_t dataReadyRetryMs = 250;    ///< Retry delay after not-ready response
  uint32_t recoverBackoffMs = 100;    ///< Backoff between recover() attempts
  uint8_t offlineThreshold = 5;       ///< Consecutive I2C failures before OFFLINE
  bool strictVariantCheck = true;     ///< Fail begin() if serial-number variant != SCD41
};

} // namespace SCD41
