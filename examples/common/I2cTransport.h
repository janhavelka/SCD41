/**
 * @file I2cTransport.h
 * @brief Single-attempt Wire transport used only by Arduino examples.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "SCD41/Config.h"

namespace transport {

inline bool initWire(int sda, int scl, uint32_t freqHz, uint32_t timeoutMs) {
  if (!Wire.begin(sda, scl)) {
    return false;
  }
  Wire.setClock(freqHz);
  Wire.setTimeOut(timeoutMs);
  return true;
}

inline SCD41::TransferResult result(SCD41::TransferCode code,
                                    SCD41::TransferDisposition disposition,
                                    int32_t detail,
                                    size_t bytes) {
  return SCD41::TransferResult{code, disposition, detail, bytes, millis()};
}

inline SCD41::TransferResult wireTransfer(
    const SCD41::TransferRequest& request,
    void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr || request.timeoutMs == 0U ||
      (request.writeLength == 0U && request.readLength == 0U) ||
      (request.writeLength > 0U && request.writeData == nullptr) ||
      (request.readLength > 0U && request.readData == nullptr)) {
    return result(SCD41::TransferCode::FAILED,
                  SCD41::TransferDisposition::NOT_STARTED, 0, 0);
  }

  wire->setTimeOut(request.timeoutMs);
  size_t transferred = 0;

  if (request.writeLength > 0U) {
    wire->beginTransmission(request.address);
    const size_t written =
        wire->write(request.writeData, request.writeLength);
    const uint8_t wireStatus =
        wire->endTransmission(request.readLength == 0U);
    transferred = written;
    if (written != request.writeLength) {
      return result(SCD41::TransferCode::SHORT_TRANSFER,
                    SCD41::TransferDisposition::INDETERMINATE,
                    static_cast<int32_t>(wireStatus), transferred);
    }
    switch (wireStatus) {
      case 0:
        break;
      case 1:
        return result(SCD41::TransferCode::SHORT_TRANSFER,
                      SCD41::TransferDisposition::INDETERMINATE, wireStatus,
                      transferred);
      case 2:
      case 3:
        return result(SCD41::TransferCode::NACK,
                      wireStatus == 2U ? SCD41::TransferDisposition::NO_EFFECT
                                       : SCD41::TransferDisposition::INDETERMINATE,
                      wireStatus, 0U);
      case 4:
        return result(SCD41::TransferCode::BUS_ERROR,
                      SCD41::TransferDisposition::INDETERMINATE, wireStatus, 0U);
      case 5:
        return result(SCD41::TransferCode::TIMEOUT,
                      SCD41::TransferDisposition::INDETERMINATE, wireStatus, 0U);
      default:
        return result(SCD41::TransferCode::FAILED,
                      SCD41::TransferDisposition::INDETERMINATE, wireStatus, 0U);
    }
  }

  if (request.readLength > 0U) {
    const size_t received =
        wire->requestFrom(request.address, request.readLength, true);
    if (received == 0U) {
      return result(SCD41::TransferCode::NACK,
                    request.writeLength == 0U
                        ? SCD41::TransferDisposition::NO_EFFECT
                        : SCD41::TransferDisposition::INDETERMINATE,
                    0, transferred);
    }
    if (received != request.readLength) {
      while (wire->available() > 0) {
        (void)wire->read();
      }
      return result(SCD41::TransferCode::SHORT_TRANSFER,
                    SCD41::TransferDisposition::INDETERMINATE,
                    static_cast<int32_t>(received), transferred + received);
    }
    for (size_t i = 0; i < request.readLength; ++i) {
      request.readData[i] = static_cast<uint8_t>(wire->read());
    }
    transferred += received;
  }

  return result(SCD41::TransferCode::OK,
                SCD41::TransferDisposition::COMPLETE, 0, transferred);
}

}  // namespace transport
