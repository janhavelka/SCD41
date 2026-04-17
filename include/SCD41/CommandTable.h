/// @file CommandTable.h
/// @brief Command definitions and protocol constants for SCD41
#pragma once

#include <cstddef>
#include <cstdint>

namespace SCD41 {
/// Low-level SCD41 protocol constants, command words, and encoded limits.
namespace cmd {

// ============================================================================
// I2C / protocol
// ============================================================================

static constexpr uint8_t I2C_ADDRESS = 0x62; ///< Fixed 7-bit SCD41 I2C address
static constexpr uint8_t CRC_INIT = 0xFF;    ///< CRC-8 initial value
static constexpr uint8_t CRC_POLY = 0x31;    ///< CRC-8 polynomial

static constexpr size_t DATA_WORD_BYTES = 2;     ///< Data payload bytes per SCD41 word
static constexpr size_t DATA_CRC_BYTES = 1;      ///< CRC bytes appended per returned word
static constexpr size_t DATA_WORD_WITH_CRC = 3;  ///< Total bytes per returned word including CRC

static constexpr size_t WORD_RESPONSE_LEN = 3;         ///< Response length for one CRC-protected word
static constexpr size_t SERIAL_RESPONSE_LEN = 9;       ///< Response length for the 48-bit serial number
static constexpr size_t MEASUREMENT_RESPONSE_LEN = 9;  ///< Response length for CO2 + temperature + humidity

// ============================================================================
// Command execution times
// ============================================================================

static constexpr uint16_t EXECUTION_TIME_SHORT_MS = 1;           ///< Short command execution time
static constexpr uint16_t EXECUTION_TIME_POWER_UP_MS = 30;       ///< Power-up and wake-up settle time
static constexpr uint16_t EXECUTION_TIME_REINIT_MS = 30;         ///< `reinit` execution time
static constexpr uint16_t EXECUTION_TIME_SINGLE_SHOT_RHT_MS = 50; ///< RHT-only single-shot execution time
static constexpr uint16_t EXECUTION_TIME_FRC_MS = 400;           ///< Forced recalibration execution time
static constexpr uint16_t EXECUTION_TIME_STOP_PERIODIC_MS = 500; ///< Required stop-periodic settle time
static constexpr uint16_t EXECUTION_TIME_SINGLE_SHOT_MS = 5000;  ///< Full single-shot execution time
static constexpr uint16_t EXECUTION_TIME_PERSIST_MS = 800;       ///< EEPROM persist execution time
static constexpr uint16_t EXECUTION_TIME_FACTORY_RESET_MS = 1200; ///< Factory-reset execution time
static constexpr uint16_t EXECUTION_TIME_SELF_TEST_MS = 10000;   ///< Self-test execution time

static constexpr uint32_t PERIODIC_INTERVAL_MS = 5000; ///< Standard periodic sample interval
static constexpr uint32_t LOW_POWER_PERIODIC_INTERVAL_MS = 30000; ///< Low-power periodic sample interval

// ============================================================================
// Commands
// ============================================================================

static constexpr uint16_t CMD_START_PERIODIC_MEASUREMENT = 0x21B1; ///< Start 5 s periodic measurement mode
static constexpr uint16_t CMD_READ_MEASUREMENT = 0xEC05; ///< Read CO2, temperature, and humidity sample words
static constexpr uint16_t CMD_STOP_PERIODIC_MEASUREMENT = 0x3F86; ///< Stop periodic measurement mode
static constexpr uint16_t CMD_SET_TEMPERATURE_OFFSET = 0x241D; ///< Write temperature-offset compensation
static constexpr uint16_t CMD_GET_TEMPERATURE_OFFSET = 0x2318; ///< Read temperature-offset compensation
static constexpr uint16_t CMD_SET_SENSOR_ALTITUDE = 0x2427; ///< Write altitude compensation in meters
static constexpr uint16_t CMD_GET_SENSOR_ALTITUDE = 0x2322; ///< Read altitude compensation in meters
static constexpr uint16_t CMD_SET_AMBIENT_PRESSURE = 0xE000; ///< Write ambient-pressure override word
static constexpr uint16_t CMD_GET_AMBIENT_PRESSURE = 0xE000; ///< Read ambient-pressure override word
static constexpr uint16_t CMD_PERFORM_FORCED_RECALIBRATION = 0x362F; ///< Start forced recalibration
static constexpr uint16_t CMD_SET_ASC_ENABLED = 0x2416; ///< Write ASC enable flag
static constexpr uint16_t CMD_GET_ASC_ENABLED = 0x2313; ///< Read ASC enable flag
static constexpr uint16_t CMD_SET_ASC_TARGET = 0x243A; ///< Write ASC target concentration
static constexpr uint16_t CMD_GET_ASC_TARGET = 0x233B; ///< Read ASC target concentration
static constexpr uint16_t CMD_START_LOW_POWER_PERIODIC_MEASUREMENT = 0x21AC; ///< Start 30 s low-power periodic mode
static constexpr uint16_t CMD_GET_DATA_READY_STATUS = 0xE4B8; ///< Read data-ready status bits
static constexpr uint16_t CMD_PERSIST_SETTINGS = 0x3615; ///< Persist supported settings to EEPROM
static constexpr uint16_t CMD_GET_SERIAL_NUMBER = 0x3682; ///< Read 48-bit serial number
static constexpr uint16_t CMD_PERFORM_SELF_TEST = 0x3639; ///< Start the 10 s self-test
static constexpr uint16_t CMD_PERFORM_FACTORY_RESET = 0x3632; ///< Restore factory defaults
static constexpr uint16_t CMD_REINIT = 0x3646; ///< Reload persisted settings into runtime state
static constexpr uint16_t CMD_SET_ASC_INITIAL_PERIOD = 0x2445; ///< Write ASC initial period
static constexpr uint16_t CMD_GET_ASC_INITIAL_PERIOD = 0x2340; ///< Read ASC initial period
static constexpr uint16_t CMD_SET_ASC_STANDARD_PERIOD = 0x244E; ///< Write ASC standard period
static constexpr uint16_t CMD_GET_ASC_STANDARD_PERIOD = 0x234B; ///< Read ASC standard period
static constexpr uint16_t CMD_MEASURE_SINGLE_SHOT = 0x219D; ///< Start full single-shot measurement
static constexpr uint16_t CMD_MEASURE_SINGLE_SHOT_RHT_ONLY = 0x2196; ///< Start RHT-only single-shot measurement
static constexpr uint16_t CMD_POWER_DOWN = 0x36E0; ///< Enter power-down mode
static constexpr uint16_t CMD_WAKE_UP = 0x36F6; ///< Wake from power-down; device NACK is expected

// ============================================================================
// Response encoding
// ============================================================================

static constexpr uint16_t DATA_READY_MASK = 0x07FF; ///< Valid bits in `get_data_ready_status`
static constexpr uint16_t FRC_FAILED = 0xFFFF; ///< Forced-recalibration failure sentinel
static constexpr uint16_t FRC_OFFSET_BIAS = 0x8000; ///< Bias used to decode FRC correction result
static constexpr uint16_t SELF_TEST_PASS = 0x0000; ///< Self-test pass code

static constexpr uint16_t SERIAL_VARIANT_MASK = 0xF000; ///< Variant bits in serial-number word 0
static constexpr uint16_t SERIAL_VARIANT_SHIFT = 12; ///< Shift for serial-number variant bits
static constexpr uint8_t SERIAL_VARIANT_SCD40 = 0x0; ///< Serial-number encoding for SCD40
static constexpr uint8_t SERIAL_VARIANT_SCD41 = 0x1; ///< Serial-number encoding for SCD41
static constexpr uint8_t SERIAL_VARIANT_SCD42 = 0x2; ///< Serial-number encoding for SCD42
static constexpr uint8_t SERIAL_VARIANT_SCD43 = 0x5; ///< Serial-number encoding for SCD43

// ============================================================================
// Value ranges / defaults
// ============================================================================

static constexpr uint16_t ALTITUDE_MIN_M = 0; ///< Minimum supported altitude compensation
static constexpr uint16_t ALTITUDE_MAX_M = 3000; ///< Maximum supported altitude compensation
static constexpr uint32_t AMBIENT_PRESSURE_MIN_PA = 70000; ///< Minimum ambient-pressure override
static constexpr uint32_t AMBIENT_PRESSURE_MAX_PA = 120000; ///< Maximum ambient-pressure override
static constexpr uint16_t AMBIENT_PRESSURE_MIN_WORD = 700; ///< Minimum encoded ambient-pressure word
static constexpr uint16_t AMBIENT_PRESSURE_MAX_WORD = 1200; ///< Maximum encoded ambient-pressure word
static constexpr uint32_t AMBIENT_PRESSURE_DEFAULT_PA = 101300; ///< Default ambient pressure in pascals
static constexpr uint16_t ASC_TARGET_DEFAULT_PPM = 400; ///< Default ASC target concentration
static constexpr uint16_t ASC_INITIAL_PERIOD_DEFAULT_H = 44; ///< Default ASC initial period
static constexpr uint16_t ASC_STANDARD_PERIOD_DEFAULT_H = 156; ///< Default ASC standard period
static constexpr uint16_t ASC_PERIOD_STEP_HOURS = 4; ///< Required ASC period step size
static constexpr uint16_t CO2_MIN_PPM = 0; ///< Minimum representable CO2 reading
static constexpr uint16_t CO2_MAX_PPM = 40000; ///< Maximum representable CO2 reading

} // namespace cmd
} // namespace SCD41
