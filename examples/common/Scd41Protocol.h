/**
 * @file Scd41Protocol.h
 * @brief SCD41 command helpers for the example CLI.
 */

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "common/DriverCompat.h"

namespace scd41 {

static constexpr uint8_t I2C_ADDRESS = 0x62;

enum class Mode : uint8_t {
  IDLE = 0,
  PERIODIC_5S,
  LOW_POWER_30S,
  SINGLE_SHOT,
  SINGLE_SHOT_RHT_ONLY,
  SLEEP
};

struct DataReadyStatus {
  uint16_t raw = 0;
  bool ready = false;
};

struct MeasurementFrame {
  uint16_t co2Ppm = 0;
  uint16_t rawTemperature = 0;
  uint16_t rawHumidity = 0;
  int32_t temperatureMilliC = 0;
  int32_t humidityMilliPct = 0;
  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
  bool co2Valid = true;
  uint32_t timestampMs = 0;
};

struct SerialNumber {
  uint16_t word0 = 0;
  uint16_t word1 = 0;
  uint16_t word2 = 0;
  uint64_t value = 0;
  uint8_t variantId = 0xFF;
};

namespace cmd {
static constexpr uint16_t START_PERIODIC_MEASUREMENT = 0x21B1;
static constexpr uint16_t READ_MEASUREMENT = 0xEC05;
static constexpr uint16_t STOP_PERIODIC_MEASUREMENT = 0x3F86;
static constexpr uint16_t SET_TEMPERATURE_OFFSET = 0x241D;
static constexpr uint16_t GET_TEMPERATURE_OFFSET = 0x2318;
static constexpr uint16_t SET_SENSOR_ALTITUDE = 0x2427;
static constexpr uint16_t GET_SENSOR_ALTITUDE = 0x2322;
static constexpr uint16_t SET_AMBIENT_PRESSURE = 0xE000;
static constexpr uint16_t GET_AMBIENT_PRESSURE = 0xE000;
static constexpr uint16_t PERFORM_FORCED_RECALIBRATION = 0x362F;
static constexpr uint16_t SET_ASC_ENABLED = 0x2416;
static constexpr uint16_t GET_ASC_ENABLED = 0x2313;
static constexpr uint16_t SET_ASC_TARGET = 0x243A;
static constexpr uint16_t GET_ASC_TARGET = 0x233B;
static constexpr uint16_t START_LOW_POWER_PERIODIC = 0x21AC;
static constexpr uint16_t GET_DATA_READY_STATUS = 0xE4B8;
static constexpr uint16_t PERSIST_SETTINGS = 0x3615;
static constexpr uint16_t GET_SERIAL_NUMBER = 0x3682;
static constexpr uint16_t PERFORM_SELF_TEST = 0x3639;
static constexpr uint16_t PERFORM_FACTORY_RESET = 0x3632;
static constexpr uint16_t REINIT = 0x3646;
static constexpr uint16_t SET_ASC_INITIAL_PERIOD = 0x2445;
static constexpr uint16_t GET_ASC_INITIAL_PERIOD = 0x2340;
static constexpr uint16_t SET_ASC_STANDARD_PERIOD = 0x244E;
static constexpr uint16_t GET_ASC_STANDARD_PERIOD = 0x234B;
static constexpr uint16_t MEASURE_SINGLE_SHOT = 0x219D;
static constexpr uint16_t MEASURE_SINGLE_SHOT_RHT_ONLY = 0x2196;
static constexpr uint16_t POWER_DOWN = 0x36E0;
static constexpr uint16_t WAKE_UP = 0x36F6;
}  // namespace cmd

namespace timing {
static constexpr uint32_t FAST_MS = 1;
static constexpr uint32_t WAKE_MS = 30;
static constexpr uint32_t SINGLE_SHOT_RHT_MS = 50;
static constexpr uint32_t FORCED_RECAL_MS = 400;
static constexpr uint32_t STOP_PERIODIC_MS = 500;
static constexpr uint32_t PERSIST_MS = 800;
static constexpr uint32_t FACTORY_RESET_MS = 1200;
static constexpr uint32_t SINGLE_SHOT_MS = 5000;
static constexpr uint32_t SELF_TEST_MS = 10000;
static constexpr uint32_t PERIODIC_INTERVAL_MS = 5000;
static constexpr uint32_t LOW_POWER_INTERVAL_MS = 30000;
}  // namespace timing

inline const char* modeToString(Mode mode) {
  switch (mode) {
    case Mode::IDLE: return "IDLE";
    case Mode::PERIODIC_5S: return "PERIODIC_5S";
    case Mode::LOW_POWER_30S: return "LOW_POWER_30S";
    case Mode::SINGLE_SHOT: return "SINGLE_SHOT";
    case Mode::SINGLE_SHOT_RHT_ONLY: return "SINGLE_SHOT_RHT_ONLY";
    case Mode::SLEEP: return "SLEEP";
    default: return "UNKNOWN";
  }
}

inline bool isPeriodicMode(Mode mode) {
  return mode == Mode::PERIODIC_5S || mode == Mode::LOW_POWER_30S;
}

inline uint32_t measurementIntervalMs(Mode mode) {
  switch (mode) {
    case Mode::PERIODIC_5S: return timing::PERIODIC_INTERVAL_MS;
    case Mode::LOW_POWER_30S: return timing::LOW_POWER_INTERVAL_MS;
    case Mode::SINGLE_SHOT: return timing::SINGLE_SHOT_MS;
    case Mode::SINGLE_SHOT_RHT_ONLY: return timing::SINGLE_SHOT_RHT_MS;
    default: return 0;
  }
}

inline const char* variantName(uint8_t variantId) {
  switch (variantId) {
    case 0x0: return "SCD40";
    case 0x1: return "SCD41";
    case 0x2: return "SCD42";
    case 0x5: return "SCD43";
    default: return "Unknown";
  }
}

inline bool timeReached(uint32_t nowMs, uint32_t targetMs) {
  return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

inline void waitMs(uint32_t delayMs) {
  if (delayMs == 0U) {
    return;
  }

  const uint32_t start = millis();
  while (!timeReached(millis(), start + delayMs)) {
    delay(1);
    yield();
  }
}

inline uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                          : static_cast<uint8_t>(crc << 1U);
    }
  }
  return crc;
}

inline uint16_t unpackWord(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
}

inline void packWord(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[1] = static_cast<uint8_t>(value & 0xFFU);
}

inline app_driver::Status validateConfig(const app_driver::Config& cfg) {
  if (cfg.i2cWrite == nullptr || cfg.i2cWriteRead == nullptr) {
    return app_driver::Status::Error(app_driver::Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  return app_driver::Status::Ok();
}

inline app_driver::Status writeCommand(const app_driver::Config& cfg, uint16_t command) {
  app_driver::Status st = validateConfig(cfg);
  if (!st.ok()) {
    return st;
  }

  uint8_t tx[2] = {};
  packWord(tx, command);
  return cfg.i2cWrite(cfg.i2cAddress, tx, sizeof(tx), cfg.i2cTimeoutMs, cfg.i2cUser);
}

inline app_driver::Status writeCommandWithWord(const app_driver::Config& cfg,
                                               uint16_t command,
                                               uint16_t word) {
  app_driver::Status st = validateConfig(cfg);
  if (!st.ok()) {
    return st;
  }

  uint8_t tx[5] = {};
  packWord(&tx[0], command);
  packWord(&tx[2], word);
  tx[4] = crc8(&tx[2], 2);
  return cfg.i2cWrite(cfg.i2cAddress, tx, sizeof(tx), cfg.i2cTimeoutMs, cfg.i2cUser);
}

inline app_driver::Status readBytes(const app_driver::Config& cfg,
                                    uint8_t* rxData,
                                    size_t rxLen) {
  app_driver::Status st = validateConfig(cfg);
  if (!st.ok()) {
    return st;
  }

  return cfg.i2cWriteRead(cfg.i2cAddress, nullptr, 0, rxData, rxLen, cfg.i2cTimeoutMs, cfg.i2cUser);
}

inline app_driver::Status commandReadRaw(const app_driver::Config& cfg,
                                         uint16_t command,
                                         uint32_t execTimeMs,
                                         uint8_t* rxData,
                                         size_t rxLen,
                                         bool allowNoData = false) {
  app_driver::Status st = writeCommand(cfg, command);
  if (!st.ok()) {
    return st;
  }

  waitMs(execTimeMs);

  st = readBytes(cfg, rxData, rxLen);
  if (allowNoData && st.code == app_driver::Err::I2C_NACK_READ) {
    return app_driver::Status::Error(app_driver::Err::MEASUREMENT_NOT_READY, "Measurement not ready");
  }
  return st;
}

inline app_driver::Status readWordCommand(const app_driver::Config& cfg,
                                          uint16_t command,
                                          uint32_t execTimeMs,
                                          uint16_t& outWord) {
  uint8_t rx[3] = {};
  app_driver::Status st = commandReadRaw(cfg, command, execTimeMs, rx, sizeof(rx));
  if (!st.ok()) {
    return st;
  }
  if (crc8(&rx[0], 2) != rx[2]) {
    return app_driver::Status::Error(app_driver::Err::CRC_MISMATCH, "CRC mismatch");
  }

  outWord = unpackWord(&rx[0]);
  return app_driver::Status::Ok();
}

inline app_driver::Status readMeasurement(const app_driver::Config& cfg,
                                          MeasurementFrame& outFrame,
                                          bool co2Valid = true) {
  uint8_t rx[9] = {};
  app_driver::Status st =
      commandReadRaw(cfg, cmd::READ_MEASUREMENT, timing::FAST_MS, rx, sizeof(rx), true);
  if (!st.ok()) {
    return st;
  }

  if (crc8(&rx[0], 2) != rx[2] || crc8(&rx[3], 2) != rx[5] || crc8(&rx[6], 2) != rx[8]) {
    return app_driver::Status::Error(app_driver::Err::CRC_MISMATCH, "Measurement CRC mismatch");
  }

  outFrame.co2Ppm = unpackWord(&rx[0]);
  outFrame.rawTemperature = unpackWord(&rx[3]);
  outFrame.rawHumidity = unpackWord(&rx[6]);
  outFrame.temperatureMilliC =
      static_cast<int32_t>((21875L * static_cast<int32_t>(outFrame.rawTemperature)) >> 13U) - 45000L;
  outFrame.humidityMilliPct =
      static_cast<int32_t>((12500L * static_cast<int32_t>(outFrame.rawHumidity)) >> 13U);
  outFrame.temperatureC = static_cast<float>(outFrame.temperatureMilliC) / 1000.0f;
  outFrame.humidityPct = static_cast<float>(outFrame.humidityMilliPct) / 1000.0f;
  outFrame.co2Valid = co2Valid;
  outFrame.timestampMs = millis();
  return app_driver::Status::Ok();
}

inline app_driver::Status getDataReadyStatus(const app_driver::Config& cfg,
                                             DataReadyStatus& outStatus) {
  uint16_t word = 0;
  app_driver::Status st = readWordCommand(cfg, cmd::GET_DATA_READY_STATUS, timing::FAST_MS, word);
  if (!st.ok()) {
    return st;
  }
  outStatus.raw = word;
  outStatus.ready = (word & 0x07FFU) != 0U;
  return app_driver::Status::Ok();
}

inline app_driver::Status getTemperatureOffsetC(const app_driver::Config& cfg, float& outDegC) {
  uint16_t word = 0;
  app_driver::Status st = readWordCommand(cfg, cmd::GET_TEMPERATURE_OFFSET, timing::FAST_MS, word);
  if (!st.ok()) {
    return st;
  }
  outDegC = 175.0f * static_cast<float>(word) / 65535.0f;
  return app_driver::Status::Ok();
}

inline app_driver::Status setTemperatureOffsetC(const app_driver::Config& cfg, float offsetDegC) {
  if (offsetDegC < 0.0f || offsetDegC > 20.0f) {
    return app_driver::Status::Error(app_driver::Err::INVALID_PARAM, "Offset out of range");
  }

  const uint16_t word = static_cast<uint16_t>((offsetDegC * 65536.0f / 175.0f) + 0.5f);
  app_driver::Status st = writeCommandWithWord(cfg, cmd::SET_TEMPERATURE_OFFSET, word);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status getSensorAltitudeM(const app_driver::Config& cfg, uint16_t& outMeters) {
  return readWordCommand(cfg, cmd::GET_SENSOR_ALTITUDE, timing::FAST_MS, outMeters);
}

inline app_driver::Status setSensorAltitudeM(const app_driver::Config& cfg, uint16_t meters) {
  if (meters > 3000U) {
    return app_driver::Status::Error(app_driver::Err::INVALID_PARAM, "Altitude out of range");
  }

  app_driver::Status st = writeCommandWithWord(cfg, cmd::SET_SENSOR_ALTITUDE, meters);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status getAmbientPressurePa(const app_driver::Config& cfg, uint32_t& outPa) {
  uint16_t word = 0;
  app_driver::Status st = readWordCommand(cfg, cmd::GET_AMBIENT_PRESSURE, timing::FAST_MS, word);
  if (!st.ok()) {
    return st;
  }
  outPa = static_cast<uint32_t>(word) * 100U;
  return app_driver::Status::Ok();
}

inline app_driver::Status setAmbientPressurePa(const app_driver::Config& cfg, uint32_t pressurePa) {
  if (pressurePa < 70000U || pressurePa > 120000U) {
    return app_driver::Status::Error(app_driver::Err::INVALID_PARAM, "Pressure out of range");
  }

  const uint16_t word = static_cast<uint16_t>((pressurePa + 50U) / 100U);
  app_driver::Status st = writeCommandWithWord(cfg, cmd::SET_AMBIENT_PRESSURE, word);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status getAscEnabled(const app_driver::Config& cfg, bool& outEnabled) {
  uint16_t word = 0;
  app_driver::Status st = readWordCommand(cfg, cmd::GET_ASC_ENABLED, timing::FAST_MS, word);
  if (!st.ok()) {
    return st;
  }
  outEnabled = (word != 0U);
  return app_driver::Status::Ok();
}

inline app_driver::Status setAscEnabled(const app_driver::Config& cfg, bool enabled) {
  app_driver::Status st = writeCommandWithWord(cfg, cmd::SET_ASC_ENABLED, enabled ? 1U : 0U);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status getAscTargetPpm(const app_driver::Config& cfg, uint16_t& outPpm) {
  return readWordCommand(cfg, cmd::GET_ASC_TARGET, timing::FAST_MS, outPpm);
}

inline app_driver::Status setAscTargetPpm(const app_driver::Config& cfg, uint16_t ppm) {
  if (ppm < 400U || ppm > 5000U) {
    return app_driver::Status::Error(app_driver::Err::INVALID_PARAM, "ASC target out of range");
  }

  app_driver::Status st = writeCommandWithWord(cfg, cmd::SET_ASC_TARGET, ppm);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status getAscInitialPeriodHours(const app_driver::Config& cfg, uint16_t& outHours) {
  return readWordCommand(cfg, cmd::GET_ASC_INITIAL_PERIOD, timing::FAST_MS, outHours);
}

inline app_driver::Status setAscInitialPeriodHours(const app_driver::Config& cfg, uint16_t hours) {
  if ((hours % 4U) != 0U && hours != 0U) {
    return app_driver::Status::Error(app_driver::Err::INVALID_PARAM,
                                     "ASC initial period must be 0 or multiple of 4 h");
  }

  app_driver::Status st = writeCommandWithWord(cfg, cmd::SET_ASC_INITIAL_PERIOD, hours);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status getAscStandardPeriodHours(const app_driver::Config& cfg, uint16_t& outHours) {
  return readWordCommand(cfg, cmd::GET_ASC_STANDARD_PERIOD, timing::FAST_MS, outHours);
}

inline app_driver::Status setAscStandardPeriodHours(const app_driver::Config& cfg, uint16_t hours) {
  if ((hours % 4U) != 0U) {
    return app_driver::Status::Error(app_driver::Err::INVALID_PARAM,
                                     "ASC standard period must be multiple of 4 h");
  }

  app_driver::Status st = writeCommandWithWord(cfg, cmd::SET_ASC_STANDARD_PERIOD, hours);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status getSerialNumber(const app_driver::Config& cfg, SerialNumber& outSerial) {
  uint8_t rx[9] = {};
  app_driver::Status st = commandReadRaw(cfg, cmd::GET_SERIAL_NUMBER, timing::FAST_MS, rx, sizeof(rx));
  if (!st.ok()) {
    return st;
  }
  if (crc8(&rx[0], 2) != rx[2] || crc8(&rx[3], 2) != rx[5] || crc8(&rx[6], 2) != rx[8]) {
    return app_driver::Status::Error(app_driver::Err::CRC_MISMATCH, "Serial CRC mismatch");
  }

  outSerial.word0 = unpackWord(&rx[0]);
  outSerial.word1 = unpackWord(&rx[3]);
  outSerial.word2 = unpackWord(&rx[6]);
  outSerial.value = (static_cast<uint64_t>(outSerial.word0) << 32U) |
                    (static_cast<uint64_t>(outSerial.word1) << 16U) |
                    static_cast<uint64_t>(outSerial.word2);
  outSerial.variantId = static_cast<uint8_t>((outSerial.word0 >> 12U) & 0x0FU);
  return app_driver::Status::Ok();
}

inline app_driver::Status startPeriodicMeasurement(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::START_PERIODIC_MEASUREMENT);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status startLowPowerPeriodicMeasurement(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::START_LOW_POWER_PERIODIC);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status stopPeriodicMeasurement(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::STOP_PERIODIC_MEASUREMENT);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::STOP_PERIODIC_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status triggerSingleShot(const app_driver::Config& cfg) {
  return writeCommand(cfg, cmd::MEASURE_SINGLE_SHOT);
}

inline app_driver::Status triggerSingleShotRhtOnly(const app_driver::Config& cfg) {
  return writeCommand(cfg, cmd::MEASURE_SINGLE_SHOT_RHT_ONLY);
}

inline app_driver::Status persistSettings(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::PERSIST_SETTINGS);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::PERSIST_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status reinit(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::REINIT);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::WAKE_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status performFactoryReset(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::PERFORM_FACTORY_RESET);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FACTORY_RESET_MS);
  return app_driver::Status::Ok();
}

inline app_driver::Status performSelfTest(const app_driver::Config& cfg, uint16_t& outResult) {
  return readWordCommand(cfg, cmd::PERFORM_SELF_TEST, timing::SELF_TEST_MS, outResult);
}

inline app_driver::Status performForcedRecalibration(const app_driver::Config& cfg,
                                                     uint16_t referencePpm,
                                                     int16_t& outCorrectionPpm) {
  if (referencePpm < 400U || referencePpm > 2000U) {
    return app_driver::Status::Error(app_driver::Err::INVALID_PARAM,
                                     "FRC reference must be 400..2000 ppm");
  }

  app_driver::Status st =
      writeCommandWithWord(cfg, cmd::PERFORM_FORCED_RECALIBRATION, referencePpm);
  if (!st.ok()) {
    return st;
  }

  waitMs(timing::FORCED_RECAL_MS);

  uint8_t rx[3] = {};
  st = readBytes(cfg, rx, sizeof(rx));
  if (!st.ok()) {
    return st;
  }
  if (crc8(&rx[0], 2) != rx[2]) {
    return app_driver::Status::Error(app_driver::Err::CRC_MISMATCH, "FRC CRC mismatch");
  }

  const uint16_t result = unpackWord(&rx[0]);
  if (result == 0xFFFFU) {
    return app_driver::Status::Error(app_driver::Err::COMMAND_FAILED, "Forced recalibration failed");
  }

  outCorrectionPpm = static_cast<int16_t>(static_cast<int32_t>(result) - 0x8000L);
  return app_driver::Status::Ok();
}

inline app_driver::Status powerDown(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::POWER_DOWN);
  if (!st.ok()) {
    return st;
  }
  waitMs(timing::FAST_MS);
  return app_driver::Status::Ok();
}

inline bool isExpectedWakeNack(const app_driver::Status& st) {
  return st.code == app_driver::Err::I2C_NACK_ADDR ||
         st.code == app_driver::Err::I2C_NACK_DATA ||
         st.code == app_driver::Err::I2C_NACK_READ ||
         st.code == app_driver::Err::I2C_ERROR;
}

inline app_driver::Status wakeUp(const app_driver::Config& cfg) {
  app_driver::Status st = writeCommand(cfg, cmd::WAKE_UP);
  if (!st.ok() && !isExpectedWakeNack(st)) {
    return st;
  }
  waitMs(timing::WAKE_MS);
  return app_driver::Status::Ok();
}

}  // namespace scd41
