/**
 * @file SCD41.cpp
 * @brief SCD41 driver implementation.
 */

#include "SCD41/SCD41.h"

#include <Arduino.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace SCD41 {
namespace {

static constexpr size_t MAX_WRITE_LEN = 5;
static constexpr uint16_t MAX_COMMAND_DELAY_MS = 1000;
static constexpr uint16_t MAX_POWER_UP_DELAY_MS = 1000;
static constexpr uint32_t MAX_I2C_TIMEOUT_MS = 60000;
static constexpr uint32_t MAX_RECOVER_BACKOFF_MS = 600000;
static constexpr uint32_t MAX_PERIODIC_MARGIN_MS = 5000;
static constexpr uint32_t MAX_RETRY_DELAY_MS = 60000;
static constexpr uint32_t MIN_COMMAND_DELAY_US = 1000;
static constexpr uint32_t MAX_WAIT_GUARD_EXTRA_ITERATIONS = 16;

class ScopedOfflineI2cAllowance {
public:
  explicit ScopedOfflineI2cAllowance(bool& flag, bool allow) : _flag(flag), _old(flag) {
    _flag = allow;
  }

  ~ScopedOfflineI2cAllowance() {
    _flag = _old;
  }

  ScopedOfflineI2cAllowance(const ScopedOfflineI2cAllowance&) = delete;
  ScopedOfflineI2cAllowance& operator=(const ScopedOfflineI2cAllowance&) = delete;

private:
  bool& _flag;
  bool _old;
};

static uint32_t saturatingAddU32(uint32_t a, uint32_t b) {
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  if (a > (maxU32 - b)) {
    return maxU32;
  }
  return static_cast<uint32_t>(a + b);
}

static bool isValidSingleShotMode(SingleShotMode mode) {
  return mode == SingleShotMode::CO2_T_RH || mode == SingleShotMode::T_RH_ONLY;
}

static uint32_t boundedWaitIterations(uint32_t delayMs, uint32_t timeoutMs) {
  const uint32_t windowMs = saturatingAddU32(saturatingAddU32(delayMs, timeoutMs), 1U);
  const uint32_t scaled = (windowMs > (std::numeric_limits<uint32_t>::max() / 4U))
                              ? std::numeric_limits<uint32_t>::max()
                              : windowMs * 4U;
  return saturatingAddU32(scaled, MAX_WAIT_GUARD_EXTRA_ITERATIONS);
}

} // namespace

Status SCD41::begin(const Config& config) {
  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _operatingMode = OperatingMode::IDLE;
  _singleShotMode = SingleShotMode::CO2_T_RH;
  _pendingCommand = PendingCommand::NONE;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _allowOfflineI2c = false;
  _lastCommandUs = 0;
  _lastCommandValid = false;
  _commandReadyMs = 0;
  _measurementRequested = false;
  _measurementReady = false;
  _hasSample = false;
  _measurementReadyMs = 0;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _lastSampleCo2Valid = false;
  _rawSample = RawSample{};
  _compSample = CompensatedSample{};
  _sensorVariant = SensorVariant::UNKNOWN;
  _serialNumber = 0;
  _serialNumberValid = false;
  _selfTestRaw = 0;
  _selfTestRawValid = false;
  _selfTestStatus = Status::Error(Err::MEASUREMENT_NOT_READY, "Self-test not started");
  _selfTestCompleted = false;
  _forcedRecalibrationRaw = 0;
  _forcedRecalibrationRawValid = false;
  _forcedRecalibrationCorrectionPpm = 0;
  _forcedRecalibrationStatus =
      Status::Error(Err::MEASUREMENT_NOT_READY, "Forced recalibration not started");
  _forcedRecalibrationCompleted = false;
  _lastRecoverMs = 0;
  _lastRecoverValid = false;

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cAddress != cmd::I2C_ADDRESS) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address");
  }
  if (config.i2cTimeoutMs == 0 || config.i2cTimeoutMs > MAX_I2C_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C timeout");
  }
  if (!isValidSingleShotMode(config.singleShotMode)) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid single-shot mode");
  }
  if (config.commandDelayMs == 0 || config.commandDelayMs > MAX_COMMAND_DELAY_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid command delay");
  }
  if (config.powerUpDelayMs > MAX_POWER_UP_DELAY_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Power-up delay too large");
  }
  if (config.periodicFetchMarginMs > MAX_PERIODIC_MARGIN_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Periodic fetch margin too large");
  }
  if (config.dataReadyRetryMs == 0 || config.dataReadyRetryMs > MAX_RETRY_DELAY_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid retry delay");
  }
  if (config.recoverBackoffMs > MAX_RECOVER_BACKOFF_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Recover backoff too large");
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }
  _singleShotMode = _config.singleShotMode;

  Status st = _waitMs(_config.powerUpDelayMs);
  if (!st.ok()) {
    return st;
  }

  uint64_t serial = 0;
  st = readSerialNumber(serial);
  if (!st.ok()) {
    if (_isI2cFailure(st.code)) {
      return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
    }
    return st;
  }

  if (_config.strictVariantCheck && _sensorVariant != SensorVariant::SCD41) {
    return Status::Error(Err::UNSUPPORTED, "Sensor is not SCD41",
                         static_cast<int32_t>(static_cast<uint8_t>(_sensorVariant)));
  }

  _initialized = true;
  _driverState = DriverState::READY;
  return Status::Ok();
}

void SCD41::tick(uint32_t nowMs) {
  if (!_initialized) {
    return;
  }

  if (_pendingCommand != PendingCommand::NONE && _timeElapsed(nowMs, _commandReadyMs)) {
    (void)_handlePendingCommand(nowMs);
  }

  if (_pendingCommand == PendingCommand::NONE && _measurementRequested &&
      _timeElapsed(nowMs, _measurementReadyMs)) {
    (void)_completeMeasurement();
  }
}

void SCD41::end() {
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _operatingMode = OperatingMode::IDLE;
  _pendingCommand = PendingCommand::NONE;
  _commandReadyMs = 0;
  _measurementRequested = false;
  _measurementReady = false;
  _hasSample = false;
  _measurementReadyMs = 0;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _lastCommandValid = false;
}

Status SCD41::probe() {
  if (_config.i2cWrite == nullptr || _config.i2cWriteRead == nullptr) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_initialized && _pendingCommand != PendingCommand::NONE) {
    return Status::Error(Err::BUSY, "Command in progress");
  }
  if (_initialized && _operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }

  if (isPeriodicActive()) {
    uint16_t raw = 0;
    Status st = _readWord(cmd::CMD_GET_DATA_READY_STATUS, raw, false);
    if (!st.ok()) {
      if (_isI2cFailure(st.code)) {
        return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
      }
      return st;
    }
    return Status::Ok();
  }

  uint16_t words[3] = {};
  Status st = _readWords(cmd::CMD_GET_SERIAL_NUMBER, words, 3, false);
  if (!st.ok()) {
    if (_isI2cFailure(st.code)) {
      return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
    }
    return st;
  }

  return Status::Ok();
}

Status SCD41::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand != PendingCommand::NONE) {
    return Status::Error(Err::BUSY, "Command in progress");
  }

  const uint32_t now = _nowMs();
  if (_lastRecoverValid && !_timeElapsed(now, _lastRecoverMs + _config.recoverBackoffMs)) {
    return Status::Error(Err::BUSY, "Recover backoff active");
  }
  _lastRecoverMs = now;
  _lastRecoverValid = true;

  const bool startedOffline = _driverState == DriverState::OFFLINE;
  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  Status result = [this]() -> Status {
    if (_operatingMode == OperatingMode::POWER_DOWN) {
      return wakeUp();
    }

    if (isPeriodicActive()) {
      bool ready = false;
      Status st = readDataReadyStatus(ready);
      if (st.ok()) {
        return Status::Ok();
      }
    } else {
      uint64_t serial = 0;
      Status st = readSerialNumber(serial);
      if (st.ok()) {
        return Status::Ok();
      }
    }

    if (_config.busReset != nullptr) {
      Status st = _config.busReset(_config.controlUser);
      if (!st.ok()) {
        return st;
      }

      if (isPeriodicActive()) {
        bool ready = false;
        st = readDataReadyStatus(ready);
      } else {
        uint64_t serial = 0;
        st = readSerialNumber(serial);
      }
      if (st.ok()) {
        return Status::Ok();
      }
    }

    if (_config.powerCycle != nullptr) {
      Status st = _config.powerCycle(_config.controlUser);
      if (!st.ok()) {
        return st;
      }
      _operatingMode = OperatingMode::IDLE;
      _clearMeasurementRequest();
      return _schedulePendingCommand(PendingCommand::POWER_CYCLE, _config.powerUpDelayMs);
    }

    return Status::Error(Err::COMMAND_FAILED, "Recovery exhausted");
  }();
  if (startedOffline && !result.ok() && !result.inProgress()) {
    _reassertOfflineLatch();
  }
  return result;
}

Status SCD41::requestMeasurement() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_driverState == DriverState::OFFLINE) {
    return Status::Error(Err::BUSY, "Driver is offline; call recover()");
  }
  if (_pendingCommand != PendingCommand::NONE) {
    return Status::Error(Err::BUSY, "Command in progress");
  }
  if (_measurementRequested || _measurementReady) {
    return Status::Error(Err::BUSY, "Measurement already pending");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }

  if (isPeriodicActive()) {
    _measurementRequested = true;
    _measurementReady = false;
    _measurementReadyMs = _periodicReadyMs(_nowMs());
    return Status{Err::IN_PROGRESS, 0, "Periodic fetch scheduled"};
  }

  return _startSingleShot(_singleShotMode);
}

Status SCD41::readMeasurement(Measurement& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }
  if (_measurementReady) {
    return getMeasurement(out);
  }
  if (_pendingCommand != PendingCommand::NONE && _pendingCommand != PendingCommand::SINGLE_SHOT &&
      _pendingCommand != PendingCommand::SINGLE_SHOT_RHT_ONLY) {
    return Status::Error(Err::BUSY, "Command in progress");
  }

  const uint32_t nowMs = _nowMs();
  if (_pendingCommand == PendingCommand::SINGLE_SHOT ||
      _pendingCommand == PendingCommand::SINGLE_SHOT_RHT_ONLY) {
    if (!_timeElapsed(nowMs, _commandReadyMs)) {
      return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
    }

    Status st = _completeMeasurement();
    if (st.ok()) {
      _clearPendingCommand();
      return getMeasurement(out);
    }
    if (st.code != Err::MEASUREMENT_NOT_READY) {
      _clearPendingCommand();
      _clearMeasurementRequest();
    }
    return st;
  }

  if (_measurementRequested) {
    if (!_timeElapsed(nowMs, _measurementReadyMs)) {
      return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
    }

    Status st = _completeMeasurement();
    if (!st.ok()) {
      return st;
    }
    return getMeasurement(out);
  }

  if (!isPeriodicActive()) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No measurement pending");
  }

  bool ready = false;
  Status st = readDataReadyStatus(ready);
  if (!st.ok()) {
    return st;
  }
  if (!ready) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
  }

  RawSample sample = {};
  st = _readMeasurementRaw(sample, true, true);
  if (!st.ok()) {
    return st;
  }

  _storeSample(sample, true);
  return getMeasurement(out);
}

Status SCD41::getMeasurement(Measurement& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_measurementReady) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
  }

  out.co2Ppm = _rawSample.rawCo2;
  out.temperatureC = convertTemperatureC(_rawSample.rawTemperature);
  out.humidityPct = convertHumidityPct(_rawSample.rawHumidity);
  out.co2Valid = _lastSampleCo2Valid;
  _measurementReady = false;
  return Status::Ok();
}

Status SCD41::getLastMeasurement(Measurement& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_hasSample) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No sample available");
  }

  out.co2Ppm = _rawSample.rawCo2;
  out.temperatureC = convertTemperatureC(_rawSample.rawTemperature);
  out.humidityPct = convertHumidityPct(_rawSample.rawHumidity);
  out.co2Valid = _lastSampleCo2Valid;
  return Status::Ok();
}

Status SCD41::getRawSample(RawSample& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_hasSample) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No sample available");
  }
  out = _rawSample;
  return Status::Ok();
}

Status SCD41::getCompensatedSample(CompensatedSample& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_hasSample) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No sample available");
  }
  out = _compSample;
  return Status::Ok();
}

Status SCD41::readDataReadyStatus(bool& ready) {
  DataReadyStatus status;
  Status st = readDataReadyStatus(status);
  if (!st.ok()) {
    return st;
  }
  ready = status.ready;
  return Status::Ok();
}

Status SCD41::readDataReadyStatus(DataReadyStatus& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand != PendingCommand::NONE) {
    return Status::Error(Err::BUSY, "Command in progress");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }

  uint16_t raw = 0;
  Status st = _readWord(cmd::CMD_GET_DATA_READY_STATUS, raw, true);
  if (!st.ok()) {
    return st;
  }

  out.raw = raw;
  out.ready = isDataReady(raw);
  return Status::Ok();
}

Status SCD41::setSingleShotMode(SingleShotMode mode) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidSingleShotMode(mode)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid single-shot mode");
  }
  if (_pendingCommand != PendingCommand::NONE || _measurementRequested) {
    return Status::Error(Err::BUSY, "Operation in progress");
  }
  _singleShotMode = mode;
  return Status::Ok();
}

Status SCD41::getSingleShotMode(SingleShotMode& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  out = _singleShotMode;
  return Status::Ok();
}

Status SCD41::startSingleShotMeasurement() {
  return _startSingleShot(SingleShotMode::CO2_T_RH);
}

Status SCD41::startSingleShotRhtOnlyMeasurement() {
  return _startSingleShot(SingleShotMode::T_RH_ONLY);
}

Status SCD41::startPeriodicMeasurement() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand != PendingCommand::NONE || _measurementRequested) {
    return Status::Error(Err::BUSY, "Operation in progress");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }
  if (_operatingMode == OperatingMode::PERIODIC) {
    return Status::Ok();
  }
  if (_operatingMode == OperatingMode::LOW_POWER_PERIODIC) {
    return Status::Error(Err::BUSY, "Stop low-power periodic first");
  }

  Status st = _writeCommand(cmd::CMD_START_PERIODIC_MEASUREMENT, true);
  if (!st.ok()) {
    return st;
  }

  _operatingMode = OperatingMode::PERIODIC;
  _periodicStartMs = _nowMs();
  _lastFetchMs = 0;
  _measurementReady = false;
  return Status::Ok();
}

Status SCD41::startLowPowerPeriodicMeasurement() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand != PendingCommand::NONE || _measurementRequested) {
    return Status::Error(Err::BUSY, "Operation in progress");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }
  if (_operatingMode == OperatingMode::LOW_POWER_PERIODIC) {
    return Status::Ok();
  }
  if (_operatingMode == OperatingMode::PERIODIC) {
    return Status::Error(Err::BUSY, "Stop periodic first");
  }
  if (_sensorVariant != SensorVariant::SCD41) {
    return Status::Error(Err::UNSUPPORTED, "Low-power periodic requires SCD41");
  }

  Status st = _writeCommand(cmd::CMD_START_LOW_POWER_PERIODIC_MEASUREMENT, true);
  if (!st.ok()) {
    return st;
  }

  _operatingMode = OperatingMode::LOW_POWER_PERIODIC;
  _periodicStartMs = _nowMs();
  _lastFetchMs = 0;
  _measurementReady = false;
  return Status::Ok();
}

Status SCD41::stopPeriodicMeasurement() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isPeriodicActive()) {
    return Status::Error(Err::INVALID_PARAM, "Sensor is not in periodic mode");
  }
  if (_pendingCommand != PendingCommand::NONE) {
    return Status::Error(Err::BUSY, "Command in progress");
  }

  Status st = _writeCommand(cmd::CMD_STOP_PERIODIC_MEASUREMENT, true);
  if (!st.ok()) {
    return st;
  }

  _clearMeasurementRequest();
  return _schedulePendingCommand(PendingCommand::STOP_PERIODIC,
                                 cmd::EXECUTION_TIME_STOP_PERIODIC_MS);
}

Status SCD41::powerDown() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("power down");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommand(cmd::CMD_POWER_DOWN, true);
  if (!st.ok()) {
    return st;
  }
  return _schedulePendingCommand(PendingCommand::POWER_DOWN, cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::wakeUp() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand != PendingCommand::NONE) {
    return Status::Error(Err::BUSY, "Command in progress");
  }
  if (_operatingMode != OperatingMode::POWER_DOWN) {
    return Status::Error(Err::INVALID_PARAM, "Sensor is not powered down");
  }

  Status st = _writeCommand(cmd::CMD_WAKE_UP, true, true);
  if (!st.ok()) {
    return st;
  }
  return _schedulePendingCommand(PendingCommand::WAKE_UP, _config.powerUpDelayMs);
}

Status SCD41::readSerialNumber(uint64_t& serial) {
  if (!_initialized && (_config.i2cWrite == nullptr || _config.i2cWriteRead == nullptr)) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_initialized) {
    Status st = _ensureIdleForConfig("read serial number");
    if (!st.ok()) {
      return st;
    }
  }

  uint16_t words[3] = {};
  Status st = _readWords(cmd::CMD_GET_SERIAL_NUMBER, words, 3, true);
  if (!st.ok()) {
    return st;
  }

  _sensorVariant = _variantFromSerialWord(words[0]);
  _serialNumber = (static_cast<uint64_t>(words[0]) << 32) |
                  (static_cast<uint64_t>(words[1]) << 16) |
                  static_cast<uint64_t>(words[2]);
  _serialNumberValid = true;
  serial = _serialNumber;
  return Status::Ok();
}

Status SCD41::getIdentity(Identity& out) {
  if (!_serialNumberValid) {
    uint64_t serial = 0;
    Status st = readSerialNumber(serial);
    if (!st.ok()) {
      return st;
    }
  }

  out.serialNumber = _serialNumber;
  out.variant = _sensorVariant;
  return Status::Ok();
}

Status SCD41::readSensorVariant(SensorVariant& out) {
  if (!_initialized && !_serialNumberValid &&
      (_config.i2cWrite == nullptr || _config.i2cWriteRead == nullptr)) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_serialNumberValid) {
    uint64_t serial = 0;
    Status st = readSerialNumber(serial);
    if (!st.ok()) {
      return st;
    }
  }
  out = _sensorVariant;
  return Status::Ok();
}

Status SCD41::setTemperatureOffsetC(float offsetC) {
  if (!std::isfinite(offsetC)) {
    return Status::Error(Err::INVALID_PARAM, "Temperature offset must be finite");
  }
  const int32_t milli = static_cast<int32_t>(
      (offsetC * 1000.0f) + (offsetC >= 0.0f ? 0.5f : -0.5f));
  return setTemperatureOffsetC_x1000(milli);
}

Status SCD41::getTemperatureOffsetC(float& out) {
  int32_t milli = 0;
  Status st = getTemperatureOffsetC_x1000(milli);
  if (!st.ok()) {
    return st;
  }
  out = static_cast<float>(milli) / 1000.0f;
  return Status::Ok();
}

Status SCD41::setTemperatureOffsetC_x1000(int32_t offsetC_x1000) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (offsetC_x1000 < 0 || offsetC_x1000 > 20000) {
    return Status::Error(Err::INVALID_PARAM, "Temperature offset out of range");
  }
  Status st = _ensureIdleForConfig("set temperature offset");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommandWithData(cmd::CMD_SET_TEMPERATURE_OFFSET,
                             encodeTemperatureOffsetC_x1000(offsetC_x1000), true);
  if (!st.ok()) {
    return st;
  }
  return _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::getTemperatureOffsetC_x1000(int32_t& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("get temperature offset");
  if (!st.ok()) {
    return st;
  }

  uint16_t raw = 0;
  st = _readWord(cmd::CMD_GET_TEMPERATURE_OFFSET, raw, true);
  if (!st.ok()) {
    return st;
  }
  out = decodeTemperatureOffsetC_x1000(raw);
  return Status::Ok();
}

Status SCD41::setSensorAltitudeM(uint16_t altitudeM) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (altitudeM < cmd::ALTITUDE_MIN_M || altitudeM > cmd::ALTITUDE_MAX_M) {
    return Status::Error(Err::INVALID_PARAM, "Altitude out of range");
  }
  Status st = _ensureIdleForConfig("set altitude");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommandWithData(cmd::CMD_SET_SENSOR_ALTITUDE, altitudeM, true);
  if (!st.ok()) {
    return st;
  }
  return _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::getSensorAltitudeM(uint16_t& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("get altitude");
  if (!st.ok()) {
    return st;
  }

  uint16_t raw = 0;
  st = _readWord(cmd::CMD_GET_SENSOR_ALTITUDE, raw, true);
  if (!st.ok()) {
    return st;
  }
  out = raw;
  return Status::Ok();
}

Status SCD41::setAmbientPressurePa(uint32_t pressurePa) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (pressurePa < cmd::AMBIENT_PRESSURE_MIN_PA || pressurePa > cmd::AMBIENT_PRESSURE_MAX_PA) {
    return Status::Error(Err::INVALID_PARAM, "Ambient pressure out of range");
  }
  if (_pendingCommand != PendingCommand::NONE || _operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is busy");
  }

  const uint16_t raw = encodeAmbientPressurePa(pressurePa);
  Status st = _writeCommandWithData(cmd::CMD_SET_AMBIENT_PRESSURE, raw, true);
  if (!st.ok()) {
    return st;
  }
  return _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::getAmbientPressurePa(uint32_t& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand != PendingCommand::NONE || _operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is busy");
  }

  uint16_t raw = 0;
  Status st = _readWord(cmd::CMD_GET_AMBIENT_PRESSURE, raw, true);
  if (!st.ok()) {
    return st;
  }
  out = decodeAmbientPressurePa(raw);
  return Status::Ok();
}

Status SCD41::setAutomaticSelfCalibrationEnabled(bool enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("set ASC enabled");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommandWithData(cmd::CMD_SET_ASC_ENABLED, enabled ? 1U : 0U, true);
  if (!st.ok()) {
    return st;
  }
  return _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::getAutomaticSelfCalibrationEnabled(bool& enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("get ASC enabled");
  if (!st.ok()) {
    return st;
  }

  uint16_t raw = 0;
  st = _readWord(cmd::CMD_GET_ASC_ENABLED, raw, true);
  if (!st.ok()) {
    return st;
  }
  enabled = raw != 0;
  return Status::Ok();
}

Status SCD41::setAutomaticSelfCalibrationTargetPpm(uint16_t ppm) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_sensorVariant != SensorVariant::SCD41) {
    return Status::Error(Err::UNSUPPORTED, "ASC target requires SCD41");
  }
  if (ppm < 1 || ppm > cmd::CO2_MAX_PPM) {
    return Status::Error(Err::INVALID_PARAM, "ASC target out of range");
  }
  Status st = _ensureIdleForConfig("set ASC target");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommandWithData(cmd::CMD_SET_ASC_TARGET, ppm, true);
  if (!st.ok()) {
    return st;
  }
  return _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::getAutomaticSelfCalibrationTargetPpm(uint16_t& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_sensorVariant != SensorVariant::SCD41) {
    return Status::Error(Err::UNSUPPORTED, "ASC target requires SCD41");
  }
  Status st = _ensureIdleForConfig("get ASC target");
  if (!st.ok()) {
    return st;
  }

  return _readWord(cmd::CMD_GET_ASC_TARGET, out, true);
}

Status SCD41::setAutomaticSelfCalibrationInitialPeriodHours(uint16_t hours) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (hours != 0 && (hours % cmd::ASC_PERIOD_STEP_HOURS) != 0) {
    return Status::Error(Err::INVALID_PARAM, "Initial ASC period must be multiple of 4 h");
  }
  Status st = _ensureIdleForConfig("set ASC initial period");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommandWithData(cmd::CMD_SET_ASC_INITIAL_PERIOD, hours, true);
  if (!st.ok()) {
    return st;
  }
  return _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::getAutomaticSelfCalibrationInitialPeriodHours(uint16_t& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("get ASC initial period");
  if (!st.ok()) {
    return st;
  }

  return _readWord(cmd::CMD_GET_ASC_INITIAL_PERIOD, out, true);
}

Status SCD41::setAutomaticSelfCalibrationStandardPeriodHours(uint16_t hours) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (hours == 0 || (hours % cmd::ASC_PERIOD_STEP_HOURS) != 0) {
    return Status::Error(Err::INVALID_PARAM, "Standard ASC period must be positive multiple of 4 h");
  }
  Status st = _ensureIdleForConfig("set ASC standard period");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommandWithData(cmd::CMD_SET_ASC_STANDARD_PERIOD, hours, true);
  if (!st.ok()) {
    return st;
  }
  return _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
}

Status SCD41::getAutomaticSelfCalibrationStandardPeriodHours(uint16_t& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("get ASC standard period");
  if (!st.ok()) {
    return st;
  }

  return _readWord(cmd::CMD_GET_ASC_STANDARD_PERIOD, out, true);
}

Status SCD41::startPersistSettings() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("persist settings");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommand(cmd::CMD_PERSIST_SETTINGS, true);
  if (!st.ok()) {
    return st;
  }
  return _schedulePendingCommand(PendingCommand::PERSIST_SETTINGS,
                                 cmd::EXECUTION_TIME_PERSIST_MS);
}

Status SCD41::startReinit() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("reinit");
  if (!st.ok()) {
    return st;
  }

  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  st = _writeCommand(cmd::CMD_REINIT, true);
  if (!st.ok()) {
    return st;
  }
  return _schedulePendingCommand(PendingCommand::REINIT, cmd::EXECUTION_TIME_REINIT_MS);
}

Status SCD41::startFactoryReset() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("factory reset");
  if (!st.ok()) {
    return st;
  }

  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  st = _writeCommand(cmd::CMD_PERFORM_FACTORY_RESET, true);
  if (!st.ok()) {
    return st;
  }
  return _schedulePendingCommand(PendingCommand::FACTORY_RESET,
                                 cmd::EXECUTION_TIME_FACTORY_RESET_MS);
}

Status SCD41::startSelfTest() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _ensureIdleForConfig("self test");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommand(cmd::CMD_PERFORM_SELF_TEST, true);
  if (!st.ok()) {
    return st;
  }

  _selfTestCompleted = false;
  _selfTestRaw = 0;
  _selfTestRawValid = false;
  _selfTestStatus = Status::Error(Err::IN_PROGRESS, "Self-test running");
  return _schedulePendingCommand(PendingCommand::SELF_TEST, cmd::EXECUTION_TIME_SELF_TEST_MS);
}

Status SCD41::getSelfTestResult(uint16_t& rawResult) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand == PendingCommand::SELF_TEST) {
    return Status::Error(Err::BUSY, "Self-test still running");
  }
  if (!_selfTestCompleted) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Self-test result not available");
  }
  if (!_selfTestStatus.ok()) {
    return _selfTestStatus;
  }
  rawResult = _selfTestRaw;
  return Status::Ok();
}

Status SCD41::getSelfTestRawResult(uint16_t& rawResult) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand == PendingCommand::SELF_TEST) {
    return Status::Error(Err::BUSY, "Self-test still running");
  }
  if (!_selfTestCompleted) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Self-test result not available");
  }
  if (!_selfTestRawValid) {
    return _selfTestStatus;
  }
  rawResult = _selfTestRaw;
  return Status::Ok();
}

Status SCD41::startForcedRecalibration(uint16_t referencePpm) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (referencePpm == 0 || referencePpm > cmd::CO2_MAX_PPM) {
    return Status::Error(Err::INVALID_PARAM, "Reference CO2 out of range");
  }
  Status st = _ensureIdleForConfig("forced recalibration");
  if (!st.ok()) {
    return st;
  }

  st = _writeCommandWithData(cmd::CMD_PERFORM_FORCED_RECALIBRATION, referencePpm, true);
  if (!st.ok()) {
    return st;
  }

  _forcedRecalibrationCompleted = false;
  _forcedRecalibrationStatus = Status::Error(Err::IN_PROGRESS, "Forced recalibration running");
  _forcedRecalibrationRaw = 0;
  _forcedRecalibrationRawValid = false;
  return _schedulePendingCommand(PendingCommand::FORCED_RECALIBRATION,
                                 cmd::EXECUTION_TIME_FRC_MS);
}

Status SCD41::getForcedRecalibrationCorrectionPpm(int16_t& correctionPpm) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand == PendingCommand::FORCED_RECALIBRATION) {
    return Status::Error(Err::BUSY, "Forced recalibration still running");
  }
  if (!_forcedRecalibrationCompleted) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Forced recalibration result not available");
  }
  if (!_forcedRecalibrationStatus.ok()) {
    return _forcedRecalibrationStatus;
  }
  correctionPpm = _forcedRecalibrationCorrectionPpm;
  return Status::Ok();
}

Status SCD41::getForcedRecalibrationRawResult(uint16_t& rawResult) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_pendingCommand == PendingCommand::FORCED_RECALIBRATION) {
    return Status::Error(Err::BUSY, "Forced recalibration still running");
  }
  if (!_forcedRecalibrationCompleted) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Forced recalibration result not available");
  }
  if (!_forcedRecalibrationRawValid) {
    return _forcedRecalibrationStatus;
  }
  rawResult = _forcedRecalibrationRaw;
  return Status::Ok();
}

Status SCD41::getSettings(SettingsSnapshot& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  out.initialized = _initialized;
  out.state = _driverState;
  out.operatingMode = _operatingMode;
  out.singleShotMode = _singleShotMode;
  out.pendingCommand = _pendingCommand;
  out.busy = (_pendingCommand != PendingCommand::NONE);
  out.commandReadyMs = _commandReadyMs;
  out.i2cAddress = _config.i2cAddress;
  out.i2cTimeoutMs = _config.i2cTimeoutMs;
  out.offlineThreshold = _config.offlineThreshold;
  out.measurementPending = _measurementRequested;
  out.measurementReady = _measurementReady;
  out.hasSample = _hasSample;
  out.measurementReadyMs = _measurementReadyMs;
  out.sampleTimestampMs = _sampleTimestampMs;
  out.missedSamples = _missedSamples;
  out.lastSampleCo2Valid = _lastSampleCo2Valid;
  out.sensorVariant = _sensorVariant;
  out.serialNumberValid = _serialNumberValid;
  out.serialNumber = _serialNumber;
  out.liveConfigValid = false;
  out.temperatureOffsetC_x1000 = 0;
  out.sensorAltitudeM = 0;
  out.ambientPressurePa = 0;
  out.automaticSelfCalibrationEnabled = false;
  out.automaticSelfCalibrationTargetPpm = 0;
  out.automaticSelfCalibrationInitialPeriodHours = 0;
  out.automaticSelfCalibrationStandardPeriodHours = 0;
  return Status::Ok();
}

Status SCD41::readSettings(SettingsSnapshot& out) {
  Status st = getSettings(out);
  if (!st.ok()) {
    return st;
  }

  if (_pendingCommand != PendingCommand::NONE || _measurementRequested ||
      _operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Ok();
  }

  if (isPeriodicActive()) {
    return getAmbientPressurePa(out.ambientPressurePa);
  }

  st = getTemperatureOffsetC_x1000(out.temperatureOffsetC_x1000);
  if (!st.ok()) {
    return st;
  }

  st = getSensorAltitudeM(out.sensorAltitudeM);
  if (!st.ok()) {
    return st;
  }

  st = getAmbientPressurePa(out.ambientPressurePa);
  if (!st.ok()) {
    return st;
  }

  st = getAutomaticSelfCalibrationEnabled(out.automaticSelfCalibrationEnabled);
  if (!st.ok()) {
    return st;
  }

  st = getAutomaticSelfCalibrationTargetPpm(out.automaticSelfCalibrationTargetPpm);
  if (!st.ok() && !st.is(Err::UNSUPPORTED)) {
    return st;
  }

  st = getAutomaticSelfCalibrationInitialPeriodHours(
      out.automaticSelfCalibrationInitialPeriodHours);
  if (!st.ok()) {
    return st;
  }

  st = getAutomaticSelfCalibrationStandardPeriodHours(
      out.automaticSelfCalibrationStandardPeriodHours);
  if (!st.ok()) {
    return st;
  }

  out.liveConfigValid = true;
  return Status::Ok();
}

Status SCD41::writeCommand(uint16_t command, bool allowExpectedNack) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _validateRawCommand(command);
  if (!st.ok()) {
    return st;
  }
  return _writeCommand(command, true, allowExpectedNack);
}

Status SCD41::writeCommandWithData(uint16_t command, uint16_t data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _validateRawCommand(command);
  if (!st.ok()) {
    return st;
  }
  return _writeCommandWithData(command, data, true);
}

Status SCD41::readCommand(uint16_t command, uint8_t* out, size_t len, bool allowNoData) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (out == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid output buffer");
  }
  if (len > cmd::MEASUREMENT_RESPONSE_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Read length too large");
  }
  Status st = _validateRawCommand(command);
  if (!st.ok()) {
    return st;
  }

  st = _writeCommand(command, true);
  if (!st.ok()) {
    return st;
  }

  st = _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
  if (!st.ok()) {
    return st;
  }

  return _readOnly(out, len, true, allowNoData);
}

Status SCD41::readWordCommand(uint16_t command, uint16_t& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _validateRawCommand(command);
  if (!st.ok()) {
    return st;
  }
  return _readWord(command, out, true);
}

Status SCD41::readWordsCommand(uint16_t command, uint16_t* out, size_t count) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status st = _validateRawCommand(command);
  if (!st.ok()) {
    return st;
  }
  return _readWords(command, out, count, true);
}

float SCD41::convertTemperatureC(uint16_t raw) {
  return -45.0f + (175.0f * static_cast<float>(raw) / 65535.0f);
}

float SCD41::convertHumidityPct(uint16_t raw) {
  return 100.0f * static_cast<float>(raw) / 65535.0f;
}

int32_t SCD41::convertTemperatureC_x1000(uint16_t raw) {
  return ((21875 * static_cast<int32_t>(raw)) >> 13) - 45000;
}

uint32_t SCD41::convertHumidityPct_x1000(uint16_t raw) {
  return static_cast<uint32_t>((12500U * static_cast<uint32_t>(raw)) >> 13);
}

uint16_t SCD41::encodeTemperatureOffsetC(float offsetC) {
  if (!std::isfinite(offsetC)) {
    return 0;
  }
  const int32_t milli = static_cast<int32_t>(
      (offsetC * 1000.0f) + (offsetC >= 0.0f ? 0.5f : -0.5f));
  return encodeTemperatureOffsetC_x1000(milli);
}

uint16_t SCD41::encodeTemperatureOffsetC_x1000(int32_t offsetC_x1000) {
  if (offsetC_x1000 <= 0) {
    return 0;
  }
  const uint64_t numerator = static_cast<uint64_t>(offsetC_x1000) * 65535ULL + 87500ULL;
  const uint64_t encoded = numerator / 175000ULL;
  return static_cast<uint16_t>(
      encoded > std::numeric_limits<uint16_t>::max()
          ? std::numeric_limits<uint16_t>::max()
          : encoded);
}

float SCD41::decodeTemperatureOffsetC(uint16_t raw) {
  return static_cast<float>(decodeTemperatureOffsetC_x1000(raw)) / 1000.0f;
}

int32_t SCD41::decodeTemperatureOffsetC_x1000(uint16_t raw) {
  const uint32_t numerator = static_cast<uint32_t>(raw) * 175000U + 32767U;
  return static_cast<int32_t>(numerator / 65535U);
}

uint16_t SCD41::encodeAmbientPressurePa(uint32_t pressurePa) {
  return static_cast<uint16_t>(pressurePa / 100U);
}

uint32_t SCD41::decodeAmbientPressurePa(uint16_t raw) {
  return static_cast<uint32_t>(raw) * 100U;
}

Status SCD41::_validateRawCommand(uint16_t command) const {
  if (_pendingCommand != PendingCommand::NONE || _measurementRequested) {
    return Status::Error(Err::BUSY, "Operation in progress");
  }
  if (_isManagedOnlyRawCommand(command)) {
    return Status::Error(Err::UNSUPPORTED, "Use typed API for managed command");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }
  if (isPeriodicActive() && !_isPeriodicAllowedCommand(command)) {
    return Status::Error(Err::BUSY, "Command not allowed during periodic measurement");
  }
  return Status::Ok();
}

bool SCD41::_isPeriodicAllowedCommand(uint16_t command) {
  switch (command) {
    case cmd::CMD_READ_MEASUREMENT:
    case cmd::CMD_GET_DATA_READY_STATUS:
    case cmd::CMD_SET_AMBIENT_PRESSURE:
      return true;
    default:
      return false;
  }
}

bool SCD41::_isManagedOnlyRawCommand(uint16_t command) {
  switch (command) {
    case cmd::CMD_START_PERIODIC_MEASUREMENT:
    case cmd::CMD_STOP_PERIODIC_MEASUREMENT:
    case cmd::CMD_START_LOW_POWER_PERIODIC_MEASUREMENT:
    case cmd::CMD_MEASURE_SINGLE_SHOT:
    case cmd::CMD_MEASURE_SINGLE_SHOT_RHT_ONLY:
    case cmd::CMD_PERFORM_FORCED_RECALIBRATION:
    case cmd::CMD_PERSIST_SETTINGS:
    case cmd::CMD_PERFORM_SELF_TEST:
    case cmd::CMD_PERFORM_FACTORY_RESET:
    case cmd::CMD_REINIT:
    case cmd::CMD_POWER_DOWN:
    case cmd::CMD_WAKE_UP:
      return true;
    default:
      return false;
  }
}

Status SCD41::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf, size_t rxLen) {
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C read callback not set");
  }
  if ((txLen > 0 && txBuf == nullptr) || rxBuf == nullptr || rxLen == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C read buffers");
  }
  return _config.i2cWriteRead(_config.i2cAddress, txBuf, txLen, rxBuf, rxLen,
                              _config.i2cTimeoutMs, _config.i2cUser);
}

Status SCD41::_i2cWriteRaw(const uint8_t* buf, size_t len) {
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write callback not set");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C write buffer");
  }
  return _config.i2cWrite(_config.i2cAddress, buf, len, _config.i2cTimeoutMs, _config.i2cUser);
}

Status SCD41::_i2cWriteTracked(const uint8_t* buf, size_t len) {
  Status allowed = _ensureNormalI2cAllowed();
  if (!allowed.ok()) {
    return allowed;
  }
  return _updateHealth(_i2cWriteRaw(buf, len));
}

Status SCD41::_i2cWriteTrackedAllowExpectedNack(const uint8_t* buf, size_t len) {
  Status allowed = _ensureNormalI2cAllowed();
  if (!allowed.ok()) {
    return allowed;
  }
  Status st = _i2cWriteRaw(buf, len);
  if (st.ok()) {
    return _updateHealth(st);
  }
  if (st.code == Err::I2C_NACK_ADDR || st.code == Err::I2C_NACK_DATA ||
      st.code == Err::I2C_ERROR) {
    return _updateHealth(Status::Ok());
  }
  return _updateHealth(st);
}

Status SCD41::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf,
                                   size_t rxLen) {
  Status allowed = _ensureNormalI2cAllowed();
  if (!allowed.ok()) {
    return allowed;
  }
  return _updateHealth(_i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen));
}

Status SCD41::_i2cWriteReadTrackedAllowNoData(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf,
                                              size_t rxLen, bool allowNoData) {
  Status allowed = _ensureNormalI2cAllowed();
  if (!allowed.ok()) {
    return allowed;
  }
  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (allowNoData && st.code == Err::I2C_NACK_READ &&
      hasCapability(_config.transportCapabilities, TransportCapability::READ_HEADER_NACK)) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No data available");
  }
  return _updateHealth(st);
}

Status SCD41::_writeCommand(uint16_t cmd, bool tracked, bool allowExpectedNack) {
  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  const uint8_t buf[2] = {
      static_cast<uint8_t>((cmd >> 8) & 0xFF),
      static_cast<uint8_t>(cmd & 0xFF),
  };

  if (tracked) {
    st = allowExpectedNack ? _i2cWriteTrackedAllowExpectedNack(buf, sizeof(buf))
                           : _i2cWriteTracked(buf, sizeof(buf));
  } else {
    st = _i2cWriteRaw(buf, sizeof(buf));
  }
  if (!st.ok()) {
    return st;
  }

  _lastCommandUs = _nowUs();
  _lastCommandValid = true;
  return Status::Ok();
}

Status SCD41::_writeCommandWithData(uint16_t cmd, uint16_t data, bool tracked) {
  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[MAX_WRITE_LEN] = {
      static_cast<uint8_t>((cmd >> 8) & 0xFF),
      static_cast<uint8_t>(cmd & 0xFF),
      static_cast<uint8_t>((data >> 8) & 0xFF),
      static_cast<uint8_t>(data & 0xFF),
      0,
  };
  buf[4] = _crc8(&buf[2], 2);

  st = tracked ? _i2cWriteTracked(buf, sizeof(buf)) : _i2cWriteRaw(buf, sizeof(buf));
  if (!st.ok()) {
    return st;
  }

  _lastCommandUs = _nowUs();
  _lastCommandValid = true;
  return Status::Ok();
}

Status SCD41::_readCommand(uint16_t cmd, uint8_t* out, size_t len, bool tracked) {
  Status st = _writeCommand(cmd, tracked);
  if (!st.ok()) {
    return st;
  }

  st = _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
  if (!st.ok()) {
    return st;
  }

  return _readOnly(out, len, tracked);
}

Status SCD41::_readOnly(uint8_t* out, size_t len, bool tracked, bool allowNoData) {
  if (out == nullptr || len == 0 || len > cmd::MEASUREMENT_RESPONSE_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }

  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  if (tracked) {
    st = _i2cWriteReadTrackedAllowNoData(nullptr, 0, out, len, allowNoData);
  } else {
    st = _i2cWriteReadRaw(nullptr, 0, out, len);
  }
  if (!st.ok()) {
    return st;
  }

  _lastCommandUs = _nowUs();
  _lastCommandValid = true;
  return Status::Ok();
}

Status SCD41::_readWord(uint16_t cmd, uint16_t& value, bool tracked) {
  return _readWords(cmd, &value, 1, tracked);
}

Status SCD41::_readWords(uint16_t cmd, uint16_t* values, size_t count, bool tracked) {
  if (values == nullptr || count == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid output buffer");
  }
  if (count > (cmd::MEASUREMENT_RESPONSE_LEN / cmd::DATA_WORD_WITH_CRC)) {
    return Status::Error(Err::INVALID_PARAM, "Read length too large");
  }

  const size_t len = count * cmd::DATA_WORD_WITH_CRC;
  uint8_t buf[cmd::MEASUREMENT_RESPONSE_LEN] = {};
  if (len > sizeof(buf)) {
    return Status::Error(Err::INVALID_PARAM, "Read length too large");
  }

  Status st = _readCommand(cmd, buf, len, tracked);
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < count; ++i) {
    const size_t offset = i * cmd::DATA_WORD_WITH_CRC;
    const uint8_t expected = _crc8(&buf[offset], 2);
    if (buf[offset + 2] != expected) {
      return Status::Error(Err::CRC_MISMATCH, "CRC mismatch");
    }
    values[i] = static_cast<uint16_t>((static_cast<uint16_t>(buf[offset]) << 8) |
                                      static_cast<uint16_t>(buf[offset + 1]));
  }

  return Status::Ok();
}

Status SCD41::_readWordsOnly(uint16_t* values, size_t count, bool tracked, bool allowNoData) {
  if (values == nullptr || count == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid output buffer");
  }
  if (count > (cmd::MEASUREMENT_RESPONSE_LEN / cmd::DATA_WORD_WITH_CRC)) {
    return Status::Error(Err::INVALID_PARAM, "Read length too large");
  }

  const size_t len = count * cmd::DATA_WORD_WITH_CRC;
  uint8_t buf[cmd::MEASUREMENT_RESPONSE_LEN] = {};
  if (len > sizeof(buf)) {
    return Status::Error(Err::INVALID_PARAM, "Read length too large");
  }

  Status st = _readOnly(buf, len, tracked, allowNoData);
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < count; ++i) {
    const size_t offset = i * cmd::DATA_WORD_WITH_CRC;
    const uint8_t expected = _crc8(&buf[offset], 2);
    if (buf[offset + 2] != expected) {
      return Status::Error(Err::CRC_MISMATCH, "CRC mismatch");
    }
    values[i] = static_cast<uint16_t>((static_cast<uint16_t>(buf[offset]) << 8) |
                                      static_cast<uint16_t>(buf[offset + 1]));
  }

  return Status::Ok();
}

Status SCD41::_readMeasurementRaw(RawSample& out, bool tracked, bool allowNoData) {
  uint16_t words[3] = {};
  Status st = _writeCommand(cmd::CMD_READ_MEASUREMENT, tracked);
  if (!st.ok()) {
    return st;
  }

  st = _waitMs(cmd::EXECUTION_TIME_SHORT_MS);
  if (!st.ok()) {
    return st;
  }

  st = _readWordsOnly(words, 3, tracked, allowNoData);
  if (!st.ok()) {
    return st;
  }

  out.rawCo2 = words[0];
  out.rawTemperature = words[1];
  out.rawHumidity = words[2];
  return Status::Ok();
}

Status SCD41::_updateHealth(const Status& st) {
  if (!_initialized || st.inProgress()) {
    return st;
  }

  const uint32_t now = _nowMs();

  if (st.ok()) {
    _lastOkMs = now;
    if (_totalSuccess != std::numeric_limits<uint32_t>::max()) {
      ++_totalSuccess;
    }
    _consecutiveFailures = 0;
    _lastError = Status::Ok();
    _driverState = DriverState::READY;
    return st;
  }

  if (_isI2cFailure(st.code)) {
    _lastErrorMs = now;
    _lastError = st;
    if (_totalFailures != std::numeric_limits<uint32_t>::max()) {
      ++_totalFailures;
    }
    if (_consecutiveFailures != std::numeric_limits<uint8_t>::max()) {
      ++_consecutiveFailures;
    }
    _driverState = (_consecutiveFailures >= _config.offlineThreshold)
                       ? DriverState::OFFLINE
                       : DriverState::DEGRADED;
  }

  return st;
}

void SCD41::_reassertOfflineLatch() {
  _driverState = DriverState::OFFLINE;
  const uint8_t threshold = _config.offlineThreshold == 0 ? 1 : _config.offlineThreshold;
  if (_consecutiveFailures < threshold) {
    _consecutiveFailures = threshold;
  }
}

Status SCD41::_ensureNormalI2cAllowed() const {
  if (_initialized && _driverState == DriverState::OFFLINE && !_allowOfflineI2c) {
    return Status::Error(Err::BUSY, "Driver is offline; call recover()");
  }
  return Status::Ok();
}

Status SCD41::_ensureCommandDelay() {
  if (!_lastCommandValid) {
    return Status::Ok();
  }

  const uint32_t minDelayUs = static_cast<uint32_t>(_config.commandDelayMs) * 1000U;
  if (minDelayUs <= MIN_COMMAND_DELAY_US) {
    const uint32_t nowUs = _nowUs();
    if ((nowUs - _lastCommandUs) >= MIN_COMMAND_DELAY_US) {
      return Status::Ok();
    }
  }

  const uint32_t startMs = _nowMs();
  uint32_t iterations = 0;
  const uint32_t maxIterations = boundedWaitIterations(_config.commandDelayMs,
                                                       _config.i2cTimeoutMs);
  while ((_nowUs() - _lastCommandUs) < minDelayUs) {
    if (_timeElapsed(_nowMs(), startMs + _config.commandDelayMs + _config.i2cTimeoutMs)) {
      return Status::Error(Err::TIMEOUT, "Command delay guard timed out");
    }
    if (iterations++ >= maxIterations) {
      return Status::Error(Err::TIMEOUT, "Command delay guard stalled");
    }
    _cooperativeYield();
  }
  return Status::Ok();
}

Status SCD41::_waitMs(uint32_t delayMs) {
  if (delayMs == 0) {
    return Status::Ok();
  }
  const uint32_t start = _nowMs();
  uint32_t iterations = 0;
  const uint32_t maxIterations = boundedWaitIterations(delayMs, _config.i2cTimeoutMs);
  while (!_timeElapsed(_nowMs(), start + delayMs)) {
    if (_timeElapsed(_nowMs(), start + delayMs + _config.i2cTimeoutMs)) {
      return Status::Error(Err::TIMEOUT, "Delay timed out");
    }
    if (iterations++ >= maxIterations) {
      return Status::Error(Err::TIMEOUT, "Delay stalled");
    }
    _cooperativeYield();
  }
  return Status::Ok();
}

Status SCD41::_ensureIdleForConfig(const char* opName) const {
  if (_pendingCommand != PendingCommand::NONE || _measurementRequested) {
    return Status::Error(Err::BUSY, "Operation in progress");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }
  if (isPeriodicActive()) {
    return Status::Error(Err::BUSY, opName);
  }
  return Status::Ok();
}

Status SCD41::_startSingleShot(SingleShotMode mode) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidSingleShotMode(mode)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid single-shot mode");
  }
  if (_pendingCommand != PendingCommand::NONE) {
    return Status::Error(Err::BUSY, "Command in progress");
  }
  if (_measurementRequested || _measurementReady) {
    return Status::Error(Err::BUSY, "Measurement already pending");
  }
  if (_operatingMode == OperatingMode::POWER_DOWN) {
    return Status::Error(Err::BUSY, "Sensor is powered down");
  }
  if (_sensorVariant != SensorVariant::SCD41) {
    return Status::Error(Err::UNSUPPORTED, "Single-shot commands require SCD41");
  }

  const uint16_t command =
      (mode == SingleShotMode::T_RH_ONLY) ? cmd::CMD_MEASURE_SINGLE_SHOT_RHT_ONLY
                                          : cmd::CMD_MEASURE_SINGLE_SHOT;
  const uint32_t execMs =
      (mode == SingleShotMode::T_RH_ONLY) ? cmd::EXECUTION_TIME_SINGLE_SHOT_RHT_MS
                                          : cmd::EXECUTION_TIME_SINGLE_SHOT_MS;
  const PendingCommand pending =
      (mode == SingleShotMode::T_RH_ONLY) ? PendingCommand::SINGLE_SHOT_RHT_ONLY
                                          : PendingCommand::SINGLE_SHOT;

  Status st = _writeCommand(command, true);
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = true;
  _measurementReady = false;
  _measurementReadyMs = _nowMs() + execMs;
  return _schedulePendingCommand(pending, execMs);
}

Status SCD41::_schedulePendingCommand(PendingCommand command, uint32_t delayMs) {
  _pendingCommand = command;
  _commandReadyMs = _nowMs() + delayMs;
  return Status{Err::IN_PROGRESS, 0, "Command scheduled"};
}

void SCD41::_clearMeasurementRequest() {
  _measurementRequested = false;
  _measurementReadyMs = 0;
}

void SCD41::_clearPendingCommand() {
  _pendingCommand = PendingCommand::NONE;
  _commandReadyMs = 0;
}

void SCD41::_setBusyError(Status& st) const {
  st = Status::Error(Err::BUSY, "Command in progress",
                     static_cast<int32_t>(static_cast<uint8_t>(_pendingCommand)));
}

void SCD41::_updatePeriodicMissedSamples(uint32_t nowMs) {
  const uint32_t periodMs = _periodMsForMode();
  if (_lastFetchMs == 0 || periodMs == 0) {
    return;
  }
  const uint32_t elapsed = nowMs - _lastFetchMs;
  if (elapsed > periodMs) {
    uint32_t missed = elapsed / periodMs;
    if (missed > 0) {
      missed -= 1;
      _missedSamples = saturatingAddU32(_missedSamples, missed);
    }
  }
}

void SCD41::_storeSample(const RawSample& sample, bool co2Valid) {
  const uint32_t now = _nowMs();
  if (isPeriodicActive()) {
    _updatePeriodicMissedSamples(now);
    _lastFetchMs = now;
  }

  _rawSample = sample;
  _compSample.co2Ppm = sample.rawCo2;
  _compSample.tempC_x1000 = convertTemperatureC_x1000(sample.rawTemperature);
  _compSample.humidityPct_x1000 = convertHumidityPct_x1000(sample.rawHumidity);
  _lastSampleCo2Valid = co2Valid;
  _compSample.co2Valid = co2Valid;
  _sampleTimestampMs = now;
  _hasSample = true;
  _measurementReady = true;
  _clearMeasurementRequest();
}

Status SCD41::_handlePendingCommand(uint32_t nowMs) {
  switch (_pendingCommand) {
    case PendingCommand::NONE:
      return Status::Ok();

    case PendingCommand::STOP_PERIODIC:
      _operatingMode = OperatingMode::IDLE;
      _clearMeasurementRequest();
      _measurementReady = false;
      _clearPendingCommand();
      return Status::Ok();

    case PendingCommand::SINGLE_SHOT:
    case PendingCommand::SINGLE_SHOT_RHT_ONLY: {
      Status st = _completeMeasurement();
      if (st.ok()) {
        _clearPendingCommand();
      } else if (st.code != Err::MEASUREMENT_NOT_READY) {
        _clearPendingCommand();
        _clearMeasurementRequest();
      } else {
        _commandReadyMs = nowMs + _config.dataReadyRetryMs;
      }
      return st;
    }

    case PendingCommand::POWER_DOWN:
      _operatingMode = OperatingMode::POWER_DOWN;
      _clearMeasurementRequest();
      _measurementReady = false;
      _clearPendingCommand();
      return Status::Ok();

    case PendingCommand::WAKE_UP:
    case PendingCommand::REINIT:
    case PendingCommand::PERSIST_SETTINGS:
    case PendingCommand::FACTORY_RESET:
    case PendingCommand::POWER_CYCLE:
      _operatingMode = OperatingMode::IDLE;
      _clearPendingCommand();
      return Status::Ok();

    case PendingCommand::SELF_TEST: {
      Status st = _completeSelfTest();
      _clearPendingCommand();
      return st;
    }

    case PendingCommand::FORCED_RECALIBRATION: {
      Status st = _completeForcedRecalibration();
      _clearPendingCommand();
      return st;
    }
  }

  return Status::Error(Err::COMMAND_FAILED, "Unknown pending command");
}

Status SCD41::_completeSelfTest() {
  uint16_t value = 0;
  Status st = _readWordsOnly(&value, 1, true, false);
  _selfTestCompleted = true;
  if (!st.ok()) {
    _selfTestStatus = st;
    return st;
  }

  _selfTestRaw = value;
  _selfTestRawValid = true;
  _selfTestStatus = (value == cmd::SELF_TEST_PASS)
                        ? Status::Ok()
                        : Status::Error(Err::COMMAND_FAILED, "Self-test failed", value);
  return _selfTestStatus.ok() ? Status::Ok() : _selfTestStatus;
}

Status SCD41::_completeForcedRecalibration() {
  uint16_t value = 0;
  Status st = _readWordsOnly(&value, 1, true, false);
  _forcedRecalibrationCompleted = true;
  if (!st.ok()) {
    _forcedRecalibrationStatus = st;
    return st;
  }

  _forcedRecalibrationRaw = value;
  _forcedRecalibrationRawValid = true;
  if (value == cmd::FRC_FAILED) {
    _forcedRecalibrationStatus = Status::Error(Err::COMMAND_FAILED, "Forced recalibration failed");
    return _forcedRecalibrationStatus;
  }

  _forcedRecalibrationCorrectionPpm =
      static_cast<int16_t>(static_cast<int32_t>(value) - cmd::FRC_OFFSET_BIAS);
  _forcedRecalibrationStatus = Status::Ok();
  return Status::Ok();
}

Status SCD41::_completeMeasurement() {
  uint16_t readyWord = 0;
  Status st = _readWord(cmd::CMD_GET_DATA_READY_STATUS, readyWord, true);
  if (!st.ok()) {
    return st;
  }
  if (!isDataReady(readyWord)) {
    _measurementReadyMs = _nowMs() + _config.dataReadyRetryMs;
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
  }

  RawSample sample;
  st = _readMeasurementRaw(sample, true, true);
  if (!st.ok()) {
    if (st.code == Err::MEASUREMENT_NOT_READY) {
      _measurementReadyMs = _nowMs() + _config.dataReadyRetryMs;
    }
    return st;
  }

  _storeSample(sample, _pendingCommand != PendingCommand::SINGLE_SHOT_RHT_ONLY);
  return Status::Ok();
}

bool SCD41::_timeElapsed(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

bool SCD41::_isI2cFailure(Err code) {
  return code == Err::I2C_ERROR || code == Err::I2C_NACK_ADDR ||
         code == Err::I2C_NACK_DATA || code == Err::I2C_NACK_READ ||
         code == Err::I2C_TIMEOUT || code == Err::I2C_BUS;
}

uint8_t SCD41::_crc8(const uint8_t* data, size_t len) {
  uint8_t crc = cmd::CRC_INIT;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x80U) != 0U) {
        crc = static_cast<uint8_t>((crc << 1U) ^ cmd::CRC_POLY);
      } else {
        crc <<= 1U;
      }
    }
  }
  return crc;
}

SensorVariant SCD41::_variantFromSerialWord(uint16_t word0) {
  const uint8_t variant =
      static_cast<uint8_t>((word0 & cmd::SERIAL_VARIANT_MASK) >> cmd::SERIAL_VARIANT_SHIFT);
  switch (variant) {
    case cmd::SERIAL_VARIANT_SCD40: return SensorVariant::SCD40;
    case cmd::SERIAL_VARIANT_SCD41: return SensorVariant::SCD41;
    case cmd::SERIAL_VARIANT_SCD42: return SensorVariant::SCD42;
    case cmd::SERIAL_VARIANT_SCD43: return SensorVariant::SCD43;
    default: return SensorVariant::UNKNOWN;
  }
}

uint32_t SCD41::_nowMs() const {
  return (_config.nowMs != nullptr) ? _config.nowMs(_config.timeUser) : millis();
}

uint32_t SCD41::_nowUs() const {
  return (_config.nowUs != nullptr) ? _config.nowUs(_config.timeUser) : micros();
}

void SCD41::_cooperativeYield() const {
  if (_config.cooperativeYield != nullptr) {
    _config.cooperativeYield(_config.timeUser);
  } else {
    yield();
  }
}

uint32_t SCD41::_periodMsForMode() const {
  switch (_operatingMode) {
    case OperatingMode::PERIODIC: return cmd::PERIODIC_INTERVAL_MS;
    case OperatingMode::LOW_POWER_PERIODIC: return cmd::LOW_POWER_PERIODIC_INTERVAL_MS;
    default: return 0;
  }
}

uint32_t SCD41::_periodicFetchMarginMs() const {
  if (_config.periodicFetchMarginMs != 0) {
    return _config.periodicFetchMarginMs;
  }
  const uint32_t period = _periodMsForMode();
  if (period == 0) {
    return 0;
  }
  const uint32_t autoMargin = period / 20U;
  return (autoMargin < 100U) ? 100U : autoMargin;
}

uint32_t SCD41::_periodicReadyMs(uint32_t nowMs) const {
  const uint32_t period = _periodMsForMode();
  if (period == 0) {
    return nowMs;
  }
  const uint32_t margin = _periodicFetchMarginMs();
  const uint32_t base = (_lastFetchMs != 0) ? _lastFetchMs : _periodicStartMs;
  const uint32_t target = base + ((period > margin) ? (period - margin) : 0U);
  return _timeElapsed(nowMs, target) ? nowMs : target;
}

} // namespace SCD41
