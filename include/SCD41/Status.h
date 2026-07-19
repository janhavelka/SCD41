/// @file Status.h
/// @brief Fixed, allocation-free status values for the SCD41 driver.
#pragma once

#include <cstdint>

namespace SCD41 {

enum class Err : uint8_t {
  OK = 0,
  NOT_INITIALIZED,
  INVALID_CONFIG,
  I2C_ERROR,
  TIMEOUT,
  INVALID_PARAM,
  DEVICE_NOT_FOUND,
  CRC_MISMATCH,
  MEASUREMENT_NOT_READY,
  CONVERSION_NOT_READY = MEASUREMENT_NOT_READY,
  BUSY,
  IN_PROGRESS,
  COMMAND_FAILED,
  UNSUPPORTED,
  I2C_NACK_ADDR,
  I2C_NACK_DATA,
  I2C_NACK_READ,
  I2C_TIMEOUT,
  I2C_BUS,
  OFFLINE, ///< Passive diagnostic state only; it never gates transfers.

  // Append-only operation-model additions.
  RESULT_NOT_READY,
  STALE_RESULT,
  CANCELLED,
  PARTIAL,
  INDETERMINATE,
  CONFIRMATION_REQUIRED,
  RECONCILIATION_REQUIRED,
  I2C_NACK,
  I2C_SHORT_TRANSFER
};

struct Status {
  Err code = Err::OK;
  int32_t detail = 0;
  const char* msg = "";

  constexpr Status() = default;
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}

  constexpr bool ok() const { return code == Err::OK; }
  constexpr bool is(Err expected) const { return code == expected; }
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }
  explicit constexpr operator bool() const { return ok(); }

  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

} // namespace SCD41
