/**
 * @file DriverCompat.h
 * @brief Example-side aliases for the SCD41 driver API.
 */

#pragma once

#include "SCD41/SCD41.h"

namespace app_driver {

namespace api = ::SCD41;

using Device = api::SCD41;
using Status = api::Status;
using Err = api::Err;
using Config = api::Config;
using DriverState = api::DriverState;
using TransportCapability = api::TransportCapability;
using SettingsSnapshot = api::SettingsSnapshot;
using Identity = api::Identity;
using Measurement = api::Measurement;
using RawSample = api::RawSample;
using CompensatedSample = api::CompensatedSample;
using DataReadyStatus = api::DataReadyStatus;
using OperatingMode = api::OperatingMode;
using PendingCommand = api::PendingCommand;
using SingleShotMode = api::SingleShotMode;
using SensorVariant = api::SensorVariant;

inline const char* driverName() { return "SCD41"; }
inline const char* coreHeader() { return "SCD41/SCD41.h"; }
inline const char* coreNamespace() { return "SCD41"; }
inline const char* version() { return api::VERSION; }
inline const char* versionFull() { return api::VERSION_FULL; }
inline const char* buildTimestamp() { return api::BUILD_TIMESTAMP; }
inline const char* gitCommit() { return api::GIT_COMMIT; }
inline const char* gitStatus() { return api::GIT_STATUS; }

inline const char* errToString(Err err) {
  switch (err) {
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
    default: return "UNKNOWN";
  }
}

inline const char* stateToString(DriverState state) {
  switch (state) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::READY: return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

inline const char* modeToString(OperatingMode mode) {
  switch (mode) {
    case OperatingMode::IDLE: return "IDLE";
    case OperatingMode::PERIODIC: return "PERIODIC";
    case OperatingMode::LOW_POWER_PERIODIC: return "LOW_POWER_PERIODIC";
    case OperatingMode::POWER_DOWN: return "POWER_DOWN";
    default: return "UNKNOWN";
  }
}

inline const char* pendingToString(PendingCommand cmd) {
  switch (cmd) {
    case PendingCommand::NONE: return "NONE";
    case PendingCommand::STOP_PERIODIC: return "STOP_PERIODIC";
    case PendingCommand::SINGLE_SHOT: return "SINGLE_SHOT";
    case PendingCommand::SINGLE_SHOT_RHT_ONLY: return "SINGLE_SHOT_RHT_ONLY";
    case PendingCommand::POWER_DOWN: return "POWER_DOWN";
    case PendingCommand::WAKE_UP: return "WAKE_UP";
    case PendingCommand::PERSIST_SETTINGS: return "PERSIST_SETTINGS";
    case PendingCommand::REINIT: return "REINIT";
    case PendingCommand::FACTORY_RESET: return "FACTORY_RESET";
    case PendingCommand::SELF_TEST: return "SELF_TEST";
    case PendingCommand::FORCED_RECALIBRATION: return "FORCED_RECALIBRATION";
    case PendingCommand::POWER_CYCLE: return "POWER_CYCLE";
    default: return "UNKNOWN";
  }
}

inline const char* variantToString(SensorVariant variant) {
  switch (variant) {
    case SensorVariant::SCD40: return "SCD40";
    case SensorVariant::SCD41: return "SCD41";
    case SensorVariant::SCD42: return "SCD42";
    case SensorVariant::SCD43: return "SCD43";
    case SensorVariant::UNKNOWN: return "UNKNOWN";
    default: return "UNKNOWN";
  }
}

} // namespace app_driver
