/// @file Status.h
/// @brief Fixed, allocation-free status values for the SCD41 driver.
#pragma once

#include <cstdint>

namespace SCD41 {

/// Stable, framework-neutral error codes returned by the public API.
///
/// Existing numeric values are append-only. Transport adapters report
/// `TransferCode`; the driver maps that result to the appropriate `Err`.
enum class Err : uint8_t {
  OK = 0,                              ///< The request completed successfully.
  NOT_INITIALIZED,                     ///< `begin()` has not bound the instance.
  INVALID_CONFIG,                      ///< A `Config` field violates its contract.
  I2C_ERROR,                           ///< Legacy generic I2C failure code.
  TIMEOUT,                             ///< The operation deadline expired.
  INVALID_PARAM,                       ///< A request value or ID is invalid.
  DEVICE_NOT_FOUND,                    ///< Attach could not verify a supported device.
  CRC_MISMATCH,                        ///< A returned word failed the Sensirion CRC-8 check.
  CRC_ERROR = CRC_MISMATCH,            ///< Cross-library compatibility alias.
  MEASUREMENT_NOT_READY,               ///< The requested sample is not available yet.
  CONVERSION_NOT_READY = MEASUREMENT_NOT_READY, ///< Compatibility alias.
  BUSY,                                ///< Admission is blocked by active/result/safety state.
  IN_PROGRESS,                         ///< Work was admitted or remains active.
  COMMAND_FAILED,                      ///< The sensor reported command-level failure.
  UNSUPPORTED,                         ///< The operation is unsupported for this variant.
  I2C_NACK_ADDR,                       ///< Legacy precise address-NACK code.
  I2C_NACK_DATA,                       ///< Legacy precise data-NACK code.
  I2C_NACK_READ,                       ///< Legacy precise read-NACK code.
  I2C_TIMEOUT,                         ///< One physical transfer timed out.
  I2C_BUS,                             ///< The controller reported a bus fault.
  OFFLINE, ///< Passive diagnostic state only; it never gates transfers.

  // Append-only operation-model additions.
  RESULT_NOT_READY,                    ///< No retained terminal result is available.
  STALE_RESULT,                        ///< The supplied ID does not match retained state.
  CANCELLED,                           ///< Host-side future work was cancelled.
  PARTIAL,                             ///< Some composite fields completed before failure.
  INDETERMINATE,                       ///< Hardware effect cannot be proven.
  CONFIRMATION_REQUIRED,               ///< Maintenance confirmation is absent or wrong.
  RECONCILIATION_REQUIRED,             ///< Attach/readback is required before this work.
  I2C_NACK,                            ///< Unified transport reported a generic NACK.
  I2C_SHORT_TRANSFER                   ///< Fewer bytes completed than requested.
};

/// Return a stable allocation-free diagnostic name for an error code.
/// @param code Error code to name.
/// @return Static-lifetime enum name, or `"UNKNOWN"` for an unknown value.
constexpr const char* errorName(Err code) {
  switch (code) {
    case Err::OK: return "OK";
    case Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case Err::I2C_ERROR: return "I2C_ERROR";
    case Err::TIMEOUT: return "TIMEOUT";
    case Err::INVALID_PARAM: return "INVALID_PARAM";
    case Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case Err::CRC_MISMATCH: return "CRC_MISMATCH";
    case Err::MEASUREMENT_NOT_READY: return "MEASUREMENT_NOT_READY";
    case Err::BUSY: return "BUSY";
    case Err::IN_PROGRESS: return "IN_PROGRESS";
    case Err::COMMAND_FAILED: return "COMMAND_FAILED";
    case Err::UNSUPPORTED: return "UNSUPPORTED";
    case Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case Err::I2C_NACK_READ: return "I2C_NACK_READ";
    case Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case Err::I2C_BUS: return "I2C_BUS";
    case Err::OFFLINE: return "OFFLINE";
    case Err::RESULT_NOT_READY: return "RESULT_NOT_READY";
    case Err::STALE_RESULT: return "STALE_RESULT";
    case Err::CANCELLED: return "CANCELLED";
    case Err::PARTIAL: return "PARTIAL";
    case Err::INDETERMINATE: return "INDETERMINATE";
    case Err::CONFIRMATION_REQUIRED: return "CONFIRMATION_REQUIRED";
    case Err::RECONCILIATION_REQUIRED: return "RECONCILIATION_REQUIRED";
    case Err::I2C_NACK: return "I2C_NACK";
    case Err::I2C_SHORT_TRANSFER: return "I2C_SHORT_TRANSFER";
  }
  return "UNKNOWN";
}

/// Compatibility spelling shared by the mature sibling driver APIs.
/// @param code Error code to name.
/// @return The same static-lifetime string as `errorName(code)`.
constexpr const char* toString(Err code) { return errorName(code); }

/// Allocation-free status with a stable code, adapter/device detail, and
/// static-lifetime message.
struct Status {
  Err code = Err::OK;   ///< Machine-readable status code.
  int32_t detail = 0;   ///< Optional platform or protocol detail; zero when absent.
  const char* msg = ""; ///< Static-lifetime diagnostic text; never heap-owned.

  constexpr Status() = default;
  /// Construct a status from its three fixed fields.
  /// @param c Machine-readable status code.
  /// @param d Optional platform or protocol detail.
  /// @param m Static-lifetime diagnostic string.
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}

  /// @return `true` only when `code == Err::OK`.
  constexpr bool ok() const { return code == Err::OK; }
  /// @param expected Code to compare.
  /// @return `true` when `code` matches `expected`.
  constexpr bool is(Err expected) const { return code == expected; }
  /// @return `true` only when the operation remains active.
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }
  /// @return The same value as `ok()`.
  explicit constexpr operator bool() const { return ok(); }

  /// @return A successful status with zero detail.
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }
  /// Build an error status without allocation.
  /// @param err Machine-readable failure code.
  /// @param message Static-lifetime diagnostic string.
  /// @param detailCode Optional platform or protocol detail.
  /// @return The constructed failure status.
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

} // namespace SCD41
