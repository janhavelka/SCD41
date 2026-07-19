#include "IdfI2cTransport.h"

#include <limits>

#include <esp_err.h>
#include <esp_timer.h>

namespace {

int timeoutToIdf(uint32_t timeoutMs) {
  constexpr uint32_t MAX_TIMEOUT_MS =
      static_cast<uint32_t>(std::numeric_limits<int>::max());
  return timeoutMs > MAX_TIMEOUT_MS ? std::numeric_limits<int>::max()
                                    : static_cast<int>(timeoutMs);
}

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

SCD41::TransferResult mappedResult(esp_err_t error,
                                   size_t bytes,
                                   bool effectfulWrite) {
  SCD41::TransferCode code = SCD41::TransferCode::FAILED;
  SCD41::TransferDisposition disposition =
      SCD41::TransferDisposition::INDETERMINATE;
  switch (error) {
    case ESP_OK:
      code = SCD41::TransferCode::OK;
      disposition = SCD41::TransferDisposition::COMPLETE;
      break;
    case ESP_ERR_NOT_FOUND:
    case ESP_ERR_INVALID_RESPONSE:
      code = SCD41::TransferCode::NACK;
      disposition = effectfulWrite ? SCD41::TransferDisposition::INDETERMINATE
                                   : SCD41::TransferDisposition::NO_EFFECT;
      break;
    case ESP_ERR_TIMEOUT:
      code = SCD41::TransferCode::TIMEOUT;
      break;
    default:
      code = SCD41::TransferCode::BUS_ERROR;
      break;
  }
  return SCD41::TransferResult{code, disposition, static_cast<int32_t>(error),
                               error == ESP_OK ? bytes : 0U, nowMs()};
}

}  // namespace

SCD41::TransferResult idfI2cTransfer(
    const SCD41::TransferRequest& request,
    void* user) {
  auto* context = static_cast<IdfI2cContext*>(user);
  if (context == nullptr || context->device == nullptr ||
      request.address != context->address || request.timeoutMs == 0U ||
      (request.writeLength == 0U && request.readLength == 0U) ||
      (request.writeLength > 0U && request.writeData == nullptr) ||
      (request.readLength > 0U && request.readData == nullptr)) {
    return SCD41::TransferResult{SCD41::TransferCode::FAILED,
                                 SCD41::TransferDisposition::NOT_STARTED,
                                 ESP_ERR_INVALID_ARG, 0U, nowMs()};
  }

  const int timeout = timeoutToIdf(request.timeoutMs);
  esp_err_t error = ESP_OK;
  if (request.writeLength > 0U && request.readLength > 0U) {
    error = i2c_master_transmit_receive(
        context->device, request.writeData, request.writeLength,
        request.readData, request.readLength, timeout);
  } else if (request.writeLength > 0U) {
    error = i2c_master_transmit(context->device, request.writeData,
                                request.writeLength, timeout);
  } else if (request.readLength > 0U) {
    error = i2c_master_receive(context->device, request.readData,
                               request.readLength, timeout);
  }

  return mappedResult(error, request.writeLength + request.readLength,
                      request.writeLength > 0U);
}
