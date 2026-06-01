/// @file IdfI2cTransport.h
/// @brief ESP-IDF I2C transport adapter for the SCD41 example.
#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>

#include "SCD41/SCD41.h"

struct IdfI2cContext {
  i2c_master_dev_handle_t device = nullptr;
  uint8_t address = 0x62;
};

SCD41::Status idfI2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                          uint32_t timeoutMs, void* user);

SCD41::Status idfI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                              uint8_t* rxData, size_t rxLen,
                              uint32_t timeoutMs, void* user);
