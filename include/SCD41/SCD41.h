/// @file SCD41.h
/// @brief Bounded, externally scheduled SCD41 driver.
#pragma once

#include <cstddef>
#include <cstdint>

#include "SCD41/CommandTable.h"
#include "SCD41/Config.h"
#include "SCD41/Status.h"
#include "SCD41/Version.h"

namespace SCD41 {

enum class DriverState : uint8_t {
  UNINIT,
  READY,
  DEGRADED,
  OFFLINE
};

enum class SensorVariant : uint8_t {
  UNKNOWN = 0xFF,
  SCD40 = 0x00,
  SCD41 = 0x01,
  SCD42 = 0x02,
  SCD43 = 0x05
};

enum class OperatingMode : uint8_t {
  UNKNOWN = 0,
  IDLE,
  PERIODIC,
  LOW_POWER_PERIODIC,
  POWER_DOWN
};

enum class ModeEvidence : uint8_t {
  UNKNOWN = 0,
  ACKNOWLEDGED,
  VERIFIED
};

enum class OperationClass : uint8_t {
  STEADY_STATE = 0,
  RUNTIME,
  MAINTENANCE,
  DIAGNOSTIC
};

enum class OperationKind : uint8_t {
  NONE = 0,
  ATTACH,
  READ_IDENTITY,
  START_PERIODIC,
  START_LOW_POWER_PERIODIC,
  STOP_PERIODIC,
  READ_DATA_READY,
  FETCH_SAMPLE,
  SINGLE_SHOT,
  SINGLE_SHOT_RHT_ONLY,
  READ_TEMPERATURE_OFFSET,
  SET_TEMPERATURE_OFFSET,
  READ_SENSOR_ALTITUDE,
  SET_SENSOR_ALTITUDE,
  READ_AMBIENT_PRESSURE,
  SET_AMBIENT_PRESSURE,
  READ_ASC_ENABLED,
  SET_ASC_ENABLED,
  READ_ASC_TARGET,
  SET_ASC_TARGET,
  READ_ASC_INITIAL_PERIOD,
  SET_ASC_INITIAL_PERIOD,
  READ_ASC_STANDARD_PERIOD,
  SET_ASC_STANDARD_PERIOD,
  READ_CONFIGURATION,
  POWER_DOWN,
  WAKE_UP,
  REINIT,
  SELF_TEST,
  FORCED_RECALIBRATION,
  PERSIST_SETTINGS,
  FACTORY_RESET,
  DIAGNOSTIC_READ_WORDS,
  DIAGNOSTIC_WRITE_COMMAND,
  DIAGNOSTIC_WRITE_WORD
};

enum class OperationState : uint8_t {
  IDLE = 0,
  ACTIVE,
  RESULT_PENDING
};

enum class OperationOutcome : uint8_t {
  SUCCEEDED = 0,
  NO_DATA,
  FAILED,
  CANCELLED,
  TIMED_OUT,
  PARTIAL,
  INDETERMINATE
};

enum class EffectState : uint8_t {
  NONE = 0,
  NOT_ATTEMPTED,
  ATTEMPTED,
  ACKNOWLEDGED,
  VERIFIED,
  UNKNOWN
};

enum class OperationPhase : uint8_t {
  NONE = 0,
  WAIT_POWER_UP,
  SEND_WAKE,
  WAIT_WAKE,
  SEND_STOP,
  WAIT_STOP,
  SEND_COMMAND,
  WAIT_EXECUTION,
  SEND_READY_COMMAND,
  READ_READY_RESPONSE,
  SEND_READ_COMMAND,
  READ_RESPONSE,
  SEND_VERIFY_COMMAND,
  READ_VERIFY_RESPONSE,
  READ_DEFERRED_RESULT
};

enum class MaintenanceConfirmation : uint8_t {
  NONE = 0,
  FORCED_RECALIBRATION,
  PERSIST_SETTINGS,
  FACTORY_RESET
};

enum class ConfigurationField : uint16_t {
  NONE = 0,
  TEMPERATURE_OFFSET = 1U << 0,
  SENSOR_ALTITUDE = 1U << 1,
  AMBIENT_PRESSURE = 1U << 2,
  ASC_ENABLED = 1U << 3,
  ASC_TARGET = 1U << 4,
  ASC_INITIAL_PERIOD = 1U << 5,
  ASC_STANDARD_PERIOD = 1U << 6
};

inline constexpr uint16_t configurationFieldMask(ConfigurationField field) {
  return static_cast<uint16_t>(field);
}

static constexpr uint16_t ALL_CONFIGURATION_FIELDS =
    configurationFieldMask(ConfigurationField::TEMPERATURE_OFFSET) |
    configurationFieldMask(ConfigurationField::SENSOR_ALTITUDE) |
    configurationFieldMask(ConfigurationField::AMBIENT_PRESSURE) |
    configurationFieldMask(ConfigurationField::ASC_ENABLED) |
    configurationFieldMask(ConfigurationField::ASC_TARGET) |
    configurationFieldMask(ConfigurationField::ASC_INITIAL_PERIOD) |
    configurationFieldMask(ConfigurationField::ASC_STANDARD_PERIOD);

/// Fields stored by the SCD41 `persist_settings` command. Ambient pressure is
/// a runtime override and is intentionally excluded.
static constexpr uint16_t PERSISTABLE_CONFIGURATION_FIELDS =
    ALL_CONFIGURATION_FIELDS &
    static_cast<uint16_t>(
        ~configurationFieldMask(ConfigurationField::AMBIENT_PRESSURE));

enum SampleFlag : uint16_t {
  SAMPLE_NONE = 0,
  SAMPLE_CO2_VALID = 1U << 0,
  SAMPLE_TEMPERATURE_VALID = 1U << 1,
  SAMPLE_HUMIDITY_VALID = 1U << 2,
  SAMPLE_FRESH = 1U << 3
};

struct OperationId {
  uint32_t requestId = 0;
  uint32_t generation = 0;
};

inline constexpr bool operator==(const OperationId& lhs, const OperationId& rhs) {
  return lhs.requestId == rhs.requestId && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const OperationId& lhs, const OperationId& rhs) {
  return !(lhs == rhs);
}

struct OperationOptions {
  /// Caller correlation value. Zero is invalid.
  uint32_t requestId = 0;
  /// Admission time in the owner's monotonic 32-bit millisecond clock.
  uint32_t nowMs = 0;
  /// Immutable absolute deadline in the same clock; must be within 2^31 ms.
  uint32_t deadlineMs = 0;
};

struct OperationRequest {
  OperationKind kind = OperationKind::NONE;
  int32_t signedValue = 0;
  uint32_t value = 0;
  uint16_t command = 0;
  uint8_t wordCount = 0;
  MaintenanceConfirmation confirmation = MaintenanceConfirmation::NONE;

  static OperationRequest make(OperationKind operation) {
    OperationRequest request;
    request.kind = operation;
    return request;
  }
  static OperationRequest setTemperatureOffsetMilliC(int32_t valueMilliC) {
    OperationRequest request = make(OperationKind::SET_TEMPERATURE_OFFSET);
    request.signedValue = valueMilliC;
    return request;
  }
  static OperationRequest setSensorAltitudeM(uint16_t altitudeM) {
    OperationRequest request = make(OperationKind::SET_SENSOR_ALTITUDE);
    request.value = altitudeM;
    return request;
  }
  static OperationRequest setAmbientPressurePa(uint32_t pressurePa) {
    OperationRequest request = make(OperationKind::SET_AMBIENT_PRESSURE);
    request.value = pressurePa;
    return request;
  }
  static OperationRequest setAscEnabled(bool enabled) {
    OperationRequest request = make(OperationKind::SET_ASC_ENABLED);
    request.value = enabled ? 1U : 0U;
    return request;
  }
  static OperationRequest setAscTargetPpm(uint16_t ppm) {
    OperationRequest request = make(OperationKind::SET_ASC_TARGET);
    request.value = ppm;
    return request;
  }
  static OperationRequest setAscInitialPeriodHours(uint16_t hours) {
    OperationRequest request = make(OperationKind::SET_ASC_INITIAL_PERIOD);
    request.value = hours;
    return request;
  }
  static OperationRequest setAscStandardPeriodHours(uint16_t hours) {
    OperationRequest request = make(OperationKind::SET_ASC_STANDARD_PERIOD);
    request.value = hours;
    return request;
  }
  static OperationRequest forcedRecalibration(uint16_t referencePpm) {
    OperationRequest request = make(OperationKind::FORCED_RECALIBRATION);
    request.value = referencePpm;
    request.confirmation = MaintenanceConfirmation::FORCED_RECALIBRATION;
    return request;
  }
  static OperationRequest persistSettings() {
    OperationRequest request = make(OperationKind::PERSIST_SETTINGS);
    request.confirmation = MaintenanceConfirmation::PERSIST_SETTINGS;
    return request;
  }
  static OperationRequest factoryReset() {
    OperationRequest request = make(OperationKind::FACTORY_RESET);
    request.confirmation = MaintenanceConfirmation::FACTORY_RESET;
    return request;
  }
  static OperationRequest diagnosticReadWords(uint16_t cmdWord, uint8_t count) {
    OperationRequest request = make(OperationKind::DIAGNOSTIC_READ_WORDS);
    request.command = cmdWord;
    request.wordCount = count;
    return request;
  }
  static OperationRequest diagnosticWriteCommand(uint16_t cmdWord) {
    OperationRequest request = make(OperationKind::DIAGNOSTIC_WRITE_COMMAND);
    request.command = cmdWord;
    return request;
  }
  static OperationRequest diagnosticWriteWord(uint16_t cmdWord, uint16_t dataWord) {
    OperationRequest request = make(OperationKind::DIAGNOSTIC_WRITE_WORD);
    request.command = cmdWord;
    request.value = dataWord;
    return request;
  }
};

struct OperationLimits {
  OperationClass operationClass = OperationClass::RUNTIME;
  uint8_t maxCallbacks = 0;
  uint8_t maxRetries = 0;
  /// Maximum cumulative driver-controlled wait after admission. A carried
  /// safety window is rejected by `start()` before admission.
  uint32_t maxWaitMs = 0;
  bool writesNonvolatile = false;
  bool destructive = false;
};

struct Identity {
  uint64_t serialNumber = 0;
  SensorVariant variant = SensorVariant::UNKNOWN;
  uint32_t sensorEpoch = 0;
  bool valid = false;
};

struct DataReadyStatus {
  uint16_t raw = 0;
  bool ready = false;
};

struct FixedSample {
  uint16_t co2Ppm = 0;
  int32_t temperatureMilliC = 0;
  uint32_t humidityMilliPercent = 0;
  uint32_t capturedAtMs = 0;
  uint32_t sensorEpoch = 0;
  /// 1-based sample count since the latest sensor epoch or mode transition.
  uint32_t sequence = 0;
  OperatingMode mode = OperatingMode::UNKNOWN;
  uint16_t flags = SAMPLE_NONE;
};

struct ConfigurationSnapshot {
  int32_t temperatureOffsetMilliC = 0;
  uint16_t sensorAltitudeM = 0;
  uint32_t ambientPressurePa = 0;
  bool ascEnabled = false;
  uint16_t ascTargetPpm = 0;
  uint16_t ascInitialPeriodHours = 0;
  uint16_t ascStandardPeriodHours = 0;
  /// Fields whose current RAM value was read back successfully.
  uint16_t verifiedMask = 0;
  /// EEPROM-persistable fields changed or possibly changed through this
  /// instance and not known to have been persisted. Runtime ambient pressure
  /// is never included.
  /// A field may be both verified and dirty.
  uint16_t dirtyMask = 0;
  uint32_t sensorEpoch = 0;
  /// An EEPROM command may have taken effect, so blind persistence is blocked.
  bool persistenceIndeterminate = false;
};

struct RuntimeSnapshot {
  bool bound = false;
  bool attached = false;
  DriverState driverState = DriverState::UNINIT;
  OperatingMode operatingMode = OperatingMode::UNKNOWN;
  ModeEvidence modeEvidence = ModeEvidence::UNKNOWN;
  OperationState operationState = OperationState::IDLE;
  OperationId operationId = {};
  OperationKind operationKind = OperationKind::NONE;
  uint32_t nextDueMs = 0;
  /// Earliest safe admission/transfer time when `nextSafeCommandValid` is true.
  uint32_t nextSafeCommandMs = 0;
  bool nextSafeCommandValid = false;
  uint32_t sensorEpoch = 0;
  bool reconciliationRequired = true;
  bool sampleAvailable = false;
};

struct HealthSnapshot {
  DriverState state = DriverState::UNINIT;
  uint32_t lastTransferOkMs = 0;
  uint32_t lastTransferErrorMs = 0;
  Status lastTransferError = Status::Ok();
  uint8_t consecutiveTransferFailures = 0;
  uint32_t totalTransferSuccess = 0;
  uint32_t totalTransferFailures = 0;
  uint32_t expectedNacks = 0;
  uint32_t totalProtocolFailures = 0;
  uint32_t totalCrcFailures = 0;
  uint32_t lastProtocolErrorMs = 0;
  Status lastProtocolError = Status::Ok();
  uint32_t totalOperationSuccess = 0;
  uint32_t totalOperationFailures = 0;
  uint32_t totalOperationCancelled = 0;
  uint32_t lastOperationErrorMs = 0;
  Status lastOperationError = Status::Ok();
  OperationId lastOperationErrorId = {};
  OperationKind lastOperationErrorKind = OperationKind::NONE;
  /// Most recently completed operation, whether successful or not.
  OperationId lastOperationId = {};
  OperationKind lastOperationKind = OperationKind::NONE;
};

/// Fixed result storage. Interpret members by `OperationResult::kind`:
/// - sample operations -> `sample`
/// - attach/identity/wake/reset verification -> `identity`
/// - setting/configuration/persistence/reset operations -> `configuration`
/// - data-ready -> `dataReady`
/// - temperature offset/FRC -> `signedValue`
/// - altitude/pressure/ASC numeric/self-test -> `value`
/// - ASC enabled -> `boolValue`
/// - diagnostic reads and raw maintenance responses -> `rawWords`/`wordCount`
struct OperationValue {
  FixedSample sample = {};
  Identity identity = {};
  ConfigurationSnapshot configuration = {};
  DataReadyStatus dataReady = {};
  int32_t signedValue = 0;
  uint32_t value = 0;
  uint16_t rawWords[3] = {};
  uint8_t wordCount = 0;
  bool boolValue = false;
};

struct OperationResult {
  OperationId id = {};
  OperationKind kind = OperationKind::NONE;
  OperationOutcome outcome = OperationOutcome::FAILED;
  EffectState effect = EffectState::NONE;
  Status status = Status::Error(Err::RESULT_NOT_READY, "No result");
  OperationPhase finalPhase = OperationPhase::NONE;
  uint32_t startedMs = 0;
  uint32_t completedMs = 0;
  uint32_t deadlineMs = 0;
  uint32_t sensorEpoch = 0;
  /// Managed mode and confidence when the operation terminated.
  OperatingMode operatingMode = OperatingMode::UNKNOWN;
  ModeEvidence modeEvidence = ModeEvidence::UNKNOWN;
  uint16_t completedFieldMask = 0;
  /// Number of physical callback attempts consumed by this operation.
  uint8_t callbacksUsed = 0;
  bool reconciliationRequired = false;
  OperationValue value = {};
};

struct PollResult {
  OperationState state = OperationState::IDLE;
  OperationId id = {};
  OperationKind kind = OperationKind::NONE;
  Status status = Status::Ok();
  uint32_t nextDueMs = 0;
  uint8_t callbacksUsed = 0;
};

class SCD41 {
public:
  SCD41() = default;
  SCD41(const SCD41&) = delete;
  SCD41& operator=(const SCD41&) = delete;
  SCD41(SCD41&&) = delete;
  SCD41& operator=(SCD41&&) = delete;

  /// Validate and copy the non-owning transport configuration. Performs no I2C.
  Status begin(const Config& config);
  /// Advance the one active operation. Invokes at most `maxCallbacks` transport callbacks.
  PollResult poll(uint32_t nowMs, uint8_t maxCallbacks = 1);
  /// Compatibility executor; delegates to `poll(nowMs, 1)`.
  Status tick(uint32_t nowMs);
  /// Start one typed operation without invoking transport.
  Status start(const OperationRequest& request, const OperationOptions& options,
               OperationId& assignedId);
  /// Cancel active host work without I2C and retain one terminal result.
  Status cancel(const OperationId& id, uint32_t nowMs);
  /// Copy and consume the matching terminal result exactly once. Performs no I2C.
  Status takeResult(const OperationId& expectedId, OperationResult& out);
  /// Unbind without I2C. Active work becomes a retained cancelled result.
  void end();

  bool isBound() const { return _bound; }
  bool isAttached() const { return _attached; }
  DriverState state() const { return _driverState; }
  DriverState driverState() const { return state(); }
  OperationState operationState() const;

  RuntimeSnapshot runtimeSnapshot() const;
  HealthSnapshot healthSnapshot() const { return _health; }
  ConfigurationSnapshot configurationSnapshot() const { return _configuration; }
  Identity identity() const { return _identity; }
  Status peekLatestSample(FixedSample& out) const;

  static OperationLimits limits(OperationKind kind);
  static bool isDataReady(uint16_t rawStatus) {
    return (rawStatus & cmd::DATA_READY_MASK) != 0;
  }
  static float convertTemperatureC(uint16_t raw);
  static float convertHumidityPct(uint16_t raw);
  static int32_t convertTemperatureMilliC(uint16_t raw);
  static uint32_t convertHumidityMilliPercent(uint16_t raw);
  static Status encodeTemperatureOffsetC(float offsetC, uint16_t& out);
  static Status encodeTemperatureOffsetMilliC(int32_t offsetMilliC, uint16_t& out);
  static float decodeTemperatureOffsetC(uint16_t raw);
  static int32_t decodeTemperatureOffsetMilliC(uint16_t raw);
  static Status encodeAmbientPressurePa(uint32_t pressurePa, uint16_t& out);
  static uint32_t decodeAmbientPressurePa(uint16_t raw);

private:
  struct ActiveOperation {
    OperationRequest request = {};
    OperationId id = {};
    OperationPhase phase = OperationPhase::NONE;
    EffectState effect = EffectState::NONE;
    uint32_t startedMs = 0;
    uint32_t deadlineMs = 0;
    uint32_t nextDueMs = 0;
    uint16_t completedFieldMask = 0;
    uint16_t desiredRaw = 0;
    uint8_t fieldIndex = 0;
    uint8_t callbacksUsed = 0;
    bool effectfulWriteAttempted = false;
  };

  Status _validateConfig(const Config& config) const;
  Status _validateStart(const OperationRequest& request,
                        const OperationOptions& options) const;
  Status _validateRequestValue(const OperationRequest& request) const;
  Status _validateAdmission(OperationKind kind) const;
  Status _beginOperation(const OperationRequest& request,
                         const OperationOptions& options,
                         const OperationId& id);
  Status _step(uint32_t& nowMs, uint8_t& callbacksRemaining);
  Status _stepAttach(uint32_t& nowMs, uint8_t& callbacksRemaining);
  Status _stepReadLike(uint32_t& nowMs, uint8_t& callbacksRemaining);
  Status _stepWriteLike(uint32_t& nowMs, uint8_t& callbacksRemaining);
  Status _stepMeasurement(uint32_t& nowMs, uint8_t& callbacksRemaining);
  Status _stepMaintenance(uint32_t& nowMs, uint8_t& callbacksRemaining);
  Status _stepDiagnostic(uint32_t& nowMs, uint8_t& callbacksRemaining);

  Status _writeCommand(uint16_t command, TransferIntent intent, uint32_t& nowMs,
                       uint8_t& callbacksRemaining, bool effectful = false);
  Status _writeCommandWithWord(uint16_t command, uint16_t word, uint32_t& nowMs,
                               uint8_t& callbacksRemaining,
                               bool effectful = false);
  Status _readWords(uint16_t* words, uint8_t count, uint32_t& nowMs,
                    uint8_t& callbacksRemaining);
  Status _attemptTransfer(const uint8_t* writeData, size_t writeLength,
                          uint8_t* readData, size_t readLength, TransferIntent intent,
                          uint32_t& nowMs, uint8_t& callbacksRemaining,
                          bool effectful);
  Status _checkCommandSpacing(uint32_t nowMs);

  void _finish(OperationOutcome outcome, EffectState effect, const Status& status,
               uint32_t completedMs);
  void _finishTransferFailure(const Status& status, uint32_t completedMs);
  void _applyReadValue(OperationKind kind, uint16_t value, uint32_t nowMs);
  void _applyVerifiedSetting(OperationKind kind, uint16_t value);
  void _storeSample(const uint16_t words[3], bool co2Valid, uint32_t nowMs);
  void _setMode(OperatingMode mode, ModeEvidence evidence);
  void _markReconciliationRequired();
  void _advanceSensorEpoch();
  void _recordTransfer(const TransferResult& result, bool expectedNack);
  void _recordProtocolFailure(const Status& status, uint32_t nowMs);
  void _recordOperationOutcome(const OperationResult& result);

  static bool _timeReached(uint32_t nowMs, uint32_t targetMs);
  static bool _deadlineValid(uint32_t nowMs, uint32_t deadlineMs);
  static bool _isSettingWrite(OperationKind kind);
  static bool _isMaintenance(OperationKind kind);
  static bool _isDiagnostic(OperationKind kind);
  static bool _isEffectful(OperationKind kind);
  static bool _periodicAllowed(OperationKind kind);
  static uint16_t _readCommandFor(OperationKind kind);
  static uint16_t _writeCommandFor(OperationKind kind);
  static ConfigurationField _fieldFor(OperationKind kind);
  static SensorVariant _variantFromSerialWord(uint16_t word0);
  static uint8_t _crc8(const uint8_t* data, size_t length);
  static uint32_t _executionWaitMs(OperationKind kind);

  Config _config = {};
  bool _bound = false;
  bool _attached = false;
  DriverState _driverState = DriverState::UNINIT;
  OperatingMode _operatingMode = OperatingMode::UNKNOWN;
  ModeEvidence _modeEvidence = ModeEvidence::UNKNOWN;
  bool _reconciliationRequired = true;
  uint32_t _nextSafeCommandMs = 0;
  bool _nextSafeCommandValid = false;
  uint32_t _sensorEpoch = 0;
  uint32_t _sampleSequence = 0;
  uint32_t _nextGeneration = 1;

  ActiveOperation _active = {};
  bool _activeValid = false;
  OperationResult _terminal = {};
  bool _terminalValid = false;
  OperationValue _workingValue = {};

  Identity _identity = {};
  ConfigurationSnapshot _configuration = {};
  FixedSample _latestSample = {};
  bool _latestSampleValid = false;
  HealthSnapshot _health = {};

  TransferDisposition _lastTransferDisposition = TransferDisposition::NOT_STARTED;
  bool _lastTransferWasEffectful = false;
  uint32_t _lastOwnerNowMs = 0;
  bool _lastOwnerNowValid = false;
};

} // namespace SCD41
