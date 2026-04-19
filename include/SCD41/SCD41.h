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

/// Driver health state derived from tracked I2C successes and failures.
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// Observed sensor variant decoded from serial-number bits [15:12].
enum class SensorVariant : uint8_t {
  UNKNOWN = 0xFF, ///< Variant not yet observed
  SCD40 = 0x00,   ///< SCD40 variant
  SCD41 = 0x01,   ///< SCD41 variant
  SCD42 = 0x02,   ///< SCD42 variant
  SCD43 = 0x05    ///< SCD43 variant
};

/// High-level operating mode tracked by the driver.
enum class OperatingMode : uint8_t {
  IDLE = 0,              ///< Idle; configuration commands are allowed
  PERIODIC = 1,          ///< Standard 5 s periodic measurement mode
  LOW_POWER_PERIODIC = 2, ///< Low-power 30 s periodic measurement mode
  POWER_DOWN = 3         ///< Lowest-power sleep state
};

/// Long-running command currently scheduled in the driver state machine.
enum class PendingCommand : uint8_t {
  NONE = 0,            ///< No deferred command is scheduled
  STOP_PERIODIC,       ///< Waiting for `stop_periodic_measurement` settle time
  SINGLE_SHOT,         ///< Waiting for full single-shot completion
  SINGLE_SHOT_RHT_ONLY, ///< Waiting for RHT-only single-shot completion
  POWER_DOWN,          ///< Waiting for power-down command completion
  WAKE_UP,             ///< Waiting for wake-up settle time
  PERSIST_SETTINGS,    ///< Waiting for EEPROM persist completion
  REINIT,              ///< Waiting for reinit completion
  FACTORY_RESET,       ///< Waiting for factory-reset completion
  SELF_TEST,           ///< Waiting for self-test result
  FORCED_RECALIBRATION, ///< Waiting for FRC result
  POWER_CYCLE          ///< Waiting for externally initiated power-cycle settle time
};

/// Parsed `get_data_ready_status` response.
struct DataReadyStatus {
  uint16_t raw = 0; ///< Raw 16-bit status word
  bool ready = false; ///< True when `(raw & 0x07FF) != 0`
};

/// Cached device identity derived from `get_serial_number`.
struct Identity {
  uint64_t serialNumber = 0; ///< 48-bit serial number packed into the low bits
  SensorVariant variant = SensorVariant::UNKNOWN; ///< Variant decoded from the serial number
};

/// Converted measurement sample in engineering units.
struct Measurement {
  uint16_t co2Ppm = 0;       ///< CO2 concentration in ppm
  float temperatureC = 0.0f; ///< Temperature in Celsius
  float humidityPct = 0.0f;  ///< Relative humidity in percent
  bool co2Valid = false;     ///< False for RHT-only single-shot samples
};

/// Raw measurement words returned by `read_measurement`.
struct RawSample {
  uint16_t rawCo2 = 0;         ///< Raw CO2 word, interpreted directly as ppm
  uint16_t rawTemperature = 0; ///< Raw temperature word
  uint16_t rawHumidity = 0;    ///< Raw humidity word
};

/// Converted measurement sample in fixed-point units.
struct CompensatedSample {
  uint16_t co2Ppm = 0;            ///< CO2 concentration in ppm
  int32_t tempC_x1000 = 0;        ///< Temperature in milli-degrees Celsius
  uint32_t humidityPct_x1000 = 0; ///< Relative humidity * 1000
  bool co2Valid = false;          ///< False for RHT-only single-shot samples
};

/// Snapshot of driver state and best-effort live configuration.
struct SettingsSnapshot {
  bool initialized = false; ///< True after a successful begin()
  DriverState state = DriverState::UNINIT; ///< Current tracked driver state
  OperatingMode operatingMode = OperatingMode::IDLE; ///< Current operating mode
  SingleShotMode singleShotMode = SingleShotMode::CO2_T_RH; ///< Preferred idle single-shot mode
  PendingCommand pendingCommand = PendingCommand::NONE; ///< Deferred command awaiting completion
  bool busy = false; ///< Convenience flag mirroring `pendingCommand != NONE`
  uint32_t commandReadyMs = 0; ///< Earliest tick time for pending-command completion
  uint8_t i2cAddress = cmd::I2C_ADDRESS; ///< Configured I2C address
  uint32_t i2cTimeoutMs = 50; ///< Configured transport timeout
  uint8_t offlineThreshold = 5; ///< Failure threshold used for OFFLINE transition
  bool measurementPending = false; ///< True while a sample fetch is in flight
  bool measurementReady = false; ///< True when a cached sample can be consumed
  uint32_t measurementReadyMs = 0; ///< Earliest time a pending sample may become ready
  uint32_t sampleTimestampMs = 0; ///< Timestamp of the last stored sample
  uint32_t missedSamples = 0; ///< Estimated missed periodic samples since last fetch
  bool lastSampleCo2Valid = false; ///< False for RHT-only single-shot samples
  SensorVariant sensorVariant = SensorVariant::UNKNOWN; ///< Observed serial-number variant
  bool serialNumberValid = false; ///< True when `serialNumber` was read successfully
  uint64_t serialNumber = 0; ///< Cached 48-bit serial number
  bool liveConfigValid = false; ///< True when idle-only live configuration fields were refreshed
  int32_t temperatureOffsetC_x1000 = 0; ///< Live temperature offset in milli-degrees Celsius
  uint16_t sensorAltitudeM = 0; ///< Live altitude compensation in meters
  uint32_t ambientPressurePa = 0; ///< Live ambient-pressure compensation in pascals
  bool automaticSelfCalibrationEnabled = false; ///< Live ASC enable state
  uint16_t automaticSelfCalibrationTargetPpm = 0; ///< Live ASC target in ppm
  uint16_t automaticSelfCalibrationInitialPeriodHours = 0; ///< Live ASC initial period in hours
  uint16_t automaticSelfCalibrationStandardPeriodHours = 0; ///< Live ASC standard period in hours
};

/// Managed SCD41 driver with tracked health, bounded waits, and explicit state transitions.
class SCD41 {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================

  /// Initialize the driver, wait the configured power-up settle time, and verify device identity.
  Status begin(const Config& config);
  /// Advance pending command completion using the caller's monotonic millisecond clock.
  void tick(uint32_t nowMs);
  /// Reset the driver to `UNINIT` and clear runtime state.
  void end();

  bool isInitialized() const { return _initialized; } ///< True after a successful begin()
  const Config& getConfig() const { return _config; } ///< Return the active configuration copy

  // =========================================================================
  // Diagnostics
  // =========================================================================

  /// Probe for device presence without updating runtime health counters.
  Status probe();
  /// Attempt manual recovery using probe, optional bus reset, and optional power cycle hooks.
  Status recover();

  // =========================================================================
  // Driver state / health
  // =========================================================================

  DriverState state() const { return _driverState; } ///< Current tracked driver state
  bool isOnline() const {
    return _driverState == DriverState::READY || _driverState == DriverState::DEGRADED;
  } ///< True when the driver is READY or DEGRADED
  bool isBusy() const { return _pendingCommand != PendingCommand::NONE; } ///< True while a deferred command is pending
  bool isPeriodicActive() const {
    return _operatingMode == OperatingMode::PERIODIC ||
           _operatingMode == OperatingMode::LOW_POWER_PERIODIC;
  } ///< True when the sensor is in standard or low-power periodic mode
  OperatingMode operatingMode() const { return _operatingMode; } ///< Current tracked operating mode
  PendingCommand pendingCommand() const { return _pendingCommand; } ///< Deferred command awaiting completion
  uint32_t commandReadyMs() const { return _commandReadyMs; } ///< Scheduled completion time for `pendingCommand()`

  uint32_t lastOkMs() const { return _lastOkMs; } ///< Timestamp of the last tracked successful I2C operation
  uint32_t lastErrorMs() const { return _lastErrorMs; } ///< Timestamp of the last tracked I2C failure
  Status lastError() const { return _lastError; } ///< Most recent tracked I2C failure status
  uint8_t consecutiveFailures() const { return _consecutiveFailures; } ///< Failures since the last success
  uint32_t totalFailures() const { return _totalFailures; } ///< Lifetime tracked I2C failure counter
  uint32_t totalSuccess() const { return _totalSuccess; } ///< Lifetime tracked I2C success counter

  // =========================================================================
  // Measurement API
  // =========================================================================

  /// Request a measurement:
  /// - in IDLE: starts a single-shot measurement using the configured singleShotMode
  /// - in PERIODIC / LOW_POWER_PERIODIC: schedules the next fetch
  Status requestMeasurement();

  bool measurementPending() const { return _measurementRequested; } ///< True while a sample fetch is still pending inside the driver
  bool measurementReady() const { return _measurementReady; } ///< True when `getMeasurement()` can consume a cached sample
  uint32_t measurementReadyMs() const { return _measurementReadyMs; } ///< Earliest time a pending measurement may become available
  bool lastSampleCo2Valid() const { return _lastSampleCo2Valid; } ///< False for RHT-only single-shot samples
  uint32_t sampleTimestampMs() const { return _sampleTimestampMs; } ///< Timestamp of the cached sample
  uint32_t sampleAgeMs(uint32_t nowMs) const {
    return _sampleTimestampMs == 0 ? 0 : (nowMs - _sampleTimestampMs);
  } ///< Age of the cached sample, or zero when no sample is stored
  uint32_t missedSamplesEstimate() const { return _missedSamples; } ///< Estimated missed periodic samples

  /// Directly execute `read_measurement` when a sample is available and update the cached sample.
  Status readMeasurement(Measurement& out);
  /// Consume the cached converted sample and clear the ready flag.
  Status getMeasurement(Measurement& out);
  /// Return the most recently cached converted sample without clearing the ready flag.
  Status getLastMeasurement(Measurement& out) const;
  /// Return the last raw sample without clearing the ready flag.
  Status getRawSample(RawSample& out) const;
  /// Return the last fixed-point converted sample without clearing the ready flag.
  Status getCompensatedSample(CompensatedSample& out) const;
  /// Read the sensor data-ready state into a simple boolean.
  Status readDataReadyStatus(bool& ready);
  /// Read the raw and decoded `get_data_ready_status` response.
  Status readDataReadyStatus(DataReadyStatus& out);

  // =========================================================================
  // Measurement mode / identification
  // =========================================================================

  /// Set the preferred idle single-shot mode used by `requestMeasurement()`.
  Status setSingleShotMode(SingleShotMode mode);
  /// Return the preferred idle single-shot mode.
  Status getSingleShotMode(SingleShotMode& out) const;
  /// Start a full single-shot CO2 + temperature + humidity measurement.
  Status startSingleShotMeasurement();
  /// Start a single-shot temperature + humidity measurement with invalid CO2 output.
  Status startSingleShotRhtOnlyMeasurement();
  /// Start standard periodic measurement mode.
  Status startPeriodicMeasurement();
  /// Start low-power periodic measurement mode on SCD41 variants only.
  Status startLowPowerPeriodicMeasurement();
  /// Stop periodic or low-power periodic mode and schedule the required settle window.
  Status stopPeriodicMeasurement();
  /// Enter the sensor power-down state.
  Status powerDown();
  /// Wake the sensor from power-down and schedule the required settle window.
  Status wakeUp();

  /// Read and cache the 48-bit serial number.
  Status readSerialNumber(uint64_t& serial);
  /// Return the cached identity, reading and caching the serial number first if needed.
  Status getIdentity(Identity& out);
  /// Return the observed sensor variant, probing the serial number if needed.
  Status readSensorVariant(SensorVariant& out);

  // =========================================================================
  // Configuration commands
  // =========================================================================

  /// Set the temperature offset in degrees Celsius.
  Status setTemperatureOffsetC(float offsetC);
  /// Read the temperature offset in degrees Celsius.
  Status getTemperatureOffsetC(float& out);
  /// Set the temperature offset in milli-degrees Celsius.
  Status setTemperatureOffsetC_x1000(int32_t offsetC_x1000);
  /// Read the temperature offset in milli-degrees Celsius.
  Status getTemperatureOffsetC_x1000(int32_t& out);

  /// Set the altitude compensation value in meters.
  Status setSensorAltitudeM(uint16_t altitudeM);
  /// Read the altitude compensation value in meters.
  Status getSensorAltitudeM(uint16_t& out);

  /// Set the ambient-pressure override in pascals.
  Status setAmbientPressurePa(uint32_t pressurePa);
  /// Read the ambient-pressure override in pascals.
  Status getAmbientPressurePa(uint32_t& out);

  /// Enable or disable automatic self calibration.
  Status setAutomaticSelfCalibrationEnabled(bool enabled);
  /// Read the automatic-self-calibration enable state.
  Status getAutomaticSelfCalibrationEnabled(bool& enabled);
  /// Set the automatic-self-calibration target concentration in ppm.
  Status setAutomaticSelfCalibrationTargetPpm(uint16_t ppm);
  /// Read the automatic-self-calibration target concentration in ppm.
  Status getAutomaticSelfCalibrationTargetPpm(uint16_t& out);
  /// Set the ASC initial period in hours.
  Status setAutomaticSelfCalibrationInitialPeriodHours(uint16_t hours);
  /// Read the ASC initial period in hours.
  Status getAutomaticSelfCalibrationInitialPeriodHours(uint16_t& out);
  /// Set the ASC standard period in hours.
  Status setAutomaticSelfCalibrationStandardPeriodHours(uint16_t hours);
  /// Read the ASC standard period in hours.
  Status getAutomaticSelfCalibrationStandardPeriodHours(uint16_t& out);

  // =========================================================================
  // Long-running maintenance commands
  // =========================================================================

  /// Persist the supported runtime settings to EEPROM.
  Status startPersistSettings();
  /// Reload persisted settings from EEPROM into runtime state.
  Status startReinit();
  /// Restore factory defaults and erase stored calibration history.
  Status startFactoryReset();
  /// Start the 10 s sensor self-test.
  Status startSelfTest();
  bool selfTestReady() const { return _selfTestCompleted; } ///< True after self-test completion has been processed
  /// Return the interpreted self-test result, reporting failure as `COMMAND_FAILED`.
  Status getSelfTestResult(uint16_t& rawResult);
  /// Return the raw self-test result word even when it indicates failure.
  Status getSelfTestRawResult(uint16_t& rawResult);

  /// Start forced recalibration with a reference concentration in ppm.
  Status startForcedRecalibration(uint16_t referencePpm);
  bool forcedRecalibrationReady() const { return _forcedRecalibrationCompleted; } ///< True after FRC completion has been processed
  /// Return the interpreted forced-recalibration correction in ppm.
  Status getForcedRecalibrationCorrectionPpm(int16_t& correctionPpm);
  /// Return the raw forced-recalibration result word even when it indicates failure.
  Status getForcedRecalibrationRawResult(uint16_t& rawResult);

  // =========================================================================
  // Snapshot helpers
  // =========================================================================

  /// Return a snapshot of local driver state without issuing live I2C reads.
  Status getSettings(SettingsSnapshot& out) const;
  /// Return a best-effort state and live-configuration snapshot.
  /// @note In periodic mode only ambient pressure is refreshed because other configuration
  ///       commands are idle-only. When busy or powered down, only local state is returned.
  Status readSettings(SettingsSnapshot& out);

  // =========================================================================
  // Low-Level Command Access
  // =========================================================================

  /// Issue an immediate raw 16-bit command that does not require managed driver state updates.
  /// @note Managed state-transition commands are rejected. `allowExpectedNack` is intended only
  ///       for commands whose device-side NACK is part of the documented protocol behavior.
  Status writeCommand(uint16_t command, bool allowExpectedNack = false);
  /// Issue an immediate raw command with one CRC-protected payload word.
  Status writeCommandWithData(uint16_t command, uint16_t data);
  /// Issue an immediate short read command and return the raw response bytes.
  /// @note `allowNoData` maps a transport-reported read-header NACK to `MEASUREMENT_NOT_READY`
  ///       only when the transport declares `TransportCapability::READ_HEADER_NACK`.
  Status readCommand(uint16_t command, uint8_t* out, size_t len, bool allowNoData = false);
  /// Issue an immediate short read command and decode one CRC-checked word.
  Status readWordCommand(uint16_t command, uint16_t& out);
  /// Issue an immediate short read command and decode multiple CRC-checked words.
  Status readWordsCommand(uint16_t command, uint16_t* out, size_t count);

  // =========================================================================
  // Helpers
  // =========================================================================

  static bool isDataReady(uint16_t rawStatus) {
    return (rawStatus & cmd::DATA_READY_MASK) != 0;
  } ///< Return true when the SCD41 data-ready mask indicates a new sample

  /// Convert a raw SCD41 temperature word to degrees Celsius.
  static float convertTemperatureC(uint16_t raw);
  /// Convert a raw SCD41 humidity word to percent relative humidity.
  static float convertHumidityPct(uint16_t raw);
  /// Convert a raw SCD41 temperature word to milli-degrees Celsius.
  static int32_t convertTemperatureC_x1000(uint16_t raw);
  /// Convert a raw SCD41 humidity word to milli-percent relative humidity.
  static uint32_t convertHumidityPct_x1000(uint16_t raw);
  /// Encode a temperature offset in degrees Celsius for the SCD41 command word.
  static uint16_t encodeTemperatureOffsetC(float offsetC);
  /// Encode a temperature offset in milli-degrees Celsius for the SCD41 command word.
  static uint16_t encodeTemperatureOffsetC_x1000(int32_t offsetC_x1000);
  /// Decode a raw temperature-offset word to degrees Celsius.
  static float decodeTemperatureOffsetC(uint16_t raw);
  /// Decode a raw temperature-offset word to milli-degrees Celsius.
  static int32_t decodeTemperatureOffsetC_x1000(uint16_t raw);
  /// Encode an ambient-pressure override in pascals for the SCD41 command word.
  static uint16_t encodeAmbientPressurePa(uint32_t pressurePa);
  /// Decode an ambient-pressure override word to pascals.
  static uint32_t decodeAmbientPressurePa(uint16_t raw);

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
  Status _startSingleShot(SingleShotMode mode);
  Status _schedulePendingCommand(PendingCommand command, uint32_t delayMs);
  void _clearMeasurementRequest();
  void _clearPendingCommand();
  void _setBusyError(Status& st) const;
  void _updatePeriodicMissedSamples(uint32_t nowMs);
  void _storeSample(const RawSample& sample, bool co2Valid);

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
  Status _validateRawCommand(uint16_t command) const;
  static bool _isPeriodicAllowedCommand(uint16_t command);
  static bool _isManagedOnlyRawCommand(uint16_t command);

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
  bool _selfTestRawValid = false;
  Status _selfTestStatus = Status::Error(Err::MEASUREMENT_NOT_READY, "Self-test not started");
  bool _selfTestCompleted = false;

  uint16_t _forcedRecalibrationRaw = 0;
  bool _forcedRecalibrationRawValid = false;
  int16_t _forcedRecalibrationCorrectionPpm = 0;
  Status _forcedRecalibrationStatus =
      Status::Error(Err::MEASUREMENT_NOT_READY, "Forced recalibration not started");
  bool _forcedRecalibrationCompleted = false;

  uint32_t _lastRecoverMs = 0;
  bool _lastRecoverValid = false;
};

} // namespace SCD41
