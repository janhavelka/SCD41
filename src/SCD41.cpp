#include "SCD41/SCD41.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace SCD41 {
namespace {

template <typename T>
void incrementSaturating(T& value) {
  if (value != std::numeric_limits<T>::max()) {
    ++value;
  }
}

Status transferStatus(const TransferResult& result) {
  switch (result.code) {
    case TransferCode::OK:
      return Status::Ok();
    case TransferCode::NACK:
      return Status::Error(Err::I2C_NACK, "I2C NACK", result.detail);
    case TransferCode::TIMEOUT:
      return Status::Error(Err::I2C_TIMEOUT, "I2C timeout", result.detail);
    case TransferCode::BUS_ERROR:
      return Status::Error(Err::I2C_BUS, "I2C bus error", result.detail);
    case TransferCode::SHORT_TRANSFER:
      return Status::Error(Err::I2C_SHORT_TRANSFER, "I2C short transfer",
                           result.detail);
    case TransferCode::FAILED:
      return Status::Error(Err::I2C_ERROR, "I2C transfer failed", result.detail);
  }
  return Status::Error(Err::I2C_ERROR, "Unknown I2C result");
}

uint64_t serialNumberFromWords(const uint16_t words[3]) {
  return (static_cast<uint64_t>(words[0]) << 32) |
         (static_cast<uint64_t>(words[1]) << 16) |
         static_cast<uint64_t>(words[2]);
}

bool isReadKind(OperationKind kind) {
  switch (kind) {
    case OperationKind::READ_IDENTITY:
    case OperationKind::READ_SENSOR_VARIANT:
    case OperationKind::READ_DATA_READY:
    case OperationKind::READ_TEMPERATURE_OFFSET:
    case OperationKind::READ_SENSOR_ALTITUDE:
    case OperationKind::READ_AMBIENT_PRESSURE:
    case OperationKind::READ_ASC_ENABLED:
    case OperationKind::READ_ASC_TARGET:
    case OperationKind::READ_ASC_INITIAL_PERIOD:
    case OperationKind::READ_ASC_STANDARD_PERIOD:
    case OperationKind::READ_CONFIGURATION:
      return true;
    default:
      return false;
  }
}

bool isManagedCommand(uint16_t command) {
  switch (command) {
    case cmd::CMD_START_PERIODIC_MEASUREMENT:
    case cmd::CMD_READ_MEASUREMENT:
    case cmd::CMD_STOP_PERIODIC_MEASUREMENT:
    case cmd::CMD_SET_TEMPERATURE_OFFSET:
    case cmd::CMD_GET_TEMPERATURE_OFFSET:
    case cmd::CMD_SET_SENSOR_ALTITUDE:
    case cmd::CMD_GET_SENSOR_ALTITUDE:
    case cmd::CMD_SET_AMBIENT_PRESSURE:
    case cmd::CMD_PERFORM_FORCED_RECALIBRATION:
    case cmd::CMD_SET_ASC_ENABLED:
    case cmd::CMD_GET_ASC_ENABLED:
    case cmd::CMD_SET_ASC_TARGET:
    case cmd::CMD_GET_ASC_TARGET:
    case cmd::CMD_START_LOW_POWER_PERIODIC_MEASUREMENT:
    case cmd::CMD_GET_DATA_READY_STATUS:
    case cmd::CMD_PERSIST_SETTINGS:
    case cmd::CMD_GET_SERIAL_NUMBER:
    case cmd::CMD_PERFORM_SELF_TEST:
    case cmd::CMD_PERFORM_FACTORY_RESET:
    case cmd::CMD_REINIT:
    case cmd::CMD_GET_SENSOR_VARIANT:
    case cmd::CMD_SET_ASC_INITIAL_PERIOD:
    case cmd::CMD_GET_ASC_INITIAL_PERIOD:
    case cmd::CMD_SET_ASC_STANDARD_PERIOD:
    case cmd::CMD_GET_ASC_STANDARD_PERIOD:
    case cmd::CMD_MEASURE_SINGLE_SHOT:
    case cmd::CMD_MEASURE_SINGLE_SHOT_RHT_ONLY:
    case cmd::CMD_POWER_DOWN:
    case cmd::CMD_WAKE_UP:
      return true;
    default:
      return false;
  }
}

uint16_t fieldAt(uint8_t index) {
  static constexpr ConfigurationField FIELDS[] = {
      ConfigurationField::TEMPERATURE_OFFSET,
      ConfigurationField::SENSOR_ALTITUDE,
      ConfigurationField::AMBIENT_PRESSURE,
      ConfigurationField::ASC_ENABLED,
      ConfigurationField::ASC_TARGET,
      ConfigurationField::ASC_INITIAL_PERIOD,
      ConfigurationField::ASC_STANDARD_PERIOD,
  };
  return index < (sizeof(FIELDS) / sizeof(FIELDS[0]))
             ? configurationFieldMask(FIELDS[index])
             : 0U;
}

OperationKind readKindAt(uint8_t index) {
  static constexpr OperationKind KINDS[] = {
      OperationKind::READ_TEMPERATURE_OFFSET,
      OperationKind::READ_SENSOR_ALTITUDE,
      OperationKind::READ_AMBIENT_PRESSURE,
      OperationKind::READ_ASC_ENABLED,
      OperationKind::READ_ASC_TARGET,
      OperationKind::READ_ASC_INITIAL_PERIOD,
      OperationKind::READ_ASC_STANDARD_PERIOD,
  };
  return index < (sizeof(KINDS) / sizeof(KINDS[0])) ? KINDS[index]
                                                    : OperationKind::NONE;
}

EffectState effectFromWakeAttempt(TransferDisposition disposition) {
  switch (disposition) {
    case TransferDisposition::COMPLETE:
      return EffectState::ACKNOWLEDGED;
    case TransferDisposition::INDETERMINATE:
      return EffectState::UNKNOWN;
    case TransferDisposition::NO_EFFECT:
    case TransferDisposition::NOT_STARTED:
      return EffectState::ATTEMPTED;
  }
  return EffectState::UNKNOWN;
}

}  // namespace

Status SCD41::begin(const Config& config) {
  const Status validation = _validateConfig(config);
  if (!validation.ok()) {
    return validation;
  }
  if (_activeValid || _terminalValid) {
    return Status::Error(Err::BUSY, "Operation or result pending");
  }

  _config = config;
  _bound = true;
  _attached = false;
  _driverState = DriverState::READY;
  _operatingMode = OperatingMode::UNKNOWN;
  _modeEvidence = ModeEvidence::UNKNOWN;
  _reconciliationRequired = true;
  _sampleSequence = 0;
  _active = {};
  _terminal = {};
  _workingValue = {};
  _identity = {};
  _configuration = {};
  _latestSample = {};
  _latestSampleValid = false;
  _health = {};
  _health.state = DriverState::READY;
  _lastTransferDisposition = TransferDisposition::NOT_STARTED;
  _lastTransferWasEffectful = false;
  return Status::Ok();
}

PollResult SCD41::poll(uint32_t nowMs, uint8_t maxCallbacks) {
  PollResult result;
  result.state = operationState();

  if (_terminalValid) {
    result.state = OperationState::RESULT_PENDING;
    result.id = _terminal.id;
    result.kind = _terminal.kind;
    result.status = _terminal.status;
    return result;
  }
  if (!_activeValid) {
    result.status = Status::Ok();
    return result;
  }

  result.id = _active.id;
  result.kind = _active.request.kind;
  if (_lastOwnerNowValid && !_timeReached(nowMs, _lastOwnerNowMs)) {
    result.state = OperationState::ACTIVE;
    result.status =
        Status::Error(Err::INVALID_PARAM, "Owner clock moved backwards");
    result.nextDueMs = _active.nextDueMs;
    return result;
  }
  _lastOwnerNowMs = nowMs;
  _lastOwnerNowValid = true;
  uint8_t callbacksRemaining = maxCallbacks;
  const uint8_t callbacksBefore = callbacksRemaining;
  uint32_t driverNowMs = nowMs;
  if (_nextSafeCommandValid &&
      _timeReached(driverNowMs, _nextSafeCommandMs)) {
    _nextSafeCommandValid = false;
  }

  if (_timeReached(driverNowMs, _active.deadlineMs)) {
    EffectState effect = _active.effect;
    if (_active.effectfulWriteAttempted && effect == EffectState::NOT_ATTEMPTED) {
      effect = EffectState::UNKNOWN;
    }
    if (_active.effectfulWriteAttempted) {
      _markReconciliationRequired();
    }
    if ((_active.request.kind == OperationKind::PERSIST_SETTINGS ||
         _active.request.kind == OperationKind::FACTORY_RESET) &&
        _active.effectfulWriteAttempted) {
      _configuration.persistenceIndeterminate = true;
    }
    const bool partialConfiguration =
        _active.request.kind == OperationKind::READ_CONFIGURATION &&
        _active.completedFieldMask != 0U;
    if (partialConfiguration) {
      _workingValue.configuration = _configuration;
    }
    _finish(partialConfiguration ? OperationOutcome::PARTIAL
                                 : OperationOutcome::TIMED_OUT,
            effect, Status::Error(Err::TIMEOUT, "Operation deadline expired"),
            driverNowMs);
  }

  uint8_t cpuTransitions = 0;
  while (_activeValid && cpuTransitions < 32U) {
    ++cpuTransitions;
    if (_nextSafeCommandValid &&
        !_timeReached(driverNowMs, _nextSafeCommandMs) &&
        !_timeReached(driverNowMs, _active.nextDueMs)) {
      break;
    }

    const OperationPhase phaseBefore = _active.phase;
    const uint32_t dueBefore = _active.nextDueMs;
    const uint8_t remainingBefore = callbacksRemaining;
    const Status stepStatus = _step(driverNowMs, callbacksRemaining);

    if (!_activeValid || _terminalValid) {
      break;
    }
    if (!stepStatus.ok() && !stepStatus.inProgress()) {
      _finishTransferFailure(stepStatus, driverNowMs);
      break;
    }
    if (callbacksRemaining == 0U) {
      break;
    }
    if (_active.phase == phaseBefore && _active.nextDueMs == dueBefore &&
        callbacksRemaining == remainingBefore) {
      break;
    }
    if (!_timeReached(driverNowMs, _active.nextDueMs)) {
      break;
    }
  }

  result.callbacksUsed = static_cast<uint8_t>(callbacksBefore - callbacksRemaining);
  if (_terminalValid) {
    result.state = OperationState::RESULT_PENDING;
    result.id = _terminal.id;
    result.kind = _terminal.kind;
    result.status = _terminal.status;
    result.nextDueMs = 0;
  } else if (_activeValid) {
    result.state = OperationState::ACTIVE;
    result.id = _active.id;
    result.kind = _active.request.kind;
    result.status = Status::Error(Err::IN_PROGRESS, "Operation active");
    result.nextDueMs = _active.nextDueMs;
  } else {
    result.state = OperationState::IDLE;
    result.status = Status::Ok();
  }
  return result;
}

Status SCD41::tick(uint32_t nowMs) {
  return poll(nowMs, 1).status;
}

Status SCD41::start(const OperationRequest& request,
                    const OperationOptions& options,
                    OperationId& assignedId) {
  const Status validation = _validateStart(request, options);
  if (!validation.ok()) {
    return validation;
  }
  if (_nextGeneration == 0U) {
    return Status::Error(Err::STALE_RESULT, "Operation generation exhausted");
  }

  const OperationId id{options.requestId, _nextGeneration};
  if (_nextGeneration == std::numeric_limits<uint32_t>::max()) {
    _nextGeneration = 0U;
  } else {
    ++_nextGeneration;
  }

  const Status beginStatus = _beginOperation(request, options, id);
  if (!beginStatus.ok()) {
    return beginStatus;
  }
  assignedId = id;
  return Status::Error(Err::IN_PROGRESS, "Operation started");
}

Status SCD41::cancel(const OperationId& id, uint32_t nowMs) {
  if (!_activeValid) {
    return _terminalValid
               ? Status::Error(Err::BUSY, "Terminal result pending")
               : Status::Error(Err::RESULT_NOT_READY, "No active operation");
  }
  if (_active.id != id) {
    return Status::Error(Err::STALE_RESULT, "Operation identity mismatch");
  }
  if (_lastOwnerNowValid && !_timeReached(nowMs, _lastOwnerNowMs)) {
    return Status::Error(Err::INVALID_PARAM, "Owner clock moved backwards");
  }
  _lastOwnerNowMs = nowMs;
  _lastOwnerNowValid = true;

  EffectState effect = _active.effect;
  if (_active.callbacksUsed == 0U) {
    effect = _isEffectful(_active.request.kind) ? EffectState::NOT_ATTEMPTED
                                                : EffectState::NONE;
  } else {
    if (!_timeReached(nowMs, _active.nextDueMs)) {
      _nextSafeCommandMs = _active.nextDueMs;
      _nextSafeCommandValid = true;
    }
    _markReconciliationRequired();
    if (_active.effectfulWriteAttempted && effect == EffectState::NOT_ATTEMPTED) {
      effect = EffectState::UNKNOWN;
    }
  }
  if ((_active.request.kind == OperationKind::PERSIST_SETTINGS ||
       _active.request.kind == OperationKind::FACTORY_RESET) &&
      _active.effectfulWriteAttempted) {
    _configuration.persistenceIndeterminate = true;
  }
  _finish(OperationOutcome::CANCELLED, effect,
          Status::Error(Err::CANCELLED, "Operation cancelled"), nowMs);
  return Status::Ok();
}

Status SCD41::takeResult(const OperationId& expectedId, OperationResult& out) {
  if (!_terminalValid) {
    return Status::Error(Err::RESULT_NOT_READY, "No terminal result");
  }
  if (_terminal.id != expectedId) {
    return Status::Error(Err::STALE_RESULT, "Result identity mismatch");
  }
  out = _terminal;
  _terminal = {};
  _terminalValid = false;
  return Status::Ok();
}

void SCD41::end() {
  if (_activeValid && !_terminalValid) {
    const uint32_t completedMs =
        _lastOwnerNowValid ? _lastOwnerNowMs : _active.startedMs;
    if (_active.callbacksUsed > 0U) {
      _markReconciliationRequired();
    }
    if ((_active.request.kind == OperationKind::PERSIST_SETTINGS ||
         _active.request.kind == OperationKind::FACTORY_RESET) &&
        _active.effectfulWriteAttempted) {
      _configuration.persistenceIndeterminate = true;
    }
    _finish(OperationOutcome::CANCELLED,
            _active.effect,
            Status::Error(Err::CANCELLED, "Driver unbound"), completedMs);
  }
  _bound = false;
  _attached = false;
  _driverState = DriverState::UNINIT;
  _operatingMode = OperatingMode::UNKNOWN;
  _modeEvidence = ModeEvidence::UNKNOWN;
  _reconciliationRequired = true;
  _config = {};
  _identity.valid = false;
  _configuration.verifiedMask = 0;
  _latestSampleValid = false;
  _health.state = DriverState::UNINIT;
}

OperationState SCD41::operationState() const {
  if (_terminalValid) {
    return OperationState::RESULT_PENDING;
  }
  return _activeValid ? OperationState::ACTIVE : OperationState::IDLE;
}

RuntimeSnapshot SCD41::runtimeSnapshot() const {
  RuntimeSnapshot snapshot;
  snapshot.bound = _bound;
  snapshot.attached = _attached;
  snapshot.driverState = _driverState;
  snapshot.operatingMode = _operatingMode;
  snapshot.modeEvidence = _modeEvidence;
  snapshot.operationState = operationState();
  if (_activeValid) {
    snapshot.operationId = _active.id;
    snapshot.operationKind = _active.request.kind;
    snapshot.nextDueMs = _active.nextDueMs;
  } else if (_terminalValid) {
    snapshot.operationId = _terminal.id;
    snapshot.operationKind = _terminal.kind;
  }
  snapshot.nextSafeCommandMs = _nextSafeCommandValid ? _nextSafeCommandMs : 0U;
  snapshot.nextSafeCommandValid = _nextSafeCommandValid;
  snapshot.sensorEpoch = _sensorEpoch;
  snapshot.reconciliationRequired = _reconciliationRequired;
  snapshot.sampleAvailable = _latestSampleValid;
  return snapshot;
}

Status SCD41::peekLatestSample(FixedSample& out) const {
  if (!_latestSampleValid) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No cached sample");
  }
  out = _latestSample;
  return Status::Ok();
}

OperationLimits SCD41::limits(OperationKind kind) {
  OperationLimits limitsValue;
  limitsValue.maxRetries = 0;
  switch (kind) {
    case OperationKind::ATTACH:
      limitsValue.maxCallbacks = 6;
      limitsValue.maxWaitMs = 1533;
      break;
    case OperationKind::READ_DATA_READY:
    case OperationKind::READ_SENSOR_VARIANT:
    case OperationKind::READ_TEMPERATURE_OFFSET:
    case OperationKind::READ_SENSOR_ALTITUDE:
    case OperationKind::READ_AMBIENT_PRESSURE:
    case OperationKind::READ_ASC_ENABLED:
    case OperationKind::READ_ASC_TARGET:
    case OperationKind::READ_ASC_INITIAL_PERIOD:
    case OperationKind::READ_ASC_STANDARD_PERIOD:
      limitsValue.maxCallbacks = 2;
      limitsValue.maxWaitMs = 1;
      limitsValue.operationClass = OperationClass::STEADY_STATE;
      break;
    case OperationKind::READ_IDENTITY:
      limitsValue.maxCallbacks = 4;
      limitsValue.maxWaitMs = 3;
      limitsValue.operationClass = OperationClass::STEADY_STATE;
      break;
    case OperationKind::START_PERIODIC:
    case OperationKind::START_LOW_POWER_PERIODIC:
    case OperationKind::POWER_DOWN:
      limitsValue.maxCallbacks = 1;
      limitsValue.maxWaitMs = 1;
      break;
    case OperationKind::STOP_PERIODIC:
      limitsValue.maxCallbacks = 1;
      limitsValue.maxWaitMs = cmd::EXECUTION_TIME_STOP_PERIODIC_MS;
      break;
    case OperationKind::FETCH_SAMPLE:
      limitsValue.maxCallbacks = 4;
      limitsValue.maxWaitMs = 3;
      limitsValue.operationClass = OperationClass::STEADY_STATE;
      break;
    case OperationKind::SINGLE_SHOT:
      limitsValue.maxCallbacks = 5;
      limitsValue.maxWaitMs = cmd::EXECUTION_TIME_SINGLE_SHOT_MS + 3U;
      break;
    case OperationKind::SINGLE_SHOT_RHT_ONLY:
      limitsValue.maxCallbacks = 5;
      limitsValue.maxWaitMs = cmd::EXECUTION_TIME_SINGLE_SHOT_RHT_MS + 3U;
      break;
    case OperationKind::SET_TEMPERATURE_OFFSET:
    case OperationKind::SET_SENSOR_ALTITUDE:
    case OperationKind::SET_AMBIENT_PRESSURE:
    case OperationKind::SET_ASC_ENABLED:
    case OperationKind::SET_ASC_TARGET:
    case OperationKind::SET_ASC_INITIAL_PERIOD:
    case OperationKind::SET_ASC_STANDARD_PERIOD:
      limitsValue.maxCallbacks = 5;
      limitsValue.maxWaitMs = 4;
      break;
    case OperationKind::READ_CONFIGURATION:
      limitsValue.maxCallbacks = 14;
      limitsValue.maxWaitMs = 13;
      break;
    case OperationKind::WAKE_UP:
    case OperationKind::REINIT:
      limitsValue.maxCallbacks = 5;
      limitsValue.maxWaitMs = 33;
      break;
    case OperationKind::SELF_TEST:
      limitsValue.maxCallbacks = 2;
      limitsValue.maxWaitMs = cmd::EXECUTION_TIME_SELF_TEST_MS;
      limitsValue.operationClass = OperationClass::MAINTENANCE;
      break;
    case OperationKind::FORCED_RECALIBRATION:
      limitsValue.maxCallbacks = 2;
      limitsValue.maxWaitMs = cmd::EXECUTION_TIME_FRC_MS;
      limitsValue.operationClass = OperationClass::MAINTENANCE;
      limitsValue.writesNonvolatile = true;
      break;
    case OperationKind::PERSIST_SETTINGS:
      limitsValue.maxCallbacks = 1;
      limitsValue.maxWaitMs = cmd::EXECUTION_TIME_PERSIST_MS;
      limitsValue.operationClass = OperationClass::MAINTENANCE;
      limitsValue.writesNonvolatile = true;
      break;
    case OperationKind::FACTORY_RESET:
      limitsValue.maxCallbacks = 5;
      limitsValue.maxWaitMs = cmd::EXECUTION_TIME_FACTORY_RESET_MS + 3U;
      limitsValue.operationClass = OperationClass::MAINTENANCE;
      limitsValue.writesNonvolatile = true;
      limitsValue.destructive = true;
      break;
    case OperationKind::DIAGNOSTIC_READ_WORDS:
      limitsValue.maxCallbacks = 2;
      limitsValue.maxWaitMs = 1;
      limitsValue.operationClass = OperationClass::DIAGNOSTIC;
      break;
    case OperationKind::DIAGNOSTIC_WRITE_COMMAND:
    case OperationKind::DIAGNOSTIC_WRITE_WORD:
      limitsValue.maxCallbacks = 1;
      limitsValue.maxWaitMs = 1;
      limitsValue.operationClass = OperationClass::DIAGNOSTIC;
      break;
    case OperationKind::NONE:
      break;
  }
  return limitsValue;
}

float SCD41::convertTemperatureC(uint16_t raw) {
  return -45.0F + (175.0F * static_cast<float>(raw) / 65535.0F);
}

float SCD41::convertHumidityPct(uint16_t raw) {
  return 100.0F * static_cast<float>(raw) / 65535.0F;
}

int32_t SCD41::convertTemperatureMilliC(uint16_t raw) {
  const uint64_t scaled = static_cast<uint64_t>(raw) * 175000ULL;
  return static_cast<int32_t>((scaled + 32767ULL) / 65535ULL) - 45000;
}

uint32_t SCD41::convertHumidityMilliPercent(uint16_t raw) {
  const uint64_t scaled = static_cast<uint64_t>(raw) * 100000ULL;
  return static_cast<uint32_t>((scaled + 32767ULL) / 65535ULL);
}

Status SCD41::encodeTemperatureOffsetC(float offsetC, uint16_t& out) {
  if (!std::isfinite(offsetC) || offsetC < 0.0F || offsetC > 20.0F) {
    return Status::Error(Err::INVALID_PARAM, "Temperature offset out of range");
  }
  const int32_t milli = static_cast<int32_t>(offsetC * 1000.0F + 0.5F);
  return encodeTemperatureOffsetMilliC(milli, out);
}

Status SCD41::encodeTemperatureOffsetMilliC(int32_t offsetMilliC,
                                             uint16_t& out) {
  if (offsetMilliC < 0 || offsetMilliC > 20000) {
    return Status::Error(Err::INVALID_PARAM, "Temperature offset out of range");
  }
  const uint64_t numerator = static_cast<uint64_t>(offsetMilliC) * 65535ULL +
                             87500ULL;
  out = static_cast<uint16_t>(numerator / 175000ULL);
  return Status::Ok();
}

float SCD41::decodeTemperatureOffsetC(uint16_t raw) {
  return static_cast<float>(decodeTemperatureOffsetMilliC(raw)) / 1000.0F;
}

int32_t SCD41::decodeTemperatureOffsetMilliC(uint16_t raw) {
  return static_cast<int32_t>((static_cast<uint64_t>(raw) * 175000ULL +
                               32767ULL) /
                              65535ULL);
}

Status SCD41::encodeAmbientPressurePa(uint32_t pressurePa, uint16_t& out) {
  if (pressurePa < cmd::AMBIENT_PRESSURE_MIN_PA ||
      pressurePa > cmd::AMBIENT_PRESSURE_MAX_PA) {
    return Status::Error(Err::INVALID_PARAM, "Ambient pressure out of range");
  }
  out = static_cast<uint16_t>(pressurePa / 100U);
  return Status::Ok();
}

uint32_t SCD41::decodeAmbientPressurePa(uint16_t raw) {
  return static_cast<uint32_t>(raw) * 100U;
}

Status SCD41::_validateConfig(const Config& config) const {
  if (config.transfer == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Transfer callback is required");
  }
  if (config.transferTimeoutMs == 0U || config.transferTimeoutMs > 1000U) {
    return Status::Error(Err::INVALID_CONFIG, "Transfer timeout out of range");
  }
  if (config.powerUpDelayMs < cmd::EXECUTION_TIME_POWER_UP_MS ||
      config.powerUpDelayMs > 1000U) {
    return Status::Error(Err::INVALID_CONFIG, "Power-up delay out of range");
  }
  return Status::Ok();
}

Status SCD41::_validateStart(const OperationRequest& request,
                             const OperationOptions& options) const {
  if (!_bound) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver is not bound");
  }
  if (_activeValid || _terminalValid) {
    return Status::Error(Err::BUSY, "Operation or result pending");
  }
  if (request.kind == OperationKind::NONE || options.requestId == 0U) {
    return Status::Error(Err::INVALID_PARAM, "Invalid operation request");
  }
  if (!_deadlineValid(options.nowMs, options.deadlineMs)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid operation deadline");
  }
  if (_lastOwnerNowValid &&
      !_timeReached(options.nowMs, _lastOwnerNowMs)) {
    return Status::Error(Err::INVALID_PARAM, "Owner clock moved backwards");
  }
  if (_nextSafeCommandValid &&
      !_timeReached(options.nowMs, _nextSafeCommandMs)) {
    return Status::Error(Err::BUSY, "Sensor safety window active");
  }
  Status status = _validateRequestValue(request);
  if (!status.ok()) {
    return status;
  }
  return _validateAdmission(request.kind);
}

Status SCD41::_validateRequestValue(const OperationRequest& request) const {
  switch (request.kind) {
    case OperationKind::SET_TEMPERATURE_OFFSET: {
      uint16_t ignored = 0;
      return encodeTemperatureOffsetMilliC(request.signedValue, ignored);
    }
    case OperationKind::SET_SENSOR_ALTITUDE:
      if (request.value > cmd::ALTITUDE_MAX_M) {
        return Status::Error(Err::INVALID_PARAM, "Altitude out of range");
      }
      break;
    case OperationKind::SET_AMBIENT_PRESSURE: {
      uint16_t ignored = 0;
      return encodeAmbientPressurePa(request.value, ignored);
    }
    case OperationKind::SET_ASC_ENABLED:
      if (request.value > 1U) {
        return Status::Error(Err::INVALID_PARAM, "ASC enable must be boolean");
      }
      break;
    case OperationKind::SET_ASC_TARGET:
      if (request.value == 0U || request.value > cmd::CO2_MAX_PPM) {
        return Status::Error(Err::INVALID_PARAM, "ASC target out of range");
      }
      break;
    case OperationKind::SET_ASC_INITIAL_PERIOD:
    case OperationKind::SET_ASC_STANDARD_PERIOD:
      if (request.value > 65532U ||
          (request.value % cmd::ASC_PERIOD_STEP_HOURS) != 0U) {
        return Status::Error(Err::INVALID_PARAM, "ASC period out of range");
      }
      break;
    case OperationKind::FORCED_RECALIBRATION:
      if (request.confirmation != MaintenanceConfirmation::FORCED_RECALIBRATION) {
        return Status::Error(Err::CONFIRMATION_REQUIRED,
                             "FRC confirmation required");
      }
      if (request.value == 0U || request.value > cmd::CO2_MAX_PPM) {
        return Status::Error(Err::INVALID_PARAM, "FRC reference out of range");
      }
      break;
    case OperationKind::PERSIST_SETTINGS:
      if (request.confirmation != MaintenanceConfirmation::PERSIST_SETTINGS) {
        return Status::Error(Err::CONFIRMATION_REQUIRED,
                             "Persistence confirmation required");
      }
      break;
    case OperationKind::FACTORY_RESET:
      if (request.confirmation != MaintenanceConfirmation::FACTORY_RESET) {
        return Status::Error(Err::CONFIRMATION_REQUIRED,
                             "Factory-reset confirmation required");
      }
      break;
    case OperationKind::DIAGNOSTIC_READ_WORDS:
      if (request.command == 0U || request.wordCount == 0U ||
          request.wordCount > 3U) {
        return Status::Error(Err::INVALID_PARAM, "Invalid diagnostic read");
      }
      if (isManagedCommand(request.command)) {
        return Status::Error(Err::UNSUPPORTED,
                             "Use typed operation for managed command");
      }
      break;
    case OperationKind::DIAGNOSTIC_WRITE_COMMAND:
    case OperationKind::DIAGNOSTIC_WRITE_WORD:
      if (request.command == 0U || isManagedCommand(request.command)) {
        return Status::Error(Err::UNSUPPORTED,
                             "Use typed operation for managed command");
      }
      if (request.kind == OperationKind::DIAGNOSTIC_WRITE_WORD &&
          request.value > std::numeric_limits<uint16_t>::max()) {
        return Status::Error(Err::INVALID_PARAM,
                             "Diagnostic word is out of range");
      }
      break;
    default:
      break;
  }
  return Status::Ok();
}

Status SCD41::_validateAdmission(OperationKind kind) const {
  if (kind == OperationKind::ATTACH) {
    return Status::Ok();
  }
  if (!_attached) {
    return _reconciliationRequired
               ? Status::Error(Err::RECONCILIATION_REQUIRED,
                               "Attach reconciliation required")
               : Status::Error(Err::NOT_INITIALIZED, "Sensor is not attached");
  }
  if (_reconciliationRequired || _operatingMode == OperatingMode::UNKNOWN) {
    return Status::Error(Err::RECONCILIATION_REQUIRED,
                         "Sensor state is unknown");
  }
  if (kind == OperationKind::PERSIST_SETTINGS &&
      !_configuration.persistenceIndeterminate &&
      (_configuration.dirtyMask &
       static_cast<uint16_t>(~_configuration.verifiedMask)) != 0U) {
    return Status::Error(Err::RECONCILIATION_REQUIRED,
                         "Dirty settings require verified readback");
  }

  if (_identity.valid && _identity.variant != SensorVariant::SCD41) {
    switch (kind) {
      case OperationKind::START_LOW_POWER_PERIODIC:
      case OperationKind::SINGLE_SHOT:
      case OperationKind::SINGLE_SHOT_RHT_ONLY:
      case OperationKind::READ_ASC_TARGET:
      case OperationKind::SET_ASC_TARGET:
      case OperationKind::READ_ASC_INITIAL_PERIOD:
      case OperationKind::SET_ASC_INITIAL_PERIOD:
      case OperationKind::READ_ASC_STANDARD_PERIOD:
      case OperationKind::SET_ASC_STANDARD_PERIOD:
      case OperationKind::POWER_DOWN:
      case OperationKind::WAKE_UP:
        return Status::Error(Err::UNSUPPORTED, "Operation requires SCD41");
      default:
        break;
    }
  }

  if (_operatingMode == OperatingMode::PERIODIC ||
      _operatingMode == OperatingMode::LOW_POWER_PERIODIC) {
    return _periodicAllowed(kind)
               ? Status::Ok()
               : Status::Error(Err::BUSY, "Operation forbidden in periodic mode");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return kind == OperationKind::WAKE_UP
               ? Status::Ok()
               : Status::Error(Err::BUSY, "Sensor is powered down");
  }
  if (kind == OperationKind::FETCH_SAMPLE) {
    return Status::Error(Err::BUSY, "Periodic measurement is not active");
  }
  return Status::Ok();
}

Status SCD41::_beginOperation(const OperationRequest& request,
                              const OperationOptions& options,
                              const OperationId& id) {
  _active = {};
  _workingValue = {};
  _active.request = request;
  _active.id = id;
  _active.startedMs = options.nowMs;
  _active.deadlineMs = options.deadlineMs;
  _active.nextDueMs = options.nowMs;
  _active.effect = _isEffectful(request.kind) ? EffectState::NOT_ATTEMPTED
                                               : EffectState::NONE;
  _lastTransferWasEffectful = false;
  _lastOwnerNowMs = options.nowMs;
  _lastOwnerNowValid = true;

  if (request.kind == OperationKind::ATTACH) {
    _active.phase = OperationPhase::WAIT_POWER_UP;
    _active.nextDueMs = options.nowMs + _config.powerUpDelayMs;
  } else if (request.kind == OperationKind::FETCH_SAMPLE) {
    _active.phase = OperationPhase::SEND_READY_COMMAND;
  } else if (request.kind == OperationKind::SINGLE_SHOT ||
             request.kind == OperationKind::SINGLE_SHOT_RHT_ONLY ||
             _isMaintenance(request.kind) || _isDiagnostic(request.kind) ||
             _isSettingWrite(request.kind) ||
             request.kind == OperationKind::START_PERIODIC ||
             request.kind == OperationKind::START_LOW_POWER_PERIODIC ||
             request.kind == OperationKind::STOP_PERIODIC ||
             request.kind == OperationKind::POWER_DOWN ||
             request.kind == OperationKind::WAKE_UP) {
    _active.phase = OperationPhase::SEND_COMMAND;
  } else if (isReadKind(request.kind)) {
    _active.phase = OperationPhase::SEND_COMMAND;
  } else {
    return Status::Error(Err::UNSUPPORTED, "Unsupported operation");
  }

  if (request.kind == OperationKind::SET_TEMPERATURE_OFFSET) {
    Status status = encodeTemperatureOffsetMilliC(request.signedValue,
                                                   _active.desiredRaw);
    if (!status.ok()) {
      return status;
    }
  } else if (request.kind == OperationKind::SET_AMBIENT_PRESSURE) {
    Status status = encodeAmbientPressurePa(request.value, _active.desiredRaw);
    if (!status.ok()) {
      return status;
    }
  } else if (_isSettingWrite(request.kind)) {
    _active.desiredRaw = static_cast<uint16_t>(request.value);
  }

  _activeValid = true;
  return Status::Ok();
}

Status SCD41::_step(uint32_t& nowMs, uint8_t& callbacksRemaining) {
  if (_timeReached(nowMs, _active.deadlineMs)) {
    return Status::Error(Err::TIMEOUT, "Operation deadline expired");
  }
  if (_nextSafeCommandValid && !_timeReached(nowMs, _nextSafeCommandMs)) {
    _active.nextDueMs = _nextSafeCommandMs;
    return Status::Error(Err::IN_PROGRESS, "Sensor busy window active");
  }

  const OperationKind kind = _active.request.kind;
  if (kind == OperationKind::ATTACH) {
    return _stepAttach(nowMs, callbacksRemaining);
  }
  if (kind == OperationKind::FETCH_SAMPLE || kind == OperationKind::SINGLE_SHOT ||
      kind == OperationKind::SINGLE_SHOT_RHT_ONLY) {
    return _stepMeasurement(nowMs, callbacksRemaining);
  }
  if (_isMaintenance(kind)) {
    return _stepMaintenance(nowMs, callbacksRemaining);
  }
  if (_isDiagnostic(kind)) {
    return _stepDiagnostic(nowMs, callbacksRemaining);
  }
  if (_isSettingWrite(kind) || kind == OperationKind::START_PERIODIC ||
      kind == OperationKind::START_LOW_POWER_PERIODIC ||
      kind == OperationKind::STOP_PERIODIC || kind == OperationKind::POWER_DOWN ||
      kind == OperationKind::WAKE_UP) {
    return _stepWriteLike(nowMs, callbacksRemaining);
  }
  return _stepReadLike(nowMs, callbacksRemaining);
}

Status SCD41::_stepAttach(uint32_t& nowMs, uint8_t& callbacksRemaining) {
  Status status;
  switch (_active.phase) {
    case OperationPhase::WAIT_POWER_UP:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for power-up");
      }
      _active.phase = OperationPhase::SEND_WAKE;
      return Status::Error(Err::IN_PROGRESS, "Attach progressing");

    case OperationPhase::SEND_WAKE:
      status = _writeCommand(cmd::CMD_WAKE_UP, TransferIntent::EXPECTED_WRITE_NACK,
                             nowMs, callbacksRemaining, true);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.effect = effectFromWakeAttempt(_lastTransferDisposition);
      // A wake command can have taken effect even though its expected NACK is
      // the only bus evidence. Do not retain pre-wake managed state while the
      // remaining reconciliation phases are fallible.
      _markReconciliationRequired();
      _active.phase = OperationPhase::WAIT_WAKE;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_POWER_UP_MS;
      _nextSafeCommandMs = _active.nextDueMs;
      _nextSafeCommandValid = true;
      return Status::Error(Err::IN_PROGRESS, "Waiting after wake");

    case OperationPhase::WAIT_WAKE:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting after wake");
      }
      _active.phase = OperationPhase::SEND_STOP;
      return Status::Error(Err::IN_PROGRESS, "Attach progressing");

    case OperationPhase::SEND_STOP:
      status = _writeCommand(cmd::CMD_STOP_PERIODIC_MEASUREMENT,
                             TransferIntent::EXPECTED_WRITE_NACK, nowMs,
                             callbacksRemaining, true);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_STOP;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_STOP_PERIODIC_MS;
      _nextSafeCommandMs = _active.nextDueMs;
      _nextSafeCommandValid = true;
      return Status::Error(Err::IN_PROGRESS, "Waiting after stop");

    case OperationPhase::WAIT_STOP:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting after stop");
      }
      _active.phase = OperationPhase::SEND_READ_COMMAND;
      return Status::Error(Err::IN_PROGRESS, "Attach progressing");

    case OperationPhase::SEND_READ_COMMAND:
      status = _writeCommand(cmd::CMD_GET_SERIAL_NUMBER, TransferIntent::NORMAL,
                             nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for identity");

    case OperationPhase::WAIT_EXECUTION:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for identity");
      }
      _active.phase = _active.fieldIndex == 0U
                          ? OperationPhase::READ_RESPONSE
                          : OperationPhase::READ_VERIFY_RESPONSE;
      return Status::Error(Err::IN_PROGRESS, "Identity response due");

    case OperationPhase::READ_RESPONSE: {
      uint16_t words[3] = {};
      status = _readWords(words, 3, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _workingValue.identity.serialNumber = serialNumberFromWords(words);
      _active.fieldIndex = 1U;
      _active.phase = OperationPhase::SEND_VERIFY_COMMAND;
      _active.nextDueMs = nowMs;
      return Status::Error(Err::IN_PROGRESS, "Variant read due");
    }

    case OperationPhase::SEND_VERIFY_COMMAND:
      status = _writeCommand(cmd::CMD_GET_SENSOR_VARIANT,
                             TransferIntent::NORMAL, nowMs,
                             callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for variant");

    case OperationPhase::READ_VERIFY_RESPONSE: {
      uint16_t variantWord = 0U;
      status = _readWords(&variantWord, 1, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      const SensorVariant variant = _variantFromVariantWord(variantWord);
      const uint64_t serialNumber = _workingValue.identity.serialNumber;
      if (_config.strictVariantCheck && variant != SensorVariant::SCD41) {
        _advanceSensorEpoch();
        _identity.serialNumber = serialNumber;
        _identity.variant = variant;
        _identity.variantWord = variantWord;
        _identity.sensorEpoch = _sensorEpoch;
        _identity.valid = true;
        _configuration = {};
        _configuration.sensorEpoch = _sensorEpoch;
        _workingValue.identity = _identity;
        _markReconciliationRequired();
        _finish(OperationOutcome::FAILED, EffectState::ACKNOWLEDGED,
                Status::Error(Err::UNSUPPORTED, "Detected variant is not SCD41"),
                nowMs);
        return Status::Ok();
      }
      const bool sameSensor = _identity.valid &&
                              _identity.serialNumber == serialNumber &&
                              _identity.variant == variant;
      const ConfigurationSnapshot previousConfiguration = _configuration;
      _advanceSensorEpoch();
      _identity.serialNumber = serialNumber;
      _identity.variant = variant;
      _identity.variantWord = variantWord;
      _identity.sensorEpoch = _sensorEpoch;
      _identity.valid = true;
      _workingValue.identity = _identity;
      _attached = true;
      _reconciliationRequired = false;
      _setMode(OperatingMode::IDLE, ModeEvidence::VERIFIED);
      _configuration = sameSensor ? previousConfiguration
                                  : ConfigurationSnapshot{};
      _configuration.verifiedMask = 0U;
      _configuration.sensorEpoch = _sensorEpoch;
      _finish(OperationOutcome::SUCCEEDED, EffectState::VERIFIED, Status::Ok(),
              nowMs);
      return Status::Ok();
    }

    default:
      return Status::Error(Err::COMMAND_FAILED, "Invalid attach phase");
  }
}

Status SCD41::_stepReadLike(uint32_t& nowMs, uint8_t& callbacksRemaining) {
  OperationKind readKind = _active.request.kind;
  if (readKind == OperationKind::READ_CONFIGURATION) {
    readKind = readKindAt(_active.fieldIndex);
  }
  Status status;
  switch (_active.phase) {
    case OperationPhase::SEND_COMMAND:
      status = _writeCommand(_readCommandFor(readKind), TransferIntent::NORMAL,
                             nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for read response");

    case OperationPhase::WAIT_EXECUTION:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for read response");
      }
      _active.phase = readKind == OperationKind::READ_IDENTITY &&
                              _active.fieldIndex == 1U
                          ? OperationPhase::READ_VERIFY_RESPONSE
                          : OperationPhase::READ_RESPONSE;
      return Status::Error(Err::IN_PROGRESS, "Read response due");

    case OperationPhase::READ_RESPONSE: {
      uint16_t words[3] = {};
      const uint8_t count = readKind == OperationKind::READ_IDENTITY ? 3U : 1U;
      status = _readWords(words, count, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) {
        if (_active.request.kind == OperationKind::READ_CONFIGURATION &&
            _active.completedFieldMask != 0U) {
          _workingValue.configuration = _configuration;
          _finish(OperationOutcome::PARTIAL, EffectState::NONE, status, nowMs);
          return Status::Ok();
        }
        return status;
      }

      if (readKind == OperationKind::READ_IDENTITY) {
        _workingValue.identity.serialNumber = serialNumberFromWords(words);
        _active.fieldIndex = 1U;
        _active.phase = OperationPhase::SEND_VERIFY_COMMAND;
        _active.nextDueMs = nowMs;
        return Status::Error(Err::IN_PROGRESS, "Variant read due");
      } else if (readKind == OperationKind::READ_SENSOR_VARIANT) {
        const SensorVariant variant = _variantFromVariantWord(words[0]);
        const bool variantChanged =
            _identity.valid && _identity.variant != variant;
        const bool strictUnsupported =
            _config.strictVariantCheck && variant != SensorVariant::SCD41;
        if (strictUnsupported || variantChanged) {
          _advanceSensorEpoch();
          _identity = {};
          _identity.variant = variant;
          _identity.variantWord = words[0];
          _identity.sensorEpoch = _sensorEpoch;
          _configuration = {};
          _configuration.sensorEpoch = _sensorEpoch;
          _workingValue.identity = _identity;
          _workingValue.value = words[0];
          _markReconciliationRequired();
          _finish(OperationOutcome::FAILED, EffectState::NONE,
                  strictUnsupported
                      ? Status::Error(Err::UNSUPPORTED,
                                      "Detected variant is not SCD41")
                      : Status::Error(Err::RECONCILIATION_REQUIRED,
                                      "Sensor variant changed; attach required"),
                  nowMs);
          return Status::Ok();
        }
        _identity.variant = variant;
        _identity.variantWord = words[0];
        _identity.sensorEpoch = _sensorEpoch;
        _workingValue.identity = _identity;
        _workingValue.value = words[0];
      } else if (readKind == OperationKind::READ_DATA_READY) {
        _workingValue.dataReady.raw = words[0];
        _workingValue.dataReady.ready = isDataReady(words[0]);
        _workingValue.boolValue = _workingValue.dataReady.ready;
      } else {
        if (readKind == OperationKind::READ_ASC_ENABLED && words[0] > 1U) {
          _recordProtocolFailure(
              Status::Error(Err::COMMAND_FAILED, "Invalid ASC enable word"),
              nowMs);
          return Status::Error(Err::COMMAND_FAILED, "Invalid ASC enable word");
        }
        _applyReadValue(readKind, words[0], nowMs);
      }

      if (_active.request.kind == OperationKind::READ_CONFIGURATION) {
        const uint16_t fieldMask = fieldAt(_active.fieldIndex);
        _active.completedFieldMask |= fieldMask;
        _configuration.verifiedMask |= fieldMask;
        if (_active.fieldIndex >= 6U) {
          _workingValue.configuration = _configuration;
          _finish(OperationOutcome::SUCCEEDED, EffectState::NONE, Status::Ok(),
                  nowMs);
          return Status::Ok();
        }
        ++_active.fieldIndex;
        _active.phase = OperationPhase::SEND_COMMAND;
        _active.nextDueMs = nowMs;
        return Status::Error(Err::IN_PROGRESS, "Reading next setting");
      }

      _finish(OperationOutcome::SUCCEEDED, EffectState::NONE, Status::Ok(), nowMs);
      return Status::Ok();
    }

    case OperationPhase::SEND_VERIFY_COMMAND:
      status = _writeCommand(cmd::CMD_GET_SENSOR_VARIANT,
                             TransferIntent::NORMAL, nowMs,
                             callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for variant");

    case OperationPhase::READ_VERIFY_RESPONSE: {
      uint16_t variantWord = 0U;
      status = _readWords(&variantWord, 1, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      const SensorVariant variant = _variantFromVariantWord(variantWord);
      const uint64_t serialNumber = _workingValue.identity.serialNumber;
      if (_config.strictVariantCheck && variant != SensorVariant::SCD41) {
        _advanceSensorEpoch();
        _identity.serialNumber = serialNumber;
        _identity.variant = variant;
        _identity.variantWord = variantWord;
        _identity.sensorEpoch = _sensorEpoch;
        _identity.valid = true;
        _configuration = {};
        _configuration.sensorEpoch = _sensorEpoch;
        _workingValue.identity = _identity;
        _markReconciliationRequired();
        _finish(OperationOutcome::FAILED, EffectState::NONE,
                Status::Error(Err::UNSUPPORTED,
                              "Detected variant is not SCD41"), nowMs);
        return Status::Ok();
      }
      if (_identity.valid &&
          (_identity.serialNumber != serialNumber ||
           _identity.variant != variant)) {
        _advanceSensorEpoch();
        _configuration = {};
        _configuration.sensorEpoch = _sensorEpoch;
        _identity.serialNumber = serialNumber;
        _identity.variant = variant;
        _identity.variantWord = variantWord;
        _identity.sensorEpoch = _sensorEpoch;
        _identity.valid = true;
        _workingValue.identity = _identity;
        _markReconciliationRequired();
        _finish(OperationOutcome::FAILED, EffectState::NONE,
                Status::Error(Err::RECONCILIATION_REQUIRED,
                              "Sensor identity changed; attach required"),
                nowMs);
        return Status::Ok();
      }
      _identity.serialNumber = serialNumber;
      _identity.variant = variant;
      _identity.variantWord = variantWord;
      _identity.sensorEpoch = _sensorEpoch;
      _identity.valid = true;
      _workingValue.identity = _identity;
      _finish(OperationOutcome::SUCCEEDED, EffectState::NONE, Status::Ok(),
              nowMs);
      return Status::Ok();
    }

    default:
      return Status::Error(Err::COMMAND_FAILED, "Invalid read phase");
  }
}

Status SCD41::_stepWriteLike(uint32_t& nowMs, uint8_t& callbacksRemaining) {
  const OperationKind kind = _active.request.kind;
  const bool setting = _isSettingWrite(kind);
  Status status;

  if (setting) {
    switch (_active.phase) {
      case OperationPhase::SEND_COMMAND:
        if (_active.fieldIndex == 0U) {
          status = _writeCommand(_readCommandFor(kind), TransferIntent::NORMAL,
                                 nowMs, callbacksRemaining);
          if (status.inProgress()) return status;
          if (!status.ok()) return status;
          _active.phase = OperationPhase::WAIT_EXECUTION;
          _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
          return Status::Error(Err::IN_PROGRESS, "Waiting for current setting");
        }
        if (_active.fieldIndex == 1U) {
          status = _writeCommandWithWord(_writeCommandFor(kind),
                                         _active.desiredRaw, nowMs,
                                         callbacksRemaining, true);
          if (status.inProgress()) return status;
          const uint16_t fieldMask =
              configurationFieldMask(_fieldFor(kind));
          if (_lastTransferDisposition == TransferDisposition::COMPLETE ||
              _lastTransferDisposition == TransferDisposition::INDETERMINATE) {
            _configuration.verifiedMask &=
                static_cast<uint16_t>(~fieldMask);
            _configuration.dirtyMask |=
                static_cast<uint16_t>(fieldMask &
                                      PERSISTABLE_CONFIGURATION_FIELDS);
          }
          if (!status.ok()) {
            return status;
          }
          _active.effect = EffectState::ACKNOWLEDGED;
          _active.fieldIndex = 2U;
          _active.phase = OperationPhase::WAIT_EXECUTION;
          _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
          _nextSafeCommandMs = _active.nextDueMs;
          _nextSafeCommandValid = true;
          return Status::Error(Err::IN_PROGRESS, "Waiting after setting write");
        }
        status = _writeCommand(_readCommandFor(kind), TransferIntent::NORMAL,
                               nowMs, callbacksRemaining);
        if (status.inProgress()) return status;
        if (!status.ok()) return status;
        _active.fieldIndex = 3U;
        _active.phase = OperationPhase::WAIT_EXECUTION;
        _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
        return Status::Error(Err::IN_PROGRESS, "Waiting for setting verify");

      case OperationPhase::WAIT_EXECUTION:
        if (!_timeReached(nowMs, _active.nextDueMs)) {
          return Status::Error(Err::IN_PROGRESS, "Waiting for setting phase");
        }
        _active.phase = _active.fieldIndex == 2U
                            ? OperationPhase::SEND_COMMAND
                            : OperationPhase::READ_RESPONSE;
        return Status::Error(Err::IN_PROGRESS, "Setting response due");

      case OperationPhase::READ_RESPONSE: {
        uint16_t word = 0;
        status = _readWords(&word, 1, nowMs, callbacksRemaining);
        if (status.inProgress()) return status;
        if (!status.ok()) return status;
        if (_active.fieldIndex == 0U) {
          _applyReadValue(kind, word, nowMs);
          if (word == _active.desiredRaw) {
            _finish(OperationOutcome::SUCCEEDED, EffectState::VERIFIED, Status::Ok(),
                    nowMs);
            return Status::Ok();
          }
          _active.fieldIndex = 1U;
          _active.phase = OperationPhase::SEND_COMMAND;
          _active.nextDueMs = nowMs;
          return Status::Error(Err::IN_PROGRESS, "Setting write required");
        }
        _applyVerifiedSetting(kind, word);
        if (word != _active.desiredRaw) {
          _finish(OperationOutcome::FAILED, EffectState::VERIFIED,
                  Status::Error(Err::COMMAND_FAILED,
                                "Setting readback mismatch", word), nowMs);
          return Status::Ok();
        }
        _finish(OperationOutcome::SUCCEEDED, EffectState::VERIFIED, Status::Ok(),
                nowMs);
        return Status::Ok();
      }

      default:
        return Status::Error(Err::COMMAND_FAILED, "Invalid setting phase");
    }
  }

  switch (_active.phase) {
    case OperationPhase::SEND_COMMAND: {
      const TransferIntent intent =
          kind == OperationKind::WAKE_UP ? TransferIntent::EXPECTED_WRITE_NACK
                                         : TransferIntent::NORMAL;
      status = _writeCommand(_writeCommandFor(kind), intent, nowMs,
                             callbacksRemaining, true);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      if (kind == OperationKind::WAKE_UP) {
        _active.effect = effectFromWakeAttempt(_lastTransferDisposition);
        // Verification below is what restores attached/idle state.
        _markReconciliationRequired();
      } else {
        _active.effect = EffectState::ACKNOWLEDGED;
      }
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.nextDueMs = nowMs + _executionWaitMs(kind);
      _nextSafeCommandMs = _active.nextDueMs;
      _nextSafeCommandValid = true;
      return Status::Error(Err::IN_PROGRESS, "Waiting for command completion");
    }

    case OperationPhase::WAIT_EXECUTION:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for command completion");
      }
      if (kind == OperationKind::WAKE_UP) {
        if (_active.fieldIndex == 0U) {
          _active.phase = OperationPhase::SEND_VERIFY_COMMAND;
        } else if (_active.fieldIndex == 1U) {
          _active.phase = OperationPhase::READ_VERIFY_RESPONSE;
        } else {
          _active.phase = OperationPhase::READ_RESPONSE;
        }
        return Status::Error(Err::IN_PROGRESS, "Wake verification due");
      }
      if (kind == OperationKind::START_PERIODIC) {
        _setMode(OperatingMode::PERIODIC, ModeEvidence::ACKNOWLEDGED);
      } else if (kind == OperationKind::START_LOW_POWER_PERIODIC) {
        _setMode(OperatingMode::LOW_POWER_PERIODIC, ModeEvidence::ACKNOWLEDGED);
      } else if (kind == OperationKind::STOP_PERIODIC) {
        _setMode(OperatingMode::IDLE, ModeEvidence::ACKNOWLEDGED);
      } else if (kind == OperationKind::POWER_DOWN) {
        _setMode(OperatingMode::POWER_DOWN, ModeEvidence::ACKNOWLEDGED);
      }
      _finish(OperationOutcome::SUCCEEDED, EffectState::ACKNOWLEDGED,
              Status::Ok(), nowMs);
      return Status::Ok();

    case OperationPhase::SEND_VERIFY_COMMAND:
      status = _writeCommand(cmd::CMD_GET_SERIAL_NUMBER,
                             TransferIntent::NORMAL, nowMs,
                             callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.fieldIndex = 1U;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for wake verification");

    case OperationPhase::READ_VERIFY_RESPONSE: {
      uint16_t words[3] = {};
      status = _readWords(words, 3, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _workingValue.identity.serialNumber = serialNumberFromWords(words);
      _active.phase = OperationPhase::SEND_READ_COMMAND;
      _active.nextDueMs = nowMs;
      return Status::Error(Err::IN_PROGRESS, "Wake variant read due");
    }

    case OperationPhase::SEND_READ_COMMAND:
      status = _writeCommand(cmd::CMD_GET_SENSOR_VARIANT,
                             TransferIntent::NORMAL, nowMs,
                             callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.fieldIndex = 2U;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for wake variant");

    case OperationPhase::READ_RESPONSE: {
      uint16_t variantWord = 0U;
      status = _readWords(&variantWord, 1, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      const SensorVariant variant = _variantFromVariantWord(variantWord);
      const uint64_t serialNumber = _workingValue.identity.serialNumber;
      if (_config.strictVariantCheck && variant != SensorVariant::SCD41) {
        _advanceSensorEpoch();
        _identity.serialNumber = serialNumber;
        _identity.variant = variant;
        _identity.variantWord = variantWord;
        _identity.sensorEpoch = _sensorEpoch;
        _identity.valid = true;
        _configuration = {};
        _configuration.sensorEpoch = _sensorEpoch;
        _workingValue.identity = _identity;
        _markReconciliationRequired();
        _finish(OperationOutcome::FAILED, EffectState::UNKNOWN,
                Status::Error(Err::UNSUPPORTED,
                              "Wake verified wrong variant"), nowMs);
        return Status::Ok();
      }
      if (_identity.valid &&
          (_identity.serialNumber != serialNumber ||
           _identity.variant != variant)) {
        _advanceSensorEpoch();
        _configuration = {};
        _configuration.sensorEpoch = _sensorEpoch;
        _identity.serialNumber = serialNumber;
        _identity.variant = variant;
        _identity.variantWord = variantWord;
        _identity.sensorEpoch = _sensorEpoch;
        _identity.valid = true;
        _workingValue.identity = _identity;
        _markReconciliationRequired();
        _finish(OperationOutcome::FAILED, EffectState::UNKNOWN,
                Status::Error(Err::RECONCILIATION_REQUIRED,
                              "Sensor changed during wake; attach required"),
                nowMs);
        return Status::Ok();
      }
      _identity.serialNumber = serialNumber;
      _identity.variant = variant;
      _identity.variantWord = variantWord;
      _identity.sensorEpoch = _sensorEpoch;
      _identity.valid = true;
      _workingValue.identity = _identity;
      _attached = true;
      _reconciliationRequired = false;
      _setMode(OperatingMode::IDLE, ModeEvidence::VERIFIED);
      _finish(OperationOutcome::SUCCEEDED, EffectState::VERIFIED, Status::Ok(),
              nowMs);
      return Status::Ok();
    }

    default:
      return Status::Error(Err::COMMAND_FAILED, "Invalid write phase");
  }
}

Status SCD41::_stepMeasurement(uint32_t& nowMs,
                               uint8_t& callbacksRemaining) {
  const OperationKind kind = _active.request.kind;
  Status status;
  switch (_active.phase) {
    case OperationPhase::SEND_COMMAND:
      status = _writeCommand(_writeCommandFor(kind), TransferIntent::NORMAL,
                             nowMs, callbacksRemaining, true);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.effect = EffectState::ACKNOWLEDGED;
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.nextDueMs = nowMs + _executionWaitMs(kind);
      _nextSafeCommandMs = _active.nextDueMs;
      _nextSafeCommandValid = true;
      return Status::Error(Err::IN_PROGRESS, "Waiting for measurement");

    case OperationPhase::WAIT_EXECUTION:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for measurement");
      }
      _active.phase = OperationPhase::SEND_READY_COMMAND;
      return Status::Error(Err::IN_PROGRESS, "Measurement readiness due");

    case OperationPhase::SEND_READY_COMMAND:
      status = _writeCommand(cmd::CMD_GET_DATA_READY_STATUS,
                             TransferIntent::NORMAL, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_WAKE;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for ready response");

    case OperationPhase::WAIT_WAKE:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for ready response");
      }
      _active.phase = OperationPhase::READ_READY_RESPONSE;
      return Status::Error(Err::IN_PROGRESS, "Ready response due");

    case OperationPhase::READ_READY_RESPONSE: {
      uint16_t readyWord = 0;
      status = _readWords(&readyWord, 1, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _workingValue.dataReady.raw = readyWord;
      _workingValue.dataReady.ready = isDataReady(readyWord);
      if (!isDataReady(readyWord)) {
        _finish(OperationOutcome::NO_DATA, _active.effect,
                Status::Error(Err::MEASUREMENT_NOT_READY,
                              "Measurement not ready"), nowMs);
        return Status::Ok();
      }
      _active.phase = OperationPhase::SEND_READ_COMMAND;
      return Status::Error(Err::IN_PROGRESS, "Sample read due");
    }

    case OperationPhase::SEND_READ_COMMAND:
      status = _writeCommand(cmd::CMD_READ_MEASUREMENT, TransferIntent::NORMAL,
                             nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::WAIT_STOP;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for sample response");

    case OperationPhase::WAIT_STOP:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for sample response");
      }
      _active.phase = OperationPhase::READ_RESPONSE;
      return Status::Error(Err::IN_PROGRESS, "Sample response due");

    case OperationPhase::READ_RESPONSE: {
      uint16_t words[3] = {};
      status = _readWords(words, 3, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _storeSample(words, kind != OperationKind::SINGLE_SHOT_RHT_ONLY, nowMs);
      _workingValue.sample = _latestSample;
      _finish(OperationOutcome::SUCCEEDED,
              kind == OperationKind::FETCH_SAMPLE ? EffectState::NONE
                                                  : EffectState::VERIFIED,
              Status::Ok(), nowMs);
      return Status::Ok();
    }

    default:
      return Status::Error(Err::COMMAND_FAILED, "Invalid measurement phase");
  }
}

Status SCD41::_stepMaintenance(uint32_t& nowMs,
                               uint8_t& callbacksRemaining) {
  const OperationKind kind = _active.request.kind;
  Status status;
  if (kind == OperationKind::PERSIST_SETTINGS &&
      _configuration.persistenceIndeterminate &&
      _active.phase == OperationPhase::SEND_COMMAND) {
    _workingValue.configuration = _configuration;
    _finish(OperationOutcome::INDETERMINATE, EffectState::UNKNOWN,
            Status::Error(Err::INDETERMINATE,
                          "Persistence requires reinit reconciliation"),
            nowMs);
    return Status::Ok();
  }
  if (kind == OperationKind::PERSIST_SETTINGS &&
      _configuration.dirtyMask == 0U &&
      _active.phase == OperationPhase::SEND_COMMAND) {
    _workingValue.configuration = _configuration;
    _finish(OperationOutcome::SUCCEEDED, EffectState::NOT_ATTEMPTED,
            Status::Ok(), nowMs);
    return Status::Ok();
  }

  switch (_active.phase) {
    case OperationPhase::SEND_COMMAND:
      if (kind == OperationKind::FORCED_RECALIBRATION) {
        status = _writeCommandWithWord(cmd::CMD_PERFORM_FORCED_RECALIBRATION,
                                       static_cast<uint16_t>(_active.request.value),
                                       nowMs, callbacksRemaining, true);
      } else {
        status = _writeCommand(_writeCommandFor(kind), TransferIntent::NORMAL,
                               nowMs, callbacksRemaining, true);
      }
      if (status.inProgress()) return status;
      if (!status.ok()) {
        if (kind == OperationKind::PERSIST_SETTINGS &&
            _lastTransferDisposition == TransferDisposition::INDETERMINATE) {
          _configuration.persistenceIndeterminate = true;
        }
        return status;
      }
      _active.effect = EffectState::ACKNOWLEDGED;
      if (kind == OperationKind::REINIT || kind == OperationKind::FACTORY_RESET) {
        _markReconciliationRequired();
      }
      if (kind == OperationKind::FACTORY_RESET) {
        _configuration.persistenceIndeterminate = true;
      }
      _active.phase = OperationPhase::WAIT_EXECUTION;
      _active.nextDueMs = nowMs + _executionWaitMs(kind);
      _nextSafeCommandMs = _active.nextDueMs;
      _nextSafeCommandValid = true;
      return Status::Error(Err::IN_PROGRESS, "Waiting for maintenance command");

    case OperationPhase::WAIT_EXECUTION:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for maintenance command");
      }
      if (kind == OperationKind::SELF_TEST ||
          kind == OperationKind::FORCED_RECALIBRATION) {
        _active.phase = OperationPhase::READ_DEFERRED_RESULT;
        return Status::Error(Err::IN_PROGRESS, "Maintenance result due");
      }
      if (kind == OperationKind::REINIT || kind == OperationKind::FACTORY_RESET) {
        _active.phase = OperationPhase::SEND_VERIFY_COMMAND;
        return Status::Error(Err::IN_PROGRESS, "Maintenance verification due");
      }
      if (kind == OperationKind::PERSIST_SETTINGS) {
        _configuration.dirtyMask = 0U;
        _configuration.persistenceIndeterminate = false;
        _workingValue.configuration = _configuration;
      }
      _finish(OperationOutcome::SUCCEEDED, EffectState::ACKNOWLEDGED,
              Status::Ok(), nowMs);
      return Status::Ok();

    case OperationPhase::READ_DEFERRED_RESULT: {
      uint16_t word = 0;
      status = _readWords(&word, 1, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _workingValue.value = word;
      _workingValue.rawWords[0] = word;
      _workingValue.wordCount = 1;
      if (kind == OperationKind::SELF_TEST) {
        if (word != cmd::SELF_TEST_PASS) {
          _finish(OperationOutcome::FAILED, EffectState::VERIFIED,
                  Status::Error(Err::COMMAND_FAILED, "Self-test failed", word),
                  nowMs);
        } else {
          _finish(OperationOutcome::SUCCEEDED, EffectState::VERIFIED,
                  Status::Ok(), nowMs);
        }
      } else if (word == cmd::FRC_FAILED) {
        _finish(OperationOutcome::FAILED, EffectState::VERIFIED,
                Status::Error(Err::COMMAND_FAILED, "FRC failed", word), nowMs);
      } else {
        _workingValue.signedValue =
            static_cast<int32_t>(word) - static_cast<int32_t>(cmd::FRC_OFFSET_BIAS);
        _finish(OperationOutcome::SUCCEEDED, EffectState::VERIFIED,
                Status::Ok(), nowMs);
      }
      return Status::Ok();
    }

    case OperationPhase::SEND_VERIFY_COMMAND:
      status = _writeCommand(
          _active.fieldIndex == 0U ? cmd::CMD_GET_SERIAL_NUMBER
                                   : cmd::CMD_GET_SENSOR_VARIANT,
          TransferIntent::NORMAL, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.phase = OperationPhase::SEND_READ_COMMAND;
      _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
      return Status::Error(Err::IN_PROGRESS, "Waiting for identity verify");

    case OperationPhase::SEND_READ_COMMAND:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for identity verify");
      }
      _active.phase = OperationPhase::READ_VERIFY_RESPONSE;
      return Status::Error(Err::IN_PROGRESS, "Identity verify response due");

    case OperationPhase::READ_VERIFY_RESPONSE: {
      uint16_t words[3] = {};
      const uint8_t count = _active.fieldIndex == 0U ? 3U : 1U;
      status = _readWords(words, count, nowMs, callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      if (_active.fieldIndex == 0U) {
        _workingValue.identity.serialNumber = serialNumberFromWords(words);
        _active.fieldIndex = 1U;
        _active.phase = OperationPhase::SEND_VERIFY_COMMAND;
        _active.nextDueMs = nowMs;
        return Status::Error(Err::IN_PROGRESS, "Reset variant read due");
      }
      const uint16_t variantWord = words[0];
      const SensorVariant variant = _variantFromVariantWord(variantWord);
      const uint64_t serialNumber = _workingValue.identity.serialNumber;
      if (_config.strictVariantCheck && variant != SensorVariant::SCD41) {
        _advanceSensorEpoch();
        _identity.serialNumber = serialNumber;
        _identity.variant = variant;
        _identity.variantWord = variantWord;
        _identity.sensorEpoch = _sensorEpoch;
        _identity.valid = true;
        _configuration = {};
        _configuration.sensorEpoch = _sensorEpoch;
        _workingValue.identity = _identity;
        _markReconciliationRequired();
        _finish(OperationOutcome::INDETERMINATE, EffectState::UNKNOWN,
                Status::Error(Err::UNSUPPORTED,
                              "Reset verified wrong variant"), nowMs);
        return Status::Ok();
      }
      if (_identity.valid &&
          (_identity.serialNumber != serialNumber ||
           _identity.variant != variant)) {
        _advanceSensorEpoch();
        _identity.serialNumber = serialNumber;
        _identity.variant = variant;
        _identity.variantWord = variantWord;
        _identity.sensorEpoch = _sensorEpoch;
        _identity.valid = true;
        _configuration = {};
        _configuration.sensorEpoch = _sensorEpoch;
        _workingValue.identity = _identity;
        _markReconciliationRequired();
        _finish(OperationOutcome::INDETERMINATE, EffectState::UNKNOWN,
                Status::Error(Err::RECONCILIATION_REQUIRED,
                              "Sensor changed during reset verification"),
                nowMs);
        return Status::Ok();
      }
      _advanceSensorEpoch();
      _identity.serialNumber = serialNumber;
      _identity.variant = variant;
      _identity.variantWord = variantWord;
      _identity.sensorEpoch = _sensorEpoch;
      _identity.valid = true;
      _configuration.verifiedMask = 0;
      _configuration.dirtyMask = 0;
      _configuration.sensorEpoch = _sensorEpoch;
      _configuration.persistenceIndeterminate = false;
      _workingValue.identity = _identity;
      _workingValue.configuration = _configuration;
      _attached = true;
      _reconciliationRequired = false;
      _setMode(OperatingMode::IDLE, ModeEvidence::VERIFIED);
      _finish(OperationOutcome::SUCCEEDED, EffectState::ACKNOWLEDGED,
              Status::Ok(), nowMs);
      return Status::Ok();
    }

    default:
      return Status::Error(Err::COMMAND_FAILED, "Invalid maintenance phase");
  }
}

Status SCD41::_stepDiagnostic(uint32_t& nowMs,
                              uint8_t& callbacksRemaining) {
  const OperationKind kind = _active.request.kind;
  Status status;
  switch (_active.phase) {
    case OperationPhase::SEND_COMMAND:
      if (kind == OperationKind::DIAGNOSTIC_READ_WORDS) {
        status = _writeCommand(_active.request.command, TransferIntent::NORMAL,
                               nowMs, callbacksRemaining, true);
        if (status.inProgress()) return status;
        if (!status.ok()) return status;
        _active.effect = EffectState::ACKNOWLEDGED;
        _markReconciliationRequired();
        _active.phase = OperationPhase::WAIT_EXECUTION;
        _active.nextDueMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
        return Status::Error(Err::IN_PROGRESS, "Waiting for diagnostic read");
      }
      if (kind == OperationKind::DIAGNOSTIC_WRITE_WORD) {
        status = _writeCommandWithWord(_active.request.command,
                                       static_cast<uint16_t>(_active.request.value),
                                       nowMs, callbacksRemaining, true);
      } else {
        status = _writeCommand(_active.request.command, TransferIntent::NORMAL,
                               nowMs, callbacksRemaining, true);
      }
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      _active.effect = EffectState::ACKNOWLEDGED;
      _markReconciliationRequired();
      _finish(OperationOutcome::SUCCEEDED, EffectState::ACKNOWLEDGED,
              Status::Ok(), nowMs);
      return Status::Ok();

    case OperationPhase::WAIT_EXECUTION:
      if (!_timeReached(nowMs, _active.nextDueMs)) {
        return Status::Error(Err::IN_PROGRESS, "Waiting for diagnostic read");
      }
      _active.phase = OperationPhase::READ_RESPONSE;
      return Status::Error(Err::IN_PROGRESS, "Diagnostic response due");

    case OperationPhase::READ_RESPONSE: {
      uint16_t words[3] = {};
      status = _readWords(words, _active.request.wordCount, nowMs,
                          callbacksRemaining);
      if (status.inProgress()) return status;
      if (!status.ok()) return status;
      std::memcpy(_workingValue.rawWords, words,
                  _active.request.wordCount * sizeof(uint16_t));
      _workingValue.wordCount = _active.request.wordCount;
      _finish(OperationOutcome::SUCCEEDED, EffectState::ACKNOWLEDGED,
              Status::Ok(), nowMs);
      return Status::Ok();
    }

    default:
      return Status::Error(Err::COMMAND_FAILED, "Invalid diagnostic phase");
  }
}

Status SCD41::_writeCommand(uint16_t command, TransferIntent intent,
                            uint32_t& nowMs, uint8_t& callbacksRemaining,
                            bool effectful) {
  const Status spacing = _checkCommandSpacing(nowMs);
  if (!spacing.ok()) {
    return spacing;
  }
  const uint8_t data[2] = {static_cast<uint8_t>(command >> 8),
                           static_cast<uint8_t>(command & 0xFFU)};
  return _attemptTransfer(data, sizeof(data), nullptr, 0, intent, nowMs,
                          callbacksRemaining, effectful);
}

Status SCD41::_writeCommandWithWord(uint16_t command, uint16_t word,
                                    uint32_t& nowMs,
                                    uint8_t& callbacksRemaining,
                                    bool effectful) {
  const Status spacing = _checkCommandSpacing(nowMs);
  if (!spacing.ok()) {
    return spacing;
  }
  uint8_t data[5] = {static_cast<uint8_t>(command >> 8),
                     static_cast<uint8_t>(command & 0xFFU),
                     static_cast<uint8_t>(word >> 8),
                     static_cast<uint8_t>(word & 0xFFU), 0U};
  data[4] = _crc8(&data[2], 2);
  return _attemptTransfer(data, sizeof(data), nullptr, 0,
                          TransferIntent::NORMAL, nowMs, callbacksRemaining,
                          effectful);
}

Status SCD41::_readWords(uint16_t* words, uint8_t count, uint32_t& nowMs,
                         uint8_t& callbacksRemaining) {
  if (words == nullptr || count == 0U || count > 3U) {
    return Status::Error(Err::INVALID_PARAM, "Invalid word read");
  }
  const Status spacing = _checkCommandSpacing(nowMs);
  if (!spacing.ok()) {
    return spacing;
  }

  uint8_t bytes[cmd::MEASUREMENT_RESPONSE_LEN] = {};
  const size_t length = static_cast<size_t>(count) * cmd::DATA_WORD_WITH_CRC;
  Status status = _attemptTransfer(nullptr, 0, bytes, length,
                                   TransferIntent::NORMAL, nowMs,
                                   callbacksRemaining, false);
  if (!status.ok()) {
    return status;
  }

  uint16_t decoded[3] = {};
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* wordBytes = &bytes[static_cast<size_t>(i) * 3U];
    if (_crc8(wordBytes, 2) != wordBytes[2]) {
      status = Status::Error(Err::CRC_MISMATCH, "CRC mismatch", i);
      _recordProtocolFailure(status, nowMs);
      return status;
    }
    decoded[i] = static_cast<uint16_t>(
        (static_cast<uint16_t>(wordBytes[0]) << 8) | wordBytes[1]);
  }
  std::memcpy(words, decoded, static_cast<size_t>(count) * sizeof(uint16_t));
  return Status::Ok();
}

Status SCD41::_attemptTransfer(const uint8_t* writeData, size_t writeLength,
                               uint8_t* readData, size_t readLength,
                               TransferIntent intent, uint32_t& nowMs,
                               uint8_t& callbacksRemaining,
                               bool effectful) {
  if (callbacksRemaining == 0U) {
    return Status::Error(Err::IN_PROGRESS, "Poll callback budget exhausted");
  }
  if (_timeReached(nowMs, _active.deadlineMs)) {
    return Status::Error(Err::TIMEOUT, "Operation deadline expired");
  }

  const uint32_t remainingMs = _active.deadlineMs - nowMs;
  const uint32_t timeoutMs = remainingMs < _config.transferTimeoutMs
                                 ? remainingMs
                                 : _config.transferTimeoutMs;
  if (timeoutMs == 0U) {
    return Status::Error(Err::TIMEOUT, "No transfer time remaining");
  }

  TransferRequest request;
  request.address = cmd::I2C_ADDRESS;
  request.writeData = writeData;
  request.writeLength = writeLength;
  request.readData = readData;
  request.readLength = readLength;
  request.timeoutMs = timeoutMs;
  request.intent = intent;

  --callbacksRemaining;
  incrementSaturating(_active.callbacksUsed);
  const uint32_t attemptStartedMs = nowMs;
  const TransferResult transfer = _config.transfer(request, _config.transferUser);
  TransferResult normalized = transfer;
  const bool completionClockValid =
      (transfer.completedMs - attemptStartedMs) < 0x80000000UL;
  if (!completionClockValid) {
    normalized.code = TransferCode::FAILED;
    normalized.completedMs = attemptStartedMs;
    if (normalized.disposition != TransferDisposition::NOT_STARTED) {
      normalized.disposition = TransferDisposition::INDETERMINATE;
    }
  }
  const size_t expectedBytes = writeLength + readLength;
  if (normalized.code == TransferCode::OK &&
      (normalized.disposition != TransferDisposition::COMPLETE ||
       normalized.bytesTransferred != expectedBytes)) {
    normalized.code = TransferCode::SHORT_TRANSFER;
    normalized.disposition = TransferDisposition::INDETERMINATE;
  }
  if (normalized.code != TransferCode::OK &&
      normalized.disposition == TransferDisposition::COMPLETE) {
    normalized.disposition = TransferDisposition::INDETERMINATE;
  }
  _lastTransferDisposition = normalized.disposition;
  _lastTransferWasEffectful = effectful;
  if (effectful && normalized.disposition != TransferDisposition::NOT_STARTED) {
    _active.effectfulWriteAttempted = true;
    if (_active.effect == EffectState::NOT_ATTEMPTED) {
      _active.effect = EffectState::ATTEMPTED;
    }
  }
  const bool expectedNack =
      intent == TransferIntent::EXPECTED_WRITE_NACK &&
      normalized.code == TransferCode::NACK &&
      normalized.disposition != TransferDisposition::NOT_STARTED;
  _recordTransfer(normalized, expectedNack);

  nowMs = completionClockValid ? transfer.completedMs : attemptStartedMs;
  if (completionClockValid) {
    _lastOwnerNowMs = nowMs;
    _lastOwnerNowValid = true;
  }
  if (normalized.disposition != TransferDisposition::NOT_STARTED) {
    _nextSafeCommandMs = nowMs + cmd::EXECUTION_TIME_SHORT_MS;
    _nextSafeCommandValid = true;
  }

  if (!completionClockValid) {
    return Status::Error(Err::I2C_ERROR,
                         "Transport completion clock moved backwards");
  }

  if (expectedNack) {
    if (_timeReached(nowMs, _active.deadlineMs)) {
      return Status::Error(Err::TIMEOUT, "Transfer crossed operation deadline");
    }
    return Status::Ok();
  }

  Status status = transferStatus(normalized);
  if (!status.ok()) {
    return status;
  }
  if (_timeReached(nowMs, _active.deadlineMs)) {
    return Status::Error(Err::TIMEOUT, "Transfer crossed operation deadline");
  }
  return Status::Ok();
}

Status SCD41::_checkCommandSpacing(uint32_t nowMs) {
  if (!_nextSafeCommandValid ||
      _timeReached(nowMs, _nextSafeCommandMs)) {
    return Status::Ok();
  }
  _active.nextDueMs = _nextSafeCommandMs;
  return Status::Error(Err::IN_PROGRESS, "Command spacing active");
}

void SCD41::_finish(OperationOutcome outcome, EffectState effect,
                    const Status& status, uint32_t completedMs) {
  if (!_activeValid || _terminalValid) {
    return;
  }
  if (_fieldFor(_active.request.kind) != ConfigurationField::NONE ||
      _active.request.kind == OperationKind::READ_CONFIGURATION ||
      _active.request.kind == OperationKind::PERSIST_SETTINGS ||
      _active.request.kind == OperationKind::REINIT ||
      _active.request.kind == OperationKind::FACTORY_RESET) {
    _workingValue.configuration = _configuration;
  }
  OperationResult result;
  result.id = _active.id;
  result.kind = _active.request.kind;
  result.outcome = outcome;
  result.effect = effect;
  result.status = status;
  result.finalPhase = _active.phase;
  result.startedMs = _active.startedMs;
  result.completedMs = completedMs;
  result.deadlineMs = _active.deadlineMs;
  result.sensorEpoch = _sensorEpoch;
  result.operatingMode = _operatingMode;
  result.modeEvidence = _modeEvidence;
  result.completedFieldMask = _active.completedFieldMask;
  result.callbacksUsed = _active.callbacksUsed;
  result.reconciliationRequired = _reconciliationRequired;
  result.value = _workingValue;
  _terminal = result;
  _terminalValid = true;
  _active = {};
  _activeValid = false;
  _recordOperationOutcome(_terminal);
}

void SCD41::_finishTransferFailure(const Status& status, uint32_t completedMs) {
  if (!_activeValid) {
    return;
  }
  OperationOutcome outcome = status.code == Err::TIMEOUT
                                 ? OperationOutcome::TIMED_OUT
                                 : OperationOutcome::FAILED;
  EffectState effect = _active.effect;
  if (_lastTransferWasEffectful) {
    if (_lastTransferDisposition == TransferDisposition::INDETERMINATE) {
      effect = EffectState::UNKNOWN;
      outcome = OperationOutcome::INDETERMINATE;
      _markReconciliationRequired();
    } else if (_lastTransferDisposition == TransferDisposition::NO_EFFECT) {
      if (effect == EffectState::NOT_ATTEMPTED ||
          effect == EffectState::ATTEMPTED) {
        effect = EffectState::ATTEMPTED;
      }
    } else if (_lastTransferDisposition == TransferDisposition::NOT_STARTED) {
      if (effect == EffectState::NOT_ATTEMPTED) {
        effect = EffectState::NOT_ATTEMPTED;
      }
    } else if (status.code == Err::TIMEOUT) {
      effect = EffectState::ACKNOWLEDGED;
      _markReconciliationRequired();
    }
  }
  if ((_active.request.kind == OperationKind::PERSIST_SETTINGS ||
       _active.request.kind == OperationKind::FACTORY_RESET) &&
      (effect == EffectState::UNKNOWN ||
       (_lastTransferWasEffectful && status.code == Err::TIMEOUT &&
        _lastTransferDisposition == TransferDisposition::COMPLETE))) {
    _configuration.persistenceIndeterminate = true;
  }
  if (_active.request.kind == OperationKind::READ_CONFIGURATION &&
      _active.completedFieldMask != 0U) {
    _workingValue.configuration = _configuration;
    outcome = OperationOutcome::PARTIAL;
  }
  _finish(outcome, effect, status, completedMs);
}

void SCD41::_applyReadValue(OperationKind kind, uint16_t value,
                            uint32_t nowMs) {
  (void)nowMs;
  const ConfigurationField field = _fieldFor(kind);
  switch (field) {
    case ConfigurationField::TEMPERATURE_OFFSET:
      _configuration.temperatureOffsetMilliC =
          decodeTemperatureOffsetMilliC(value);
      _workingValue.signedValue = _configuration.temperatureOffsetMilliC;
      break;
    case ConfigurationField::SENSOR_ALTITUDE:
      _configuration.sensorAltitudeM = value;
      _workingValue.value = value;
      break;
    case ConfigurationField::AMBIENT_PRESSURE:
      _configuration.ambientPressurePa = decodeAmbientPressurePa(value);
      _workingValue.value = _configuration.ambientPressurePa;
      break;
    case ConfigurationField::ASC_ENABLED:
      _configuration.ascEnabled = value != 0U;
      _workingValue.boolValue = _configuration.ascEnabled;
      break;
    case ConfigurationField::ASC_TARGET:
      _configuration.ascTargetPpm = value;
      _workingValue.value = value;
      break;
    case ConfigurationField::ASC_INITIAL_PERIOD:
      _configuration.ascInitialPeriodHours = value;
      _workingValue.value = value;
      break;
    case ConfigurationField::ASC_STANDARD_PERIOD:
      _configuration.ascStandardPeriodHours = value;
      _workingValue.value = value;
      break;
    case ConfigurationField::NONE:
      return;
  }
  _configuration.verifiedMask |= configurationFieldMask(field);
  _configuration.sensorEpoch = _sensorEpoch;
  _workingValue.configuration = _configuration;
}

void SCD41::_applyVerifiedSetting(OperationKind kind, uint16_t value) {
  _applyReadValue(kind, value, 0);
}

void SCD41::_storeSample(const uint16_t words[3], bool co2Valid,
                         uint32_t nowMs) {
  incrementSaturating(_sampleSequence);
  if (_sampleSequence == 0U) {
    _sampleSequence = 1U;
  }
  _latestSample.co2Ppm = words[0];
  _latestSample.temperatureMilliC = convertTemperatureMilliC(words[1]);
  _latestSample.humidityMilliPercent = convertHumidityMilliPercent(words[2]);
  _latestSample.capturedAtMs = nowMs;
  _latestSample.sensorEpoch = _sensorEpoch;
  _latestSample.sequence = _sampleSequence;
  _latestSample.mode = _operatingMode;
  _latestSample.flags = SAMPLE_TEMPERATURE_VALID | SAMPLE_HUMIDITY_VALID |
                        SAMPLE_FRESH;
  if (co2Valid) {
    _latestSample.flags |= SAMPLE_CO2_VALID;
  }
  _latestSampleValid = true;
}

void SCD41::_setMode(OperatingMode mode, ModeEvidence evidence) {
  if (_operatingMode != mode) {
    _sampleSequence = 0U;
  }
  _operatingMode = mode;
  _modeEvidence = evidence;
}

void SCD41::_markReconciliationRequired() {
  _attached = false;
  _reconciliationRequired = true;
  _setMode(OperatingMode::UNKNOWN, ModeEvidence::UNKNOWN);
  _configuration.verifiedMask = 0;
  _latestSampleValid = false;
}

void SCD41::_advanceSensorEpoch() {
  incrementSaturating(_sensorEpoch);
  if (_sensorEpoch == 0U) {
    _sensorEpoch = 1U;
  }
  _sampleSequence = 0U;
  _latestSampleValid = false;
}

void SCD41::_recordTransfer(const TransferResult& result, bool expectedNack) {
  if (expectedNack) {
    incrementSaturating(_health.expectedNacks);
    return;
  }
  if (result.code == TransferCode::OK &&
      result.disposition == TransferDisposition::COMPLETE) {
    _health.lastTransferOkMs = result.completedMs;
    incrementSaturating(_health.totalTransferSuccess);
    _health.consecutiveTransferFailures = 0;
    _driverState = _bound ? DriverState::READY : DriverState::UNINIT;
    _health.state = _driverState;
    return;
  }
  if (result.disposition == TransferDisposition::NOT_STARTED) {
    return;
  }
  _health.lastTransferErrorMs = result.completedMs;
  _health.lastTransferError = transferStatus(result);
  incrementSaturating(_health.totalTransferFailures);
  incrementSaturating(_health.consecutiveTransferFailures);
  if (_config.offlineThreshold != 0U &&
      _health.consecutiveTransferFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }
  _health.state = _driverState;
}

void SCD41::_recordProtocolFailure(const Status& status, uint32_t nowMs) {
  incrementSaturating(_health.totalProtocolFailures);
  if (status.code == Err::CRC_MISMATCH) {
    incrementSaturating(_health.totalCrcFailures);
  }
  _health.lastProtocolErrorMs = nowMs;
  _health.lastProtocolError = status;
}

void SCD41::_recordOperationOutcome(const OperationResult& result) {
  _health.lastOperationId = result.id;
  _health.lastOperationKind = result.kind;
  switch (result.outcome) {
    case OperationOutcome::SUCCEEDED:
    case OperationOutcome::NO_DATA:
      incrementSaturating(_health.totalOperationSuccess);
      break;
    case OperationOutcome::CANCELLED:
      incrementSaturating(_health.totalOperationCancelled);
      _health.lastOperationErrorMs = result.completedMs;
      _health.lastOperationError = result.status;
      _health.lastOperationErrorId = result.id;
      _health.lastOperationErrorKind = result.kind;
      break;
    case OperationOutcome::FAILED:
    case OperationOutcome::TIMED_OUT:
    case OperationOutcome::PARTIAL:
    case OperationOutcome::INDETERMINATE:
      incrementSaturating(_health.totalOperationFailures);
      _health.lastOperationErrorMs = result.completedMs;
      _health.lastOperationError = result.status;
      _health.lastOperationErrorId = result.id;
      _health.lastOperationErrorKind = result.kind;
      break;
  }
}

bool SCD41::_timeReached(uint32_t nowMs, uint32_t targetMs) {
  return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

bool SCD41::_deadlineValid(uint32_t nowMs, uint32_t deadlineMs) {
  const uint32_t distance = deadlineMs - nowMs;
  return distance != 0U && distance < 0x80000000UL;
}

bool SCD41::_isSettingWrite(OperationKind kind) {
  switch (kind) {
    case OperationKind::SET_TEMPERATURE_OFFSET:
    case OperationKind::SET_SENSOR_ALTITUDE:
    case OperationKind::SET_AMBIENT_PRESSURE:
    case OperationKind::SET_ASC_ENABLED:
    case OperationKind::SET_ASC_TARGET:
    case OperationKind::SET_ASC_INITIAL_PERIOD:
    case OperationKind::SET_ASC_STANDARD_PERIOD:
      return true;
    default:
      return false;
  }
}

bool SCD41::_isMaintenance(OperationKind kind) {
  switch (kind) {
    case OperationKind::REINIT:
    case OperationKind::SELF_TEST:
    case OperationKind::FORCED_RECALIBRATION:
    case OperationKind::PERSIST_SETTINGS:
    case OperationKind::FACTORY_RESET:
      return true;
    default:
      return false;
  }
}

bool SCD41::_isDiagnostic(OperationKind kind) {
  return kind == OperationKind::DIAGNOSTIC_READ_WORDS ||
         kind == OperationKind::DIAGNOSTIC_WRITE_COMMAND ||
         kind == OperationKind::DIAGNOSTIC_WRITE_WORD;
}

bool SCD41::_isEffectful(OperationKind kind) {
  return _isSettingWrite(kind) || _isMaintenance(kind) ||
         kind == OperationKind::ATTACH ||
         kind == OperationKind::START_PERIODIC ||
         kind == OperationKind::START_LOW_POWER_PERIODIC ||
         kind == OperationKind::STOP_PERIODIC ||
         kind == OperationKind::SINGLE_SHOT ||
         kind == OperationKind::SINGLE_SHOT_RHT_ONLY ||
         kind == OperationKind::POWER_DOWN || kind == OperationKind::WAKE_UP ||
         kind == OperationKind::DIAGNOSTIC_READ_WORDS ||
         kind == OperationKind::DIAGNOSTIC_WRITE_COMMAND ||
         kind == OperationKind::DIAGNOSTIC_WRITE_WORD;
}

bool SCD41::_periodicAllowed(OperationKind kind) {
  return kind == OperationKind::READ_DATA_READY ||
         kind == OperationKind::FETCH_SAMPLE ||
         kind == OperationKind::READ_AMBIENT_PRESSURE ||
         kind == OperationKind::SET_AMBIENT_PRESSURE ||
         kind == OperationKind::STOP_PERIODIC;
}

uint16_t SCD41::_readCommandFor(OperationKind kind) {
  switch (kind) {
    case OperationKind::READ_IDENTITY:
      return cmd::CMD_GET_SERIAL_NUMBER;
    case OperationKind::READ_SENSOR_VARIANT:
      return cmd::CMD_GET_SENSOR_VARIANT;
    case OperationKind::READ_DATA_READY:
      return cmd::CMD_GET_DATA_READY_STATUS;
    case OperationKind::READ_TEMPERATURE_OFFSET:
    case OperationKind::SET_TEMPERATURE_OFFSET:
      return cmd::CMD_GET_TEMPERATURE_OFFSET;
    case OperationKind::READ_SENSOR_ALTITUDE:
    case OperationKind::SET_SENSOR_ALTITUDE:
      return cmd::CMD_GET_SENSOR_ALTITUDE;
    case OperationKind::READ_AMBIENT_PRESSURE:
    case OperationKind::SET_AMBIENT_PRESSURE:
      return cmd::CMD_GET_AMBIENT_PRESSURE;
    case OperationKind::READ_ASC_ENABLED:
    case OperationKind::SET_ASC_ENABLED:
      return cmd::CMD_GET_ASC_ENABLED;
    case OperationKind::READ_ASC_TARGET:
    case OperationKind::SET_ASC_TARGET:
      return cmd::CMD_GET_ASC_TARGET;
    case OperationKind::READ_ASC_INITIAL_PERIOD:
    case OperationKind::SET_ASC_INITIAL_PERIOD:
      return cmd::CMD_GET_ASC_INITIAL_PERIOD;
    case OperationKind::READ_ASC_STANDARD_PERIOD:
    case OperationKind::SET_ASC_STANDARD_PERIOD:
      return cmd::CMD_GET_ASC_STANDARD_PERIOD;
    default:
      return 0U;
  }
}

uint16_t SCD41::_writeCommandFor(OperationKind kind) {
  switch (kind) {
    case OperationKind::START_PERIODIC:
      return cmd::CMD_START_PERIODIC_MEASUREMENT;
    case OperationKind::START_LOW_POWER_PERIODIC:
      return cmd::CMD_START_LOW_POWER_PERIODIC_MEASUREMENT;
    case OperationKind::STOP_PERIODIC:
      return cmd::CMD_STOP_PERIODIC_MEASUREMENT;
    case OperationKind::SINGLE_SHOT:
      return cmd::CMD_MEASURE_SINGLE_SHOT;
    case OperationKind::SINGLE_SHOT_RHT_ONLY:
      return cmd::CMD_MEASURE_SINGLE_SHOT_RHT_ONLY;
    case OperationKind::SET_TEMPERATURE_OFFSET:
      return cmd::CMD_SET_TEMPERATURE_OFFSET;
    case OperationKind::SET_SENSOR_ALTITUDE:
      return cmd::CMD_SET_SENSOR_ALTITUDE;
    case OperationKind::SET_AMBIENT_PRESSURE:
      return cmd::CMD_SET_AMBIENT_PRESSURE;
    case OperationKind::SET_ASC_ENABLED:
      return cmd::CMD_SET_ASC_ENABLED;
    case OperationKind::SET_ASC_TARGET:
      return cmd::CMD_SET_ASC_TARGET;
    case OperationKind::SET_ASC_INITIAL_PERIOD:
      return cmd::CMD_SET_ASC_INITIAL_PERIOD;
    case OperationKind::SET_ASC_STANDARD_PERIOD:
      return cmd::CMD_SET_ASC_STANDARD_PERIOD;
    case OperationKind::POWER_DOWN:
      return cmd::CMD_POWER_DOWN;
    case OperationKind::WAKE_UP:
      return cmd::CMD_WAKE_UP;
    case OperationKind::REINIT:
      return cmd::CMD_REINIT;
    case OperationKind::SELF_TEST:
      return cmd::CMD_PERFORM_SELF_TEST;
    case OperationKind::PERSIST_SETTINGS:
      return cmd::CMD_PERSIST_SETTINGS;
    case OperationKind::FACTORY_RESET:
      return cmd::CMD_PERFORM_FACTORY_RESET;
    default:
      return 0U;
  }
}

ConfigurationField SCD41::_fieldFor(OperationKind kind) {
  switch (kind) {
    case OperationKind::READ_TEMPERATURE_OFFSET:
    case OperationKind::SET_TEMPERATURE_OFFSET:
      return ConfigurationField::TEMPERATURE_OFFSET;
    case OperationKind::READ_SENSOR_ALTITUDE:
    case OperationKind::SET_SENSOR_ALTITUDE:
      return ConfigurationField::SENSOR_ALTITUDE;
    case OperationKind::READ_AMBIENT_PRESSURE:
    case OperationKind::SET_AMBIENT_PRESSURE:
      return ConfigurationField::AMBIENT_PRESSURE;
    case OperationKind::READ_ASC_ENABLED:
    case OperationKind::SET_ASC_ENABLED:
      return ConfigurationField::ASC_ENABLED;
    case OperationKind::READ_ASC_TARGET:
    case OperationKind::SET_ASC_TARGET:
      return ConfigurationField::ASC_TARGET;
    case OperationKind::READ_ASC_INITIAL_PERIOD:
    case OperationKind::SET_ASC_INITIAL_PERIOD:
      return ConfigurationField::ASC_INITIAL_PERIOD;
    case OperationKind::READ_ASC_STANDARD_PERIOD:
    case OperationKind::SET_ASC_STANDARD_PERIOD:
      return ConfigurationField::ASC_STANDARD_PERIOD;
    default:
      return ConfigurationField::NONE;
  }
}

SensorVariant SCD41::_variantFromVariantWord(uint16_t variantWord) {
  switch (static_cast<uint8_t>((variantWord & cmd::SENSOR_VARIANT_MASK) >>
                               cmd::SENSOR_VARIANT_SHIFT)) {
    case cmd::SENSOR_VARIANT_SCD40:
      return SensorVariant::SCD40;
    case cmd::SENSOR_VARIANT_SCD41:
      return SensorVariant::SCD41;
    case cmd::SENSOR_VARIANT_SCD43:
      return SensorVariant::SCD43;
    default:
      return SensorVariant::UNKNOWN;
  }
}

uint8_t SCD41::_crc8(const uint8_t* data, size_t length) {
  uint8_t crc = cmd::CRC_INIT;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x80U) != 0U
                ? static_cast<uint8_t>((crc << 1U) ^ cmd::CRC_POLY)
                : static_cast<uint8_t>(crc << 1U);
    }
  }
  return crc;
}

uint32_t SCD41::_executionWaitMs(OperationKind kind) {
  switch (kind) {
    case OperationKind::STOP_PERIODIC:
      return cmd::EXECUTION_TIME_STOP_PERIODIC_MS;
    case OperationKind::SINGLE_SHOT:
      return cmd::EXECUTION_TIME_SINGLE_SHOT_MS;
    case OperationKind::SINGLE_SHOT_RHT_ONLY:
      return cmd::EXECUTION_TIME_SINGLE_SHOT_RHT_MS;
    case OperationKind::WAKE_UP:
      return cmd::EXECUTION_TIME_POWER_UP_MS;
    case OperationKind::REINIT:
      return cmd::EXECUTION_TIME_REINIT_MS;
    case OperationKind::SELF_TEST:
      return cmd::EXECUTION_TIME_SELF_TEST_MS;
    case OperationKind::FORCED_RECALIBRATION:
      return cmd::EXECUTION_TIME_FRC_MS;
    case OperationKind::PERSIST_SETTINGS:
      return cmd::EXECUTION_TIME_PERSIST_MS;
    case OperationKind::FACTORY_RESET:
      return cmd::EXECUTION_TIME_FACTORY_RESET_MS;
    default:
      return cmd::EXECUTION_TIME_SHORT_MS;
  }
}

}  // namespace SCD41
