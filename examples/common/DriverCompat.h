/**
 * @file DriverCompat.h
 * @brief Small example aliases and stable diagnostic names.
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
using OperatingMode = api::OperatingMode;
using SensorVariant = api::SensorVariant;
using OperationId = api::OperationId;
using OperationKind = api::OperationKind;
using OperationOptions = api::OperationOptions;
using OperationOutcome = api::OperationOutcome;
using OperationRequest = api::OperationRequest;
using OperationResult = api::OperationResult;
using OperationState = api::OperationState;
using PollResult = api::PollResult;
using EffectState = api::EffectState;
using ModeEvidence = api::ModeEvidence;
using FixedSample = api::FixedSample;
using RuntimeSnapshot = api::RuntimeSnapshot;
using HealthSnapshot = api::HealthSnapshot;
using ConfigurationSnapshot = api::ConfigurationSnapshot;
using Identity = api::Identity;

inline const char* errToString(Err value) {
  return api::errorName(value);
}

inline const char* stateToString(DriverState value) {
  return api::driverStateName(value);
}

inline const char* operationStateToString(OperationState value) {
  switch (value) {
    case OperationState::IDLE: return "IDLE";
    case OperationState::ACTIVE: return "ACTIVE";
    case OperationState::RESULT_PENDING: return "RESULT_PENDING";
  }
  return "UNKNOWN";
}

inline const char* evidenceToString(ModeEvidence value) {
  switch (value) {
    case ModeEvidence::UNKNOWN: return "UNKNOWN";
    case ModeEvidence::ACKNOWLEDGED: return "ACKNOWLEDGED";
    case ModeEvidence::VERIFIED: return "VERIFIED";
  }
  return "UNKNOWN";
}

inline const char* modeToString(OperatingMode value) {
  switch (value) {
    case OperatingMode::UNKNOWN: return "UNKNOWN";
    case OperatingMode::IDLE: return "IDLE";
    case OperatingMode::PERIODIC: return "PERIODIC";
    case OperatingMode::LOW_POWER_PERIODIC: return "LOW_POWER_PERIODIC";
    case OperatingMode::POWER_DOWN: return "POWER_DOWN";
  }
  return "UNKNOWN";
}

inline const char* operationToString(OperationKind value) {
  switch (value) {
    case OperationKind::NONE: return "NONE";
    case OperationKind::ATTACH: return "ATTACH";
    case OperationKind::READ_IDENTITY: return "READ_IDENTITY";
    case OperationKind::START_PERIODIC: return "START_PERIODIC";
    case OperationKind::START_LOW_POWER_PERIODIC: return "START_LOW_POWER_PERIODIC";
    case OperationKind::STOP_PERIODIC: return "STOP_PERIODIC";
    case OperationKind::READ_DATA_READY: return "READ_DATA_READY";
    case OperationKind::FETCH_SAMPLE: return "FETCH_SAMPLE";
    case OperationKind::SINGLE_SHOT: return "SINGLE_SHOT";
    case OperationKind::SINGLE_SHOT_RHT_ONLY: return "SINGLE_SHOT_RHT_ONLY";
    case OperationKind::READ_TEMPERATURE_OFFSET: return "READ_TEMPERATURE_OFFSET";
    case OperationKind::SET_TEMPERATURE_OFFSET: return "SET_TEMPERATURE_OFFSET";
    case OperationKind::READ_SENSOR_ALTITUDE: return "READ_SENSOR_ALTITUDE";
    case OperationKind::SET_SENSOR_ALTITUDE: return "SET_SENSOR_ALTITUDE";
    case OperationKind::READ_AMBIENT_PRESSURE: return "READ_AMBIENT_PRESSURE";
    case OperationKind::SET_AMBIENT_PRESSURE: return "SET_AMBIENT_PRESSURE";
    case OperationKind::READ_ASC_ENABLED: return "READ_ASC_ENABLED";
    case OperationKind::SET_ASC_ENABLED: return "SET_ASC_ENABLED";
    case OperationKind::READ_ASC_TARGET: return "READ_ASC_TARGET";
    case OperationKind::SET_ASC_TARGET: return "SET_ASC_TARGET";
    case OperationKind::READ_ASC_INITIAL_PERIOD: return "READ_ASC_INITIAL_PERIOD";
    case OperationKind::SET_ASC_INITIAL_PERIOD: return "SET_ASC_INITIAL_PERIOD";
    case OperationKind::READ_ASC_STANDARD_PERIOD: return "READ_ASC_STANDARD_PERIOD";
    case OperationKind::SET_ASC_STANDARD_PERIOD: return "SET_ASC_STANDARD_PERIOD";
    case OperationKind::READ_CONFIGURATION: return "READ_CONFIGURATION";
    case OperationKind::POWER_DOWN: return "POWER_DOWN";
    case OperationKind::WAKE_UP: return "WAKE_UP";
    case OperationKind::REINIT: return "REINIT";
    case OperationKind::SELF_TEST: return "SELF_TEST";
    case OperationKind::FORCED_RECALIBRATION: return "FORCED_RECALIBRATION";
    case OperationKind::PERSIST_SETTINGS: return "PERSIST_SETTINGS";
    case OperationKind::FACTORY_RESET: return "FACTORY_RESET";
    case OperationKind::DIAGNOSTIC_READ_WORDS: return "DIAGNOSTIC_READ_WORDS";
    case OperationKind::DIAGNOSTIC_WRITE_COMMAND: return "DIAGNOSTIC_WRITE_COMMAND";
    case OperationKind::DIAGNOSTIC_WRITE_WORD: return "DIAGNOSTIC_WRITE_WORD";
    case OperationKind::READ_SENSOR_VARIANT: return "READ_SENSOR_VARIANT";
  }
  return "UNKNOWN";
}

inline const char* outcomeToString(OperationOutcome value) {
  switch (value) {
    case OperationOutcome::SUCCEEDED: return "SUCCEEDED";
    case OperationOutcome::NO_DATA: return "NO_DATA";
    case OperationOutcome::FAILED: return "FAILED";
    case OperationOutcome::CANCELLED: return "CANCELLED";
    case OperationOutcome::TIMED_OUT: return "TIMED_OUT";
    case OperationOutcome::PARTIAL: return "PARTIAL";
    case OperationOutcome::INDETERMINATE: return "INDETERMINATE";
  }
  return "UNKNOWN";
}

inline const char* effectToString(EffectState value) {
  switch (value) {
    case EffectState::NONE: return "NONE";
    case EffectState::NOT_ATTEMPTED: return "NOT_ATTEMPTED";
    case EffectState::ATTEMPTED: return "ATTEMPTED";
    case EffectState::ACKNOWLEDGED: return "ACKNOWLEDGED";
    case EffectState::VERIFIED: return "VERIFIED";
    case EffectState::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline const char* variantToString(SensorVariant value) {
  switch (value) {
    case SensorVariant::UNKNOWN: return "UNKNOWN";
    case SensorVariant::SCD40: return "SCD40";
    case SensorVariant::SCD41: return "SCD41";
    case SensorVariant::SCD42: return "SCD42";
    case SensorVariant::SCD43: return "SCD43";
  }
  return "UNKNOWN";
}

}  // namespace app_driver
