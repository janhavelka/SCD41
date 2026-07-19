/// @file Config.h
/// @brief Non-owning, single-attempt I2C transport contract.
#pragma once

#include <cstddef>
#include <cstdint>

namespace SCD41 {

enum class TransferIntent : uint8_t {
  NORMAL = 0,
  EXPECTED_WRITE_NACK
};

enum class TransferCode : uint8_t {
  OK = 0,
  NACK,
  TIMEOUT,
  BUS_ERROR,
  SHORT_TRANSFER,
  FAILED
};

/// What the adapter can prove about the physical effect of this attempt.
enum class TransferDisposition : uint8_t {
  NOT_STARTED = 0, ///< No bus attempt began.
  NO_EFFECT,       ///< An attempt began, but the target accepted no effectful data.
  COMPLETE,        ///< The requested bytes were transferred completely.
  INDETERMINATE    ///< The adapter cannot prove whether the target accepted an effect.
};

struct TransferRequest {
  uint8_t address = 0x62;
  const uint8_t* writeData = nullptr;
  size_t writeLength = 0;
  uint8_t* readData = nullptr;
  size_t readLength = 0;
  uint32_t timeoutMs = 0;
  TransferIntent intent = TransferIntent::NORMAL;
};

/// Result of exactly one physical transport attempt.
/// `completedMs` is in the same monotonic 32-bit clock domain supplied to
/// operation start and poll calls.
struct TransferResult {
  TransferCode code = TransferCode::FAILED;
  TransferDisposition disposition = TransferDisposition::NOT_STARTED;
  int32_t detail = 0;
  /// Total bytes transferred for the requested write and read portions.
  size_t bytesTransferred = 0;
  /// Required completion timestamp for every callback invocation, including errors.
  uint32_t completedMs = 0;

  constexpr TransferResult() = default;
  constexpr TransferResult(TransferCode resultCode, TransferDisposition resultDisposition,
                           int32_t resultDetail, size_t transferred,
                           uint32_t completedAtMs)
      : code(resultCode), disposition(resultDisposition), detail(resultDetail),
        bytesTransferred(transferred), completedMs(completedAtMs) {}

  static constexpr TransferResult Ok(size_t bytes, uint32_t completedAtMs) {
    return TransferResult{TransferCode::OK, TransferDisposition::COMPLETE, 0,
                          bytes, completedAtMs};
  }
};

using I2cTransferFn = TransferResult (*)(const TransferRequest& request, void* user);

struct Config {
  I2cTransferFn transfer = nullptr;
  void* transferUser = nullptr;
  uint32_t transferTimeoutMs = 50;
  uint16_t powerUpDelayMs = 30;
  uint8_t offlineThreshold = 5;
  bool strictVariantCheck = true;
};

} // namespace SCD41
