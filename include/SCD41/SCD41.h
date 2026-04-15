/// @file SCD41.h
/// @brief Main driver class for SCD41
#pragma once

#include <cstddef>
#include <cstdint>

#include "SCD41/CommandTable.h"
#include "SCD41/Config.h"
#include "SCD41/Status.h"
#include "SCD41/Version.h"

namespace SCD41 {

/// Driver state for health monitoring
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// Observed sensor variant encoded in serial number bits [15:12]
enum class SensorVariant : uint8_t {
  UNKNOWN = 0xFF,
  SCD40 = 0x00,
  SCD41 = 0x01,
  SCD42 = 0x02,
  SCD43 = 0x05
};

/// High-level operating mode of the sensor
enum class OperatingMode : uint8_t {
  IDLE = 0,
  PERIODIC = 1,
  LOW_POWER_PERIODIC = 2,
  POWER_DOWN = 3
};

/// Long-running command currently scheduled in the driver
enum class PendingCommand : uint8_t {
  NONE = 0,
  STOP_PERIODIC,
  SINGLE_SHOT,
  SINGLE_SHOT_RHT_ONLY,
  POWER_DOWN,
  WAKE_UP,
  PERSIST_SETTINGS,
  REINIT,
  FACTORY_RESET,
  SELF_TEST,
  FORCED_RECALIBRATION,
  POWER_CYCLE
};

/// Parsed data-ready response
struct DataReadyStatus {
  uint16_t raw = 0; ///< Raw 16-bit status word
  bool ready = false;
};

/// Measurement result (float)
struct Measurement {
  uint16_t co2Ppm = 0;      ///< CO2 concentration in ppm
  float temperatureC = 0.0f; ///< Temperature in Celsius
  float humidityPct = 0.0f;  ///< Relative humidity in percent
  bool co2Valid = false;     ///< False for RHT-only single-shot samples
};

/// Raw measurement values
struct RawSample {
  uint16_t rawCo2 = 0;
  uint16_t rawTemperature = 0;
  uint16_t rawHumidity = 0;
};

/// Fixed-point converted values
struct CompensatedSample {
  uint16_t co2Ppm = 0;          ///< CO2 concentration in ppm
  int32_t tempC_x1000 = 0;      ///< Temperature * 1000
  uint32_t humidityPct_x1000 = 0; ///< Relative humidity * 1000
  bool co2Valid = false;        ///< False for RHT-only single-shot samples
};

/// Snapshot of driver configuration and state
struct SettingsSnapshot {
  bool initialized = false;
  DriverState state = DriverState::UNINIT;
  OperatingMode operatingMode = OperatingMode::IDLE;
  SingleShotMode singleShotMode = SingleShotMode::CO2_T_RH;
  PendingCommand pendingCommand = PendingCommand::NONE;
  bool busy = false;
  uint32_t commandReadyMs = 0;
  uint8_t i2cAddress = cmd::I2C_ADDRESS;
  uint32_t i2cTimeoutMs = 50;
  uint8_t offlineThreshold = 5;
  bool measurementPending = false;
  bool measurementReady = false;
  uint32_t measurementReadyMs = 0;
  uint32_t sampleTimestampMs = 0;
  uint32_t missedSamples = 0;
  bool lastSampleCo2Valid = false;
  SensorVariant sensorVariant = SensorVariant::UNKNOWN;
  bool serialNumberValid = false;
  uint64_t serialNumber = 0;
};

/// SCD41 driver class
class SCD41 {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================

  Status begin(const Config& config);
  void tick(uint32_t nowMs);
  void end();

  bool isInitialized() const { return _initialized; }
  const Config& getConfig() const { return _config; }

  // =========================================================================
  // Diagnostics
  // =========================================================================

  Status probe();
  Status recover();

  // =========================================================================
  // Driver state / health
  // =========================================================================

  DriverState state() const { return _driverState; }
  bool isOnline() const {
    return _driverState == DriverState::READY || _driverState == DriverState::DEGRADED;
  }
  bool isBusy() const { return _pendingCommand != PendingCommand::NONE; }
  bool isPeriodicActive() const {
    return _operatingMode == OperatingMode::PERIODIC ||
           _operatingMode == OperatingMode::LOW_POWER_PERIODIC;
  }
  OperatingMode operatingMode() const { return _operatingMode; }
  PendingCommand pendingCommand() const { return _pendingCommand; }
  uint32_t commandReadyMs() const { return _commandReadyMs; }

  uint32_t lastOkMs() const { return _lastOkMs; }
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  Status lastError() const { return _lastError; }
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  uint32_t totalFailures() const { return _totalFailures; }
  uint32_t totalSuccess() const { return _totalSuccess; }

  // =========================================================================
  // Measurement API
  // =========================================================================

  /// Request a measurement:
  /// - in IDLE: starts a single-shot measurement using the configured singleShotMode
  /// - in PERIODIC / LOW_POWER_PERIODIC: schedules the next fetch
  Status requestMeasurement();

  bool measurementReady() const { return _measurementReady; }
  bool lastSampleCo2Valid() const { return _lastSampleCo2Valid; }
  uint32_t sampleTimestampMs() const { return _sampleTimestampMs; }
  uint32_t sampleAgeMs(uint32_t nowMs) const {
    return _sampleTimestampMs == 0 ? 0 : (nowMs - _sampleTimestampMs);
  }
  uint32_t missedSamplesEstimate() const { return _missedSamples; }

  Status getMeasurement(Measurement& out);
  Status getRawSample(RawSample& out) const;
  Status getCompensatedSample(CompensatedSample& out) const;
  Status readDataReadyStatus(bool& ready);
  Status readDataReadyStatus(DataReadyStatus& out);

  // =========================================================================
  // Measurement mode / identification
  // =========================================================================

  Status setSingleShotMode(SingleShotMode mode);
  Status getSingleShotMode(SingleShotMode& out) const;
  Status startPeriodicMeasurement();
  Status startLowPowerPeriodicMeasurement();
  Status stopPeriodicMeasurement();
  Status powerDown();
  Status wakeUp();

  Status readSerialNumber(uint64_t& serial);
  Status readSensorVariant(SensorVariant& out);

  // =========================================================================
  // Configuration commands
  // =========================================================================

  Status setTemperatureOffsetC(float offsetC);
  Status getTemperatureOffsetC(float& out);
  Status setTemperatureOffsetC_x1000(int32_t offsetC_x1000);
  Status getTemperatureOffsetC_x1000(int32_t& out);

  Status setSensorAltitudeM(uint16_t altitudeM);
  Status getSensorAltitudeM(uint16_t& out);

  Status setAmbientPressurePa(uint32_t pressurePa);
  Status getAmbientPressurePa(uint32_t& out);

  Status setAutomaticSelfCalibrationEnabled(bool enabled);
  Status getAutomaticSelfCalibrationEnabled(bool& enabled);
  Status setAutomaticSelfCalibrationTargetPpm(uint16_t ppm);
  Status getAutomaticSelfCalibrationTargetPpm(uint16_t& out);
  Status setAutomaticSelfCalibrationInitialPeriodHours(uint16_t hours);
  Status getAutomaticSelfCalibrationInitialPeriodHours(uint16_t& out);
  Status setAutomaticSelfCalibrationStandardPeriodHours(uint16_t hours);
  Status getAutomaticSelfCalibrationStandardPeriodHours(uint16_t& out);

  // =========================================================================
  // Long-running maintenance commands
  // =========================================================================

  Status startPersistSettings();
  Status startReinit();
  Status startFactoryReset();
  Status startSelfTest();
  bool selfTestReady() const { return _selfTestCompleted; }
  Status getSelfTestResult(uint16_t& rawResult);

  Status startForcedRecalibration(uint16_t referencePpm);
  bool forcedRecalibrationReady() const { return _forcedRecalibrationCompleted; }
  Status getForcedRecalibrationCorrectionPpm(int16_t& correctionPpm);

  // =========================================================================
  // Snapshot helpers
  // =========================================================================

  Status getSettings(SettingsSnapshot& out) const;

  // =========================================================================
  // Helpers
  // =========================================================================

  static bool isDataReady(uint16_t rawStatus) {
    return (rawStatus & cmd::DATA_READY_MASK) != 0;
  }

  static float convertTemperatureC(uint16_t raw);
  static float convertHumidityPct(uint16_t raw);
  static int32_t convertTemperatureC_x1000(uint16_t raw);
  static uint32_t convertHumidityPct_x1000(uint16_t raw);
  static uint16_t encodeTemperatureOffsetC(float offsetC);
  static uint16_t encodeTemperatureOffsetC_x1000(int32_t offsetC_x1000);
  static float decodeTemperatureOffsetC(uint16_t raw);
  static int32_t decodeTemperatureOffsetC_x1000(uint16_t raw);

private:
  // =========================================================================
  // Transport wrappers
  // =========================================================================

  Status _i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf, size_t rxLen);
  Status _i2cWriteRaw(const uint8_t* buf, size_t len);
  Status _i2cWriteTracked(const uint8_t* buf, size_t len);
  Status _i2cWriteTrackedAllowExpectedNack(const uint8_t* buf, size_t len);
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf, size_t rxLen);
  Status _i2cWriteReadTrackedAllowNoData(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf,
                                         size_t rxLen, bool allowNoData);

  // =========================================================================
  // Command / parsing helpers
  // =========================================================================

  Status _writeCommand(uint16_t cmd, bool tracked, bool allowExpectedNack = false);
  Status _writeCommandWithData(uint16_t cmd, uint16_t data, bool tracked);
  Status _readCommand(uint16_t cmd, uint8_t* out, size_t len, bool tracked);
  Status _readOnly(uint8_t* out, size_t len, bool tracked, bool allowNoData = false);
  Status _readWord(uint16_t cmd, uint16_t& value, bool tracked);
  Status _readWords(uint16_t cmd, uint16_t* values, size_t count, bool tracked);
  Status _readWordsOnly(uint16_t* values, size_t count, bool tracked, bool allowNoData = false);
  Status _readMeasurementRaw(RawSample& out, bool tracked, bool allowNoData);

  // =========================================================================
  // Command scheduling / health
  // =========================================================================

  Status _updateHealth(const Status& st);
  Status _ensureCommandDelay();
  Status _waitMs(uint32_t delayMs);
  Status _ensureIdleForConfig(const char* opName) const;
  Status _schedulePendingCommand(PendingCommand command, uint32_t delayMs);
  void _clearMeasurementRequest();
  void _clearPendingCommand();
  void _setBusyError(Status& st) const;
  void _updatePeriodicMissedSamples(uint32_t nowMs);

  // =========================================================================
  // Internal command completion
  // =========================================================================

  Status _handlePendingCommand(uint32_t nowMs);
  Status _completeSelfTest();
  Status _completeForcedRecalibration();
  Status _completeMeasurement();

  // =========================================================================
  // Utility helpers
  // =========================================================================

  static bool _timeElapsed(uint32_t now, uint32_t target);
  static bool _isI2cFailure(Err code);
  static uint8_t _crc8(const uint8_t* data, size_t len);
  static SensorVariant _variantFromSerialWord(uint16_t word0);

  uint32_t _nowMs() const;
  uint32_t _nowUs() const;
  void _cooperativeYield() const;
  uint32_t _periodMsForMode() const;
  uint32_t _periodicFetchMarginMs() const;
  uint32_t _periodicReadyMs(uint32_t nowMs) const;

  // =========================================================================
  // State
  // =========================================================================

  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;
  OperatingMode _operatingMode = OperatingMode::IDLE;
  SingleShotMode _singleShotMode = SingleShotMode::CO2_T_RH;
  PendingCommand _pendingCommand = PendingCommand::NONE;

  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  uint32_t _lastCommandUs = 0;
  bool _lastCommandValid = false;
  uint32_t _commandReadyMs = 0;

  bool _measurementRequested = false;
  bool _measurementReady = false;
  uint32_t _measurementReadyMs = 0;
  uint32_t _periodicStartMs = 0;
  uint32_t _lastFetchMs = 0;
  uint32_t _sampleTimestampMs = 0;
  uint32_t _missedSamples = 0;
  bool _lastSampleCo2Valid = false;

  RawSample _rawSample = {};
  CompensatedSample _compSample = {};

  SensorVariant _sensorVariant = SensorVariant::UNKNOWN;
  uint64_t _serialNumber = 0;
  bool _serialNumberValid = false;

  uint16_t _selfTestRaw = 0;
  Status _selfTestStatus = Status::Error(Err::MEASUREMENT_NOT_READY, "Self-test not started");
  bool _selfTestCompleted = false;

  int16_t _forcedRecalibrationCorrectionPpm = 0;
  Status _forcedRecalibrationStatus =
      Status::Error(Err::MEASUREMENT_NOT_READY, "Forced recalibration not started");
  bool _forcedRecalibrationCompleted = false;

  uint32_t _lastRecoverMs = 0;
  bool _lastRecoverValid = false;
};

} // namespace SCD41
