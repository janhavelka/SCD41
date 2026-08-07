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

/// Passive transport-health state for a bound driver instance.
enum class DriverState : uint8_t {
  UNINIT,   ///< Not bound, or explicitly ended.
  READY,    ///< Bound with no consecutive attempted transfer failures.
  DEGRADED, ///< Bound with failures below the configured offline threshold.
  OFFLINE   ///< Threshold reached; diagnostic only and never an admission gate.
};

/// Return a stable allocation-free diagnostic name for a driver state.
/// @param state Driver state to name.
/// @return Static-lifetime enum name, or `"UNKNOWN"` for an unknown value.
constexpr const char* driverStateName(DriverState state) {
  switch (state) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::READY: return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
  }
  return "UNKNOWN";
}

/// Compatibility spelling shared by the mature sibling driver APIs.
/// @param state Driver state to name.
/// @return The same static-lifetime string as `driverStateName(state)`.
constexpr const char* toString(DriverState state) {
  return driverStateName(state);
}

/// Variant encoded in the dedicated `get_sensor_variant` response.
enum class SensorVariant : uint8_t {
  UNKNOWN = 0xFF, ///< No verified or recognized identity.
  SCD40 = 0x00,   ///< Observed SCD40 identity; not claimed as supported.
  SCD41 = 0x01,   ///< Supported production target.
  SCD42 = 0x02,   ///< Legacy enum retained; datasheet v1.7 defines no SCD42 code.
  SCD43 = 0x05    ///< Observed SCD43 identity; not claimed as supported.
};

/// Driver's conservative model of the sensor operating mode.
enum class OperatingMode : uint8_t {
  UNKNOWN = 0,       ///< Hardware mode is not safely known.
  IDLE,              ///< Sensor is available for idle-only commands.
  PERIODIC,          ///< Standard 5-second periodic measurement is active.
  LOW_POWER_PERIODIC,///< Low-power 30-second periodic measurement is active.
  POWER_DOWN         ///< Sensor has acknowledged entry to power-down mode.
};

/// Strength of evidence behind `OperatingMode`.
enum class ModeEvidence : uint8_t {
  UNKNOWN = 0, ///< No reliable mode claim is available.
  ACKNOWLEDGED,///< An effectful mode command completed successfully.
  VERIFIED     ///< Attach/reconciliation established usable mode state.
};

/// Scheduling and policy class published by `SCD41::limits()`.
enum class OperationClass : uint8_t {
  STEADY_STATE = 0, ///< Normal bounded measurement/configuration reads.
  RUNTIME,          ///< Startup, mode, conversion, or runtime configuration work.
  MAINTENANCE,      ///< Slow, nonvolatile, calibration, or destructive work.
  DIAGNOSTIC        ///< Explicit low-level engineering access.
};

/// Complete set of typed jobs accepted by the owner-driven engine.
enum class OperationKind : uint8_t {
  NONE = 0,                  ///< Sentinel; never admissible.
  ATTACH,                    ///< Wake/stop reconciliation plus identity verification.
  READ_IDENTITY,             ///< Read and verify the serial number and variant.
  START_PERIODIC,            ///< Start standard periodic measurement.
  START_LOW_POWER_PERIODIC,  ///< Start low-power periodic measurement.
  STOP_PERIODIC,             ///< Stop either periodic mode and wait 500 ms.
  READ_DATA_READY,           ///< Read the device data-ready status word.
  FETCH_SAMPLE,              ///< Fetch one available periodic sample.
  SINGLE_SHOT,               ///< Start and fetch a full 5-second single shot.
  SINGLE_SHOT_RHT_ONLY,      ///< Start and fetch a 50 ms RHT-only single shot.
  READ_TEMPERATURE_OFFSET,   ///< Read runtime temperature-offset compensation.
  SET_TEMPERATURE_OFFSET,    ///< Set and verify temperature-offset compensation.
  READ_SENSOR_ALTITUDE,      ///< Read sensor altitude compensation.
  SET_SENSOR_ALTITUDE,       ///< Set and verify sensor altitude compensation.
  READ_AMBIENT_PRESSURE,     ///< Read runtime ambient-pressure override.
  SET_AMBIENT_PRESSURE,      ///< Set and verify runtime ambient pressure.
  READ_ASC_ENABLED,          ///< Read automatic self-calibration enable state.
  SET_ASC_ENABLED,           ///< Set and verify ASC enable state.
  READ_ASC_TARGET,           ///< Read ASC target concentration.
  SET_ASC_TARGET,            ///< Set and verify ASC target concentration.
  READ_ASC_INITIAL_PERIOD,   ///< Read ASC initial period in hours.
  SET_ASC_INITIAL_PERIOD,    ///< Set and verify ASC initial period.
  READ_ASC_STANDARD_PERIOD,  ///< Read ASC standard period in hours.
  SET_ASC_STANDARD_PERIOD,   ///< Set and verify ASC standard period.
  READ_CONFIGURATION,        ///< Read all supported configuration fields.
  POWER_DOWN,                ///< Enter sensor power-down mode.
  WAKE_UP,                   ///< Wake and verify the sensor identity.
  REINIT,                    ///< Reload persisted settings and reconcile state.
  SELF_TEST,                 ///< Run the bounded 10-second device self-test.
  FORCED_RECALIBRATION,      ///< Perform explicitly confirmed FRC.
  PERSIST_SETTINGS,          ///< Persist dirty verified settings, when any.
  FACTORY_RESET,             ///< Perform explicitly confirmed factory reset.
  DIAGNOSTIC_READ_WORDS,     ///< Send an arbitrary command and CRC-read up to 3 words.
  DIAGNOSTIC_WRITE_COMMAND,  ///< Send an arbitrary command word.
  DIAGNOSTIC_WRITE_WORD,     ///< Send an arbitrary command plus CRC-protected word.
  READ_SENSOR_VARIANT        ///< Read the dedicated CRC-protected variant word.
};

/// Ownership state of the single operation/result slot.
enum class OperationState : uint8_t {
  IDLE = 0,      ///< No active operation or retained terminal result.
  ACTIVE,        ///< An operation is admitted and awaits owner polling.
  RESULT_PENDING///< One terminal result must be consumed.
};

/// Logical terminal classification independent of low-level error detail.
enum class OperationOutcome : uint8_t {
  SUCCEEDED = 0, ///< Operation completed successfully.
  NO_DATA,       ///< Valid terminal result, but no measurement was available.
  FAILED,        ///< Operation failed with no published partial composite result.
  CANCELLED,     ///< Owner cancelled future host work.
  TIMED_OUT,     ///< Immutable operation deadline expired.
  PARTIAL,       ///< A composite read published a completed-field subset.
  INDETERMINATE  ///< Hardware effect cannot be determined safely.
};

/// Best evidence about an operation's effect on sensor state.
enum class EffectState : uint8_t {
  NONE = 0,     ///< Operation has no effectful write semantics.
  NOT_ATTEMPTED,///< No effectful physical attempt began.
  ATTEMPTED,    ///< An effectful attempt began without acknowledgement proof.
  ACKNOWLEDGED, ///< Effectful bytes completed successfully.
  VERIFIED,     ///< A later read/reconciliation verified the intended state.
  UNKNOWN       ///< Conflicting or incomplete evidence prevents a safe claim.
};

/// Last state-machine phase observed in progress or at termination.
enum class OperationPhase : uint8_t {
  NONE = 0,            ///< No operation phase.
  WAIT_POWER_UP,       ///< Zero-I2C initial power-up wait.
  SEND_WAKE,           ///< Wake command attempt.
  WAIT_WAKE,           ///< Zero-I2C wake settle.
  SEND_STOP,           ///< Stop-periodic reconciliation attempt.
  WAIT_STOP,           ///< Zero-I2C 500 ms stop settle.
  SEND_COMMAND,        ///< Primary command or payload attempt.
  WAIT_EXECUTION,      ///< Zero-I2C sensor execution wait.
  SEND_READY_COMMAND,  ///< Data-ready command write.
  READ_READY_RESPONSE, ///< Data-ready response read.
  SEND_READ_COMMAND,   ///< Command write preceding a separate response read.
  READ_RESPONSE,       ///< Primary CRC-protected response read.
  SEND_VERIFY_COMMAND, ///< Readback command used to verify a mutation.
  READ_VERIFY_RESPONSE,///< Verification response read.
  READ_DEFERRED_RESULT ///< Deferred maintenance/conversion result read.
};

/// Explicit authority token required by sensitive operation builders.
enum class MaintenanceConfirmation : uint8_t {
  NONE = 0,            ///< No maintenance authority supplied.
  FORCED_RECALIBRATION,///< Authority for one FRC request.
  PERSIST_SETTINGS,    ///< Authority for one persistence request.
  FACTORY_RESET        ///< Authority for one factory-reset request.
};

/// Bit positions used by configuration provenance masks.
enum class ConfigurationField : uint16_t {
  NONE = 0,                    ///< No field.
  TEMPERATURE_OFFSET = 1U << 0,///< Temperature-offset compensation.
  SENSOR_ALTITUDE = 1U << 1,   ///< Altitude compensation.
  AMBIENT_PRESSURE = 1U << 2,  ///< Runtime ambient-pressure override.
  ASC_ENABLED = 1U << 3,       ///< ASC enable flag.
  ASC_TARGET = 1U << 4,        ///< ASC target concentration.
  ASC_INITIAL_PERIOD = 1U << 5,///< ASC initial period.
  ASC_STANDARD_PERIOD = 1U << 6///< ASC standard period.
};

/// Convert one configuration field to its mask value.
/// @param field Field enum value.
/// @return Underlying 16-bit mask.
inline constexpr uint16_t configurationFieldMask(ConfigurationField field) {
  return static_cast<uint16_t>(field);
}

/// Mask containing every field represented by `ConfigurationSnapshot`.
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

/// Bit flags describing which fixed sample values are usable.
enum SampleFlag : uint16_t {
  SAMPLE_NONE = 0,                  ///< No sample value is valid.
  SAMPLE_CO2_VALID = 1U << 0,       ///< `FixedSample::co2Ppm` is valid.
  SAMPLE_TEMPERATURE_VALID = 1U << 1,///< Temperature value is valid.
  SAMPLE_HUMIDITY_VALID = 1U << 2,  ///< Humidity value is valid.
  SAMPLE_FRESH = 1U << 3            ///< Sample was produced by the completed operation.
};

/// Correlation key assigned when a request is admitted.
struct OperationId {
  uint32_t requestId = 0; ///< Nonzero caller-supplied correlation value.
  uint32_t generation = 0; ///< Driver generation preventing stale ID reuse.
};

/// Compare both caller and driver parts of an operation identity.
/// @param lhs Left identity.
/// @param rhs Right identity.
/// @return `true` only when both fields match.
inline constexpr bool operator==(const OperationId& lhs, const OperationId& rhs) {
  return lhs.requestId == rhs.requestId && lhs.generation == rhs.generation;
}

/// Compare complete operation identities.
/// @param lhs Left identity.
/// @param rhs Right identity.
/// @return `true` when either field differs.
inline constexpr bool operator!=(const OperationId& lhs, const OperationId& rhs) {
  return !(lhs == rhs);
}

/// Owner-supplied identity and absolute timing contract for one request.
struct OperationOptions {
  /// Caller correlation value. Zero is invalid.
  uint32_t requestId = 0;
  /// Admission time in the owner's monotonic 32-bit millisecond clock.
  uint32_t nowMs = 0;
  /// Immutable absolute deadline in the same clock; must be within 2^31 ms.
  uint32_t deadlineMs = 0;
};

/// Fixed-size typed operation request.
///
/// Prefer the named builders for operations carrying values or maintenance
/// confirmation. `make()` is suitable for value-free operation kinds.
struct OperationRequest {
  OperationKind kind = OperationKind::NONE; ///< Requested typed operation.
  int32_t signedValue = 0; ///< Signed payload, currently temperature offset.
  uint32_t value = 0; ///< Unsigned setting, FRC reference, or diagnostic word.
  uint16_t command = 0; ///< Command word for a diagnostic request.
  uint8_t wordCount = 0; ///< Diagnostic read count in the supported range 1..3.
  MaintenanceConfirmation confirmation = MaintenanceConfirmation::NONE; ///< Authority token.

  /// Construct a value-free typed request.
  /// @param operation Operation kind to request.
  /// @return Request with zero payload and no maintenance confirmation.
  static OperationRequest make(OperationKind operation) {
    OperationRequest request;
    request.kind = operation;
    return request;
  }
  /// Construct a temperature-offset request in milli-degrees Celsius.
  /// @param valueMilliC Desired offset; validated during admission.
  /// @return Typed setting request.
  static OperationRequest setTemperatureOffsetMilliC(int32_t valueMilliC) {
    OperationRequest request = make(OperationKind::SET_TEMPERATURE_OFFSET);
    request.signedValue = valueMilliC;
    return request;
  }
  /// Construct a sensor-altitude request.
  /// @param altitudeM Desired altitude in meters.
  /// @return Typed setting request.
  static OperationRequest setSensorAltitudeM(uint16_t altitudeM) {
    OperationRequest request = make(OperationKind::SET_SENSOR_ALTITUDE);
    request.value = altitudeM;
    return request;
  }
  /// Construct a runtime ambient-pressure request.
  /// @param pressurePa Desired pressure in pascals.
  /// @return Typed setting request.
  static OperationRequest setAmbientPressurePa(uint32_t pressurePa) {
    OperationRequest request = make(OperationKind::SET_AMBIENT_PRESSURE);
    request.value = pressurePa;
    return request;
  }
  /// Construct an ASC enable request.
  /// @param enabled Desired enable state.
  /// @return Typed setting request.
  static OperationRequest setAscEnabled(bool enabled) {
    OperationRequest request = make(OperationKind::SET_ASC_ENABLED);
    request.value = enabled ? 1U : 0U;
    return request;
  }
  /// Construct an ASC target request.
  /// @param ppm Target concentration in ppm.
  /// @return Typed setting request.
  static OperationRequest setAscTargetPpm(uint16_t ppm) {
    OperationRequest request = make(OperationKind::SET_ASC_TARGET);
    request.value = ppm;
    return request;
  }
  /// Construct an ASC initial-period request.
  /// @param hours Desired period in hours; validated against the device step.
  /// @return Typed setting request.
  static OperationRequest setAscInitialPeriodHours(uint16_t hours) {
    OperationRequest request = make(OperationKind::SET_ASC_INITIAL_PERIOD);
    request.value = hours;
    return request;
  }
  /// Construct an ASC standard-period request.
  /// @param hours Desired period in hours; validated against the device step.
  /// @return Typed setting request.
  static OperationRequest setAscStandardPeriodHours(uint16_t hours) {
    OperationRequest request = make(OperationKind::SET_ASC_STANDARD_PERIOD);
    request.value = hours;
    return request;
  }
  /// Construct an explicitly confirmed forced-recalibration request.
  /// @param referencePpm Known stable reference concentration in ppm.
  /// @return Confirmed maintenance request.
  static OperationRequest forcedRecalibration(uint16_t referencePpm) {
    OperationRequest request = make(OperationKind::FORCED_RECALIBRATION);
    request.value = referencePpm;
    request.confirmation = MaintenanceConfirmation::FORCED_RECALIBRATION;
    return request;
  }
  /// @return Explicitly confirmed persistence request.
  static OperationRequest persistSettings() {
    OperationRequest request = make(OperationKind::PERSIST_SETTINGS);
    request.confirmation = MaintenanceConfirmation::PERSIST_SETTINGS;
    return request;
  }
  /// @return Explicitly confirmed factory-reset request.
  static OperationRequest factoryReset() {
    OperationRequest request = make(OperationKind::FACTORY_RESET);
    request.confirmation = MaintenanceConfirmation::FACTORY_RESET;
    return request;
  }
  /// Construct a diagnostic command followed by a CRC-protected word read.
  /// @param cmdWord Arbitrary 16-bit command word.
  /// @param count Number of returned words in the supported range 1..3.
  /// @return Diagnostic request; execution invalidates managed state.
  static OperationRequest diagnosticReadWords(uint16_t cmdWord, uint8_t count) {
    OperationRequest request = make(OperationKind::DIAGNOSTIC_READ_WORDS);
    request.command = cmdWord;
    request.wordCount = count;
    return request;
  }
  /// Construct an arbitrary diagnostic command write.
  /// @param cmdWord Arbitrary 16-bit command word.
  /// @return Diagnostic request; execution invalidates managed state.
  static OperationRequest diagnosticWriteCommand(uint16_t cmdWord) {
    OperationRequest request = make(OperationKind::DIAGNOSTIC_WRITE_COMMAND);
    request.command = cmdWord;
    return request;
  }
  /// Construct an arbitrary diagnostic command plus one CRC-protected word.
  /// @param cmdWord Arbitrary 16-bit command word.
  /// @param dataWord Data word appended with Sensirion CRC-8 by the driver.
  /// @return Diagnostic request; execution invalidates managed state.
  static OperationRequest diagnosticWriteWord(uint16_t cmdWord, uint16_t dataWord) {
    OperationRequest request = make(OperationKind::DIAGNOSTIC_WRITE_WORD);
    request.command = cmdWord;
    request.value = dataWord;
    return request;
  }
};

/// Worst-case scheduling metadata for one `OperationKind`.
struct OperationLimits {
  OperationClass operationClass = OperationClass::RUNTIME; ///< Policy/scheduling class.
  uint8_t maxCallbacks = 0; ///< Maximum physical callback attempts after admission.
  uint8_t maxRetries = 0; ///< Hidden retry count; always zero in this library.
  uint32_t maxWaitMs = 0; ///< Post-admission wait; excludes carried safety gates.
  bool writesNonvolatile = false; ///< Operation can change nonvolatile state.
  bool destructive = false; ///< Operation can reset or irreversibly alter state.
};

/// Verified device identity and its cache epoch.
struct Identity {
  uint64_t serialNumber = 0; ///< 48-bit serial number in the low bits.
  SensorVariant variant = SensorVariant::UNKNOWN; ///< Decoded family variant.
  uint32_t sensorEpoch = 0; ///< Epoch in which this identity was verified.
  bool valid = false; ///< `true` only after a complete CRC-valid identity read.
  uint16_t variantWord = 0; ///< Complete CRC-verified `get_sensor_variant` word.
};

/// Raw and decoded result of `READ_DATA_READY`.
struct DataReadyStatus {
  uint16_t raw = 0; ///< Complete CRC-verified status word.
  bool ready = false; ///< `(raw & 0x07FF) != 0`.
};

/// Allocation-free fixed-point measurement with explicit provenance.
struct FixedSample {
  uint16_t co2Ppm = 0; ///< CO2 concentration in parts per million.
  int32_t temperatureMilliC = 0; ///< Temperature in milli-degrees Celsius.
  uint32_t humidityMilliPercent = 0; ///< Relative humidity in milli-percent RH.
  uint32_t capturedAtMs = 0; ///< Owner-clock time when the response completed.
  uint32_t sensorEpoch = 0; ///< Sensor/cache epoch that produced this sample.
  uint32_t sequence = 0; ///< 1-based count since the latest epoch/mode change.
  OperatingMode mode = OperatingMode::UNKNOWN; ///< Measurement mode provenance.
  uint16_t flags = SAMPLE_NONE; ///< Bitwise `SampleFlag` validity/freshness mask.
};

/// Cached configuration values with per-field verification and persistence truth.
struct ConfigurationSnapshot {
  int32_t temperatureOffsetMilliC = 0; ///< Temperature offset in milli-degrees Celsius.
  uint16_t sensorAltitudeM = 0; ///< Altitude compensation in meters.
  uint32_t ambientPressurePa = 0; ///< Runtime ambient pressure in pascals.
  bool ascEnabled = false; ///< Automatic self-calibration enable state.
  uint16_t ascTargetPpm = 0; ///< ASC target concentration in ppm.
  uint16_t ascInitialPeriodHours = 0; ///< ASC initial period in hours.
  uint16_t ascStandardPeriodHours = 0; ///< ASC standard period in hours.
  uint16_t verifiedMask = 0; ///< Fields read back successfully from runtime state.
  uint16_t dirtyMask = 0; ///< Changed/unpersisted EEPROM fields; excludes pressure.
                          ///< A field can be both verified and dirty.
  uint32_t sensorEpoch = 0; ///< Epoch in which these cache facts were observed.
  bool persistenceIndeterminate = false; ///< Possible EEPROM effect blocks blind persist.
};

/// Cache-only view of lifecycle, scheduling, and reconciliation state.
struct RuntimeSnapshot {
  bool bound = false; ///< Configuration is valid and copied.
  bool attached = false; ///< Current epoch has a verified supported identity.
  DriverState driverState = DriverState::UNINIT; ///< Passive transport-health state.
  OperatingMode operatingMode = OperatingMode::UNKNOWN; ///< Mode model.
  ModeEvidence modeEvidence = ModeEvidence::UNKNOWN; ///< Confidence in mode model.
  OperationState operationState = OperationState::IDLE; ///< Slot ownership state.
  OperationId operationId = {}; ///< Active or retained operation identity.
  OperationKind operationKind = OperationKind::NONE; ///< Active or retained kind.
  uint32_t nextDueMs = 0; ///< Earliest useful poll time for active work.
  uint32_t nextSafeCommandMs = 0; ///< Earliest safe admission when validity is true.
  bool nextSafeCommandValid = false; ///< Whether `nextSafeCommandMs` is meaningful.
  uint32_t sensorEpoch = 0; ///< Current cache/provenance epoch.
  bool reconciliationRequired = true; ///< `ATTACH` is required before managed work.
  bool sampleAvailable = false; ///< A valid latest sample can be peeked.
};

/// Passive telemetry separated into transfer, protocol, and logical job channels.
///
/// Counters saturate at their integer maximum. They never block admission or
/// perform recovery.
struct HealthSnapshot {
  DriverState state = DriverState::UNINIT; ///< Derived passive driver state.
  uint32_t lastTransferOkMs = 0; ///< Completion time of latest successful attempt.
  uint32_t lastTransferErrorMs = 0; ///< Completion time of latest failed attempt.
  Status lastTransferError = Status::Ok(); ///< Latest mapped transport failure.
  uint8_t consecutiveTransferFailures = 0; ///< Attempt failures since last success.
  uint32_t totalTransferSuccess = 0; ///< Lifetime successful physical attempts.
  uint32_t totalTransferFailures = 0; ///< Lifetime failed physical attempts.
  uint32_t expectedNacks = 0; ///< Accepted NACKs in explicitly marked phases.
  uint32_t totalProtocolFailures = 0; ///< Lifetime CRC/contract/protocol failures.
  uint32_t totalCrcFailures = 0; ///< Protocol failures specifically caused by CRC.
  uint32_t lastProtocolErrorMs = 0; ///< Owner-clock time of latest protocol failure.
  Status lastProtocolError = Status::Ok(); ///< Latest protocol-layer failure.
  uint32_t totalOperationSuccess = 0; ///< Successful plus no-data terminal jobs.
  uint32_t totalOperationFailures = 0; ///< Failed/timeout/partial/indeterminate jobs.
  uint32_t totalOperationCancelled = 0; ///< Cancelled terminal jobs.
  uint32_t lastOperationErrorMs = 0; ///< Completion time of latest logical error.
  Status lastOperationError = Status::Ok(); ///< Latest non-success operation status.
  OperationId lastOperationErrorId = {}; ///< Identity correlated to logical error.
  OperationKind lastOperationErrorKind = OperationKind::NONE; ///< Kind correlated to error.
  OperationId lastOperationId = {}; ///< Identity of latest completion of any outcome.
  OperationKind lastOperationKind = OperationKind::NONE; ///< Kind of latest completion.
};

/// Fixed result storage. Interpret members by `OperationResult::kind`:
/// - sample operations -> `sample`
/// - attach/identity/wake/reinit -> `identity`
/// - sensor-variant read -> `identity` and the raw word in `value`
/// - factory reset -> `identity` and `configuration`
/// - setting reads -> their scalar member plus `configuration`
/// - setting writes/configuration/persistence -> `configuration`
/// - data-ready -> `dataReady`
/// - temperature offset/FRC -> `signedValue`
/// - altitude/pressure/ASC numeric/self-test -> `value`
/// - ASC enabled -> `boolValue`
/// - diagnostic reads and raw maintenance responses -> `rawWords`/`wordCount`
struct OperationValue {
  FixedSample sample = {}; ///< Sample-operation payload.
  Identity identity = {}; ///< Attach/identity/wake/reset verification payload.
  ConfigurationSnapshot configuration = {}; ///< Setting/config/persistence payload.
  DataReadyStatus dataReady = {}; ///< Data-ready payload.
  int32_t signedValue = 0; ///< Temperature-offset or FRC signed result.
  uint32_t value = 0; ///< Altitude/pressure/ASC numeric/self-test result.
  uint16_t rawWords[3] = {}; ///< CRC-verified diagnostic/raw words.
  uint8_t wordCount = 0; ///< Number of valid entries in `rawWords`.
  bool boolValue = false; ///< ASC enabled read result.
};

/// Exactly-once terminal record retained until consumed by matching identity.
struct OperationResult {
  OperationId id = {}; ///< Complete caller/driver correlation identity.
  OperationKind kind = OperationKind::NONE; ///< Completed operation kind.
  OperationOutcome outcome = OperationOutcome::FAILED; ///< Logical terminal class.
  EffectState effect = EffectState::NONE; ///< Best hardware-effect evidence.
  Status status = Status::Error(Err::RESULT_NOT_READY, "No result"); ///< Detail status.
  OperationPhase finalPhase = OperationPhase::NONE; ///< Last reached phase.
  uint32_t startedMs = 0; ///< Immutable owner-clock admission time.
  uint32_t completedMs = 0; ///< Owner/callback clock terminal time.
  uint32_t deadlineMs = 0; ///< Immutable absolute deadline.
  uint32_t sensorEpoch = 0; ///< Epoch at terminal publication.
  OperatingMode operatingMode = OperatingMode::UNKNOWN; ///< Mode at termination.
  ModeEvidence modeEvidence = ModeEvidence::UNKNOWN; ///< Confidence in final mode.
  uint16_t completedFieldMask = 0; ///< Composite fields completed before termination.
  uint8_t callbacksUsed = 0; ///< Physical attempts consumed by this operation.
  bool reconciliationRequired = false; ///< Managed work requires a new attach/reconcile.
  OperationValue value = {}; ///< Fixed storage interpreted according to `kind`.
};

/// Progress returned by one bounded owner call to `SCD41::poll()`.
struct PollResult {
  OperationState state = OperationState::IDLE; ///< Slot state after this call.
  OperationId id = {}; ///< Active or retained identity, when present.
  OperationKind kind = OperationKind::NONE; ///< Active or retained operation kind.
  Status status = Status::Ok(); ///< Progress/admission/terminal status.
  uint32_t nextDueMs = 0; ///< Earliest useful poll time for active work.
  uint8_t callbacksUsed = 0; ///< Physical attempts used by this poll call.
};

/// Fixed-memory SCD41 protocol driver advanced only by an external owner.
///
/// An instance is neither thread-safe nor ISR-safe. The application must
/// serialize calls and guarantee that the non-owning transport configuration
/// remains valid until a successful rebind or `end()`.
class SCD41 {
public:
  SCD41() = default;
  SCD41(const SCD41&) = delete;
  SCD41& operator=(const SCD41&) = delete;
  SCD41(SCD41&&) = delete;
  SCD41& operator=(SCD41&&) = delete;

  /// Validate and copy the non-owning transport configuration. Performs no I2C.
  /// @param config Callback, context, and bounded transport policy to copy.
  /// @return `OK`, `INVALID_CONFIG`, or `BUSY` when a slot/result is retained.
  Status begin(const Config& config);
  /// Advance the one active operation.
  /// @param nowMs Current time in the owner's wrapping 32-bit clock domain.
  /// @param maxCallbacks Hard callback-attempt budget for this call; zero is valid.
  /// @return Current slot state, progress status, callback use, and next due time.
  PollResult poll(uint32_t nowMs, uint8_t maxCallbacks = 1);
  /// Compatibility executor; delegates to `poll(nowMs, 1)`.
  /// @param nowMs Current time in the owner clock domain.
  /// @return The status from the delegated poll.
  Status tick(uint32_t nowMs);
  /// Start one typed operation without invoking transport.
  /// @param request Fixed typed operation request.
  /// @param options Nonzero request identity and valid absolute deadline.
  /// @param assignedId Receives the complete identity only when work is admitted.
  /// @return `IN_PROGRESS` on admission, otherwise a zero-I2C rejection status.
  Status start(const OperationRequest& request, const OperationOptions& options,
               OperationId& assignedId);
  /// Cancel active host work without I2C and retain one terminal result.
  /// @param id Exact active operation identity.
  /// @param nowMs Cancellation time in the owner clock domain.
  /// @return `OK` when cancelled, or an identity/state/clock error.
  Status cancel(const OperationId& id, uint32_t nowMs);
  /// Copy and consume the matching terminal result exactly once. Performs no I2C.
  /// @param expectedId Exact retained operation identity.
  /// @param out Destination; unchanged when no matching result exists.
  /// @return `OK`, `RESULT_NOT_READY`, or `STALE_RESULT`.
  Status takeResult(const OperationId& expectedId, OperationResult& out);
  /// Unbind without I2C. Active work becomes a retained cancelled result.
  ///
  /// A retained result must still be consumed before `begin()` can bind again.
  void end();

  /// @return Whether a valid configuration is currently bound.
  bool isBound() const { return _bound; }
  /// @return Whether identity was verified in the current sensor epoch.
  bool isAttached() const { return _attached; }
  /// @return Passive transport-health state.
  DriverState state() const { return _driverState; }
  /// @return Compatibility alias for `state()`.
  DriverState driverState() const { return state(); }
  /// @return Ownership state of the single operation/result slot.
  OperationState operationState() const;

  /// @return Cache-only lifecycle and scheduling snapshot; performs no I2C.
  RuntimeSnapshot runtimeSnapshot() const;
  /// @return Cache-only passive health telemetry; performs no I2C.
  HealthSnapshot healthSnapshot() const { return _health; }
  /// @return Cache-only configuration/provenance snapshot; performs no I2C.
  ConfigurationSnapshot configurationSnapshot() const { return _configuration; }
  /// @return Cache-only verified identity; inspect `Identity::valid`.
  Identity identity() const { return _identity; }
  /// Copy the latest cached sample without consuming it or performing I2C.
  /// @param out Destination; unchanged when no valid cached sample exists.
  /// @return `OK` or `RESULT_NOT_READY`.
  Status peekLatestSample(FixedSample& out) const;

  /// Get authoritative worst-case metadata for an operation kind.
  /// @param kind Operation kind to classify.
  /// @return Fixed callback, wait, retry, nonvolatile, and destructive limits.
  static OperationLimits limits(OperationKind kind);
  /// Decode the SCD4x data-ready mask.
  /// @param rawStatus Complete CRC-verified status word.
  /// @return `(rawStatus & 0x07FF) != 0`.
  static bool isDataReady(uint16_t rawStatus) {
    return (rawStatus & cmd::DATA_READY_MASK) != 0;
  }
  /// Convert a raw temperature word to degrees Celsius.
  /// @param raw Complete 16-bit sensor word.
  /// @return Temperature using the datasheet floating-point formula.
  static float convertTemperatureC(uint16_t raw);
  /// Convert a raw humidity word to percent relative humidity.
  /// @param raw Complete 16-bit sensor word.
  /// @return Relative humidity using the datasheet floating-point formula.
  static float convertHumidityPct(uint16_t raw);
  /// Convert a raw temperature word to milli-degrees Celsius.
  /// @param raw Complete 16-bit sensor word.
  /// @return Deterministic fixed-point temperature.
  static int32_t convertTemperatureMilliC(uint16_t raw);
  /// Convert a raw humidity word to milli-percent relative humidity.
  /// @param raw Complete 16-bit sensor word.
  /// @return Deterministic fixed-point relative humidity.
  static uint32_t convertHumidityMilliPercent(uint16_t raw);
  /// Encode a finite temperature offset in the supported 0..20 C range.
  /// @param offsetC Offset in degrees Celsius.
  /// @param out Encoded word; unchanged when validation fails.
  /// @return `OK` or `INVALID_PARAM`.
  static Status encodeTemperatureOffsetC(float offsetC, uint16_t& out);
  /// Encode a temperature offset in the supported 0..20000 milli-C range.
  /// @param offsetMilliC Offset in milli-degrees Celsius.
  /// @param out Encoded word; unchanged when validation fails.
  /// @return `OK` or `INVALID_PARAM`.
  static Status encodeTemperatureOffsetMilliC(int32_t offsetMilliC, uint16_t& out);
  /// Decode an offset word to degrees Celsius.
  /// @param raw Complete 16-bit encoded offset.
  /// @return Decoded floating-point offset.
  static float decodeTemperatureOffsetC(uint16_t raw);
  /// Decode an offset word to milli-degrees Celsius.
  /// @param raw Complete 16-bit encoded offset.
  /// @return Rounded fixed-point offset.
  static int32_t decodeTemperatureOffsetMilliC(uint16_t raw);
  /// Encode ambient pressure in the supported 70000..120000 Pa range.
  /// @param pressurePa Pressure in pascals.
  /// @param out Encoded `pressurePa / 100` word; unchanged on validation failure.
  /// @return `OK` or `INVALID_PARAM`.
  static Status encodeAmbientPressurePa(uint32_t pressurePa, uint16_t& out);
  /// Decode an ambient-pressure word to pascals.
  /// @param raw Complete pressure word in hPa.
  /// @return Pressure in pascals.
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
  static SensorVariant _variantFromVariantWord(uint16_t variantWord);
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
