/// @file IdfI2cTransport.h
/// @brief Single-attempt ESP-IDF I2C transport for the SCD41 example.
#pragma once

#include <driver/i2c_master.h>

#include "SCD41/Config.h"

struct IdfI2cContext {
  i2c_master_dev_handle_t device = nullptr;
  uint8_t address = 0x62;
};

SCD41::TransferResult idfI2cTransfer(
    const SCD41::TransferRequest& request,
    void* user);
