/// @file Config.h
/// @brief Non-owning, single-attempt I2C transport contract.
#pragma once

#include <cstddef>
#include <cstdint>

namespace SCD41 {

/// Context attached to a single physical transfer attempt.
enum class TransferIntent : uint8_t {
  NORMAL = 0, ///< Ordinary transfer; all non-OK results are failures.
  /// Wake/reconciliation write for which a generic NACK is expected.
  EXPECTED_WRITE_NACK
};

/// Framework-neutral outcome of one physical controller attempt.
enum class TransferCode : uint8_t {
  OK = 0,        ///< All requested bytes completed.
  NACK,          ///< The target/controller reported a generic NACK.
  TIMEOUT,       ///< The adapter's finite transfer timeout expired.
  BUS_ERROR,     ///< Arbitration, line, controller, or other bus fault.
  SHORT_TRANSFER,///< Fewer bytes completed than requested.
  FAILED         ///< Other bounded adapter failure.
};

/// What the adapter can prove about the physical effect of this attempt.
enum class TransferDisposition : uint8_t {
  NOT_STARTED = 0, ///< No bus attempt began.
  NO_EFFECT,       ///< An attempt began, but the target accepted no effectful data.
  COMPLETE,        ///< The requested bytes were transferred completely.
  INDETERMINATE    ///< The adapter cannot prove whether the target accepted an effect.
};

/// Non-owning buffers and limits for exactly one callback invocation.
///
/// Pointers are valid only for the synchronous duration of the callback.
struct TransferRequest {
  uint8_t address = 0x62;               ///< Fixed 7-bit device address.
  const uint8_t* writeData = nullptr;   ///< Bytes to write, or null when length is zero.
  size_t writeLength = 0;               ///< Number of requested write bytes.
  uint8_t* readData = nullptr;          ///< Destination buffer, or null when length is zero.
  size_t readLength = 0;                ///< Number of requested read bytes.
  uint32_t timeoutMs = 0;               ///< Finite bound the adapter must enforce.
  TransferIntent intent = TransferIntent::NORMAL; ///< Expected protocol context.
};

/// Result of exactly one physical transport attempt.
/// `completedMs` is in the same monotonic 32-bit clock domain supplied to
/// operation start and poll calls.
struct TransferResult {
  TransferCode code = TransferCode::FAILED; ///< Framework-neutral attempt result.
  TransferDisposition disposition = TransferDisposition::NOT_STARTED; ///< Proven effect.
  int32_t detail = 0; ///< Optional platform detail copied into mapped status.
  size_t bytesTransferred = 0; ///< Total completed write plus read bytes.
  uint32_t completedMs = 0; ///< Required owner-clock completion time, including errors.

  constexpr TransferResult() = default;
  /// Construct a complete adapter result.
  /// @param resultCode Framework-neutral transfer outcome.
  /// @param resultDisposition Best evidence about the physical effect.
  /// @param resultDetail Optional controller/platform detail.
  /// @param transferred Total write plus read bytes completed.
  /// @param completedAtMs Callback completion time in the owner clock domain.
  constexpr TransferResult(TransferCode resultCode, TransferDisposition resultDisposition,
                           int32_t resultDetail, size_t transferred,
                           uint32_t completedAtMs)
      : code(resultCode), disposition(resultDisposition), detail(resultDetail),
        bytesTransferred(transferred), completedMs(completedAtMs) {}

  /// Construct an exact successful transfer result.
  /// @param bytes Total write plus read bytes completed.
  /// @param completedAtMs Callback completion time in the owner clock domain.
  /// @return A `COMPLETE` result with zero detail.
  static constexpr TransferResult Ok(size_t bytes, uint32_t completedAtMs) {
    return TransferResult{TransferCode::OK, TransferDisposition::COMPLETE, 0,
                          bytes, completedAtMs};
  }
};

/// Synchronous callback for one bounded physical I2C attempt.
/// @param request Non-owning request valid for this call only.
/// @param user Application context from `Config::transferUser`.
/// @return Exact outcome, effect evidence, byte count, and completion time.
using I2cTransferFn = TransferResult (*)(const TransferRequest& request, void* user);

/// Non-owning driver configuration copied by zero-I2C `SCD41::begin()`.
struct Config {
  /// Required callback. It must perform exactly one bounded physical attempt.
  I2cTransferFn transfer = nullptr;
  /// Non-owning callback context; it must outlive use of this binding.
  void* transferUser = nullptr;
  /// Maximum time passed to one adapter attempt, in milliseconds.
  uint32_t transferTimeoutMs = 50;
  /// Initial attach power-up wait. Values below 30 ms are rejected.
  uint16_t powerUpDelayMs = 30;
  /// Consecutive attempted transfer failures that report passive `OFFLINE`.
  /// Zero disables `OFFLINE`; failures still report `DEGRADED`.
  uint8_t offlineThreshold = 5;
  /// Reject observed family variants other than SCD41 during verification.
  bool strictVariantCheck = true;
};

} // namespace SCD41
