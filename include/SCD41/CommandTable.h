/// @file CommandTable.h
/// @brief Command definitions and protocol constants for SCD41
#pragma once

#include <cstddef>
#include <cstdint>

namespace SCD41 {
namespace cmd {

// ============================================================================
// I2C / protocol
// ============================================================================

static constexpr uint8_t I2C_ADDRESS = 0x62;
static constexpr uint8_t CRC_INIT = 0xFF;
static constexpr uint8_t CRC_POLY = 0x31;

static constexpr size_t DATA_WORD_BYTES = 2;
static constexpr size_t DATA_CRC_BYTES = 1;
static constexpr size_t DATA_WORD_WITH_CRC = 3;

static constexpr size_t WORD_RESPONSE_LEN = 3;
static constexpr size_t SERIAL_RESPONSE_LEN = 9;
static constexpr size_t MEASUREMENT_RESPONSE_LEN = 9;

// ============================================================================
// Command execution times
// ============================================================================

static constexpr uint16_t EXECUTION_TIME_SHORT_MS = 1;
static constexpr uint16_t EXECUTION_TIME_POWER_UP_MS = 30;
static constexpr uint16_t EXECUTION_TIME_REINIT_MS = 30;
static constexpr uint16_t EXECUTION_TIME_SINGLE_SHOT_RHT_MS = 50;
static constexpr uint16_t EXECUTION_TIME_FRC_MS = 400;
static constexpr uint16_t EXECUTION_TIME_STOP_PERIODIC_MS = 500;
static constexpr uint16_t EXECUTION_TIME_SINGLE_SHOT_MS = 5000;
static constexpr uint16_t EXECUTION_TIME_PERSIST_MS = 800;
static constexpr uint16_t EXECUTION_TIME_FACTORY_RESET_MS = 1200;
static constexpr uint16_t EXECUTION_TIME_SELF_TEST_MS = 10000;

static constexpr uint32_t PERIODIC_INTERVAL_MS = 5000;
static constexpr uint32_t LOW_POWER_PERIODIC_INTERVAL_MS = 30000;

// ============================================================================
// Commands
// ============================================================================

static constexpr uint16_t CMD_START_PERIODIC_MEASUREMENT = 0x21B1;
static constexpr uint16_t CMD_READ_MEASUREMENT = 0xEC05;
static constexpr uint16_t CMD_STOP_PERIODIC_MEASUREMENT = 0x3F86;
static constexpr uint16_t CMD_SET_TEMPERATURE_OFFSET = 0x241D;
static constexpr uint16_t CMD_GET_TEMPERATURE_OFFSET = 0x2318;
static constexpr uint16_t CMD_SET_SENSOR_ALTITUDE = 0x2427;
static constexpr uint16_t CMD_GET_SENSOR_ALTITUDE = 0x2322;
static constexpr uint16_t CMD_SET_AMBIENT_PRESSURE = 0xE000;
static constexpr uint16_t CMD_GET_AMBIENT_PRESSURE = 0xE000;
static constexpr uint16_t CMD_PERFORM_FORCED_RECALIBRATION = 0x362F;
static constexpr uint16_t CMD_SET_ASC_ENABLED = 0x2416;
static constexpr uint16_t CMD_GET_ASC_ENABLED = 0x2313;
static constexpr uint16_t CMD_SET_ASC_TARGET = 0x243A;
static constexpr uint16_t CMD_GET_ASC_TARGET = 0x233B;
static constexpr uint16_t CMD_START_LOW_POWER_PERIODIC_MEASUREMENT = 0x21AC;
static constexpr uint16_t CMD_GET_DATA_READY_STATUS = 0xE4B8;
static constexpr uint16_t CMD_PERSIST_SETTINGS = 0x3615;
static constexpr uint16_t CMD_GET_SERIAL_NUMBER = 0x3682;
static constexpr uint16_t CMD_PERFORM_SELF_TEST = 0x3639;
static constexpr uint16_t CMD_PERFORM_FACTORY_RESET = 0x3632;
static constexpr uint16_t CMD_REINIT = 0x3646;
static constexpr uint16_t CMD_SET_ASC_INITIAL_PERIOD = 0x2445;
static constexpr uint16_t CMD_GET_ASC_INITIAL_PERIOD = 0x2340;
static constexpr uint16_t CMD_SET_ASC_STANDARD_PERIOD = 0x244E;
static constexpr uint16_t CMD_GET_ASC_STANDARD_PERIOD = 0x234B;
static constexpr uint16_t CMD_MEASURE_SINGLE_SHOT = 0x219D;
static constexpr uint16_t CMD_MEASURE_SINGLE_SHOT_RHT_ONLY = 0x2196;
static constexpr uint16_t CMD_POWER_DOWN = 0x36E0;
static constexpr uint16_t CMD_WAKE_UP = 0x36F6;

// ============================================================================
// Response encoding
// ============================================================================

static constexpr uint16_t DATA_READY_MASK = 0x07FF;
static constexpr uint16_t FRC_FAILED = 0xFFFF;
static constexpr uint16_t FRC_OFFSET_BIAS = 0x8000;
static constexpr uint16_t SELF_TEST_PASS = 0x0000;

static constexpr uint16_t SERIAL_VARIANT_MASK = 0xF000;
static constexpr uint16_t SERIAL_VARIANT_SHIFT = 12;
static constexpr uint8_t SERIAL_VARIANT_SCD40 = 0x0;
static constexpr uint8_t SERIAL_VARIANT_SCD41 = 0x1;
static constexpr uint8_t SERIAL_VARIANT_SCD42 = 0x2;
static constexpr uint8_t SERIAL_VARIANT_SCD43 = 0x5;

// ============================================================================
// Value ranges / defaults
// ============================================================================

static constexpr uint16_t ALTITUDE_MIN_M = 0;
static constexpr uint16_t ALTITUDE_MAX_M = 3000;
static constexpr uint32_t AMBIENT_PRESSURE_MIN_PA = 70000;
static constexpr uint32_t AMBIENT_PRESSURE_MAX_PA = 120000;
static constexpr uint16_t AMBIENT_PRESSURE_MIN_WORD = 700;
static constexpr uint16_t AMBIENT_PRESSURE_MAX_WORD = 1200;
static constexpr uint32_t AMBIENT_PRESSURE_DEFAULT_PA = 101300;
static constexpr uint16_t ASC_TARGET_DEFAULT_PPM = 400;
static constexpr uint16_t ASC_INITIAL_PERIOD_DEFAULT_H = 44;
static constexpr uint16_t ASC_STANDARD_PERIOD_DEFAULT_H = 156;
static constexpr uint16_t ASC_PERIOD_STEP_HOURS = 4;
static constexpr uint16_t CO2_MIN_PPM = 0;
static constexpr uint16_t CO2_MAX_PPM = 40000;

} // namespace cmd
} // namespace SCD41
