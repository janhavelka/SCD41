/// @file test_basic.cpp
/// @brief Public-contract tests for the bounded SCD41 operation engine.

#include <unity.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "SCD41/SCD41.h"
#include "common/DiagnosticWorkflow.h"

using namespace SCD41;
using Device = SCD41::SCD41;

static_assert(!std::is_copy_constructible<Device>::value, "driver must not be copied");
static_assert(!std::is_copy_assignable<Device>::value, "driver must not be copy assigned");
static_assert(!std::is_move_constructible<Device>::value, "driver must not be moved");
static_assert(!std::is_move_assignable<Device>::value, "driver must not be move assigned");
static_assert(std::is_standard_layout<OperationId>::value, "operation IDs cross owner boundaries");
static_assert(std::is_trivially_copyable<OperationId>::value, "operation IDs must be fixed values");
static_assert(std::is_standard_layout<OperationRequest>::value, "requests must be fixed values");
static_assert(std::is_trivially_copyable<OperationRequest>::value, "requests must be copyable values");
static_assert(std::is_standard_layout<PollResult>::value, "poll results must be fixed values");
static_assert(std::is_trivially_copyable<PollResult>::value, "poll results must be copyable values");
static_assert(std::is_standard_layout<OperationResult>::value, "results must be fixed values");
static_assert(std::is_trivially_copyable<OperationResult>::value, "results must be copyable values");
static_assert(std::is_standard_layout<FixedSample>::value, "samples must be fixed values");
static_assert(std::is_trivially_copyable<FixedSample>::value, "samples must be copyable values");
static_assert(sizeof(OperationRequest) <= 24, "review operation request growth");
static_assert(sizeof(PollResult) <= 40, "review poll result growth");
static_assert(sizeof(FixedSample) <= 32, "review sample growth");
static_assert(sizeof(OperationResult) <= 256, "review terminal result growth");
static_assert(sizeof(Device) <= 1024, "review driver fixed-memory growth");
static_assert(static_cast<uint8_t>(OperationKind::DIAGNOSTIC_WRITE_WORD) == 34U,
              "existing operation IDs are append-only");
static_assert(static_cast<uint8_t>(OperationKind::READ_SENSOR_VARIANT) == 35U,
              "new operations must be appended");

namespace {

constexpr size_t MAX_TRACE = 128;

uint8_t crc8(const uint8_t* data, size_t length) {
  uint8_t crc = cmd::CRC_INIT;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) != 0U
                ? static_cast<uint8_t>((crc << 1U) ^ cmd::CRC_POLY)
                : static_cast<uint8_t>(crc << 1U);
    }
  }
  return crc;
}

void packWord(uint16_t word, uint8_t* out) {
  out[0] = static_cast<uint8_t>(word >> 8U);
  out[1] = static_cast<uint8_t>(word & 0xFFU);
  out[2] = crc8(out, 2);
}

uint16_t commandOf(const TransferRequest& request) {
  if (request.writeLength < 2 || request.writeData == nullptr) {
    return 0;
  }
  return static_cast<uint16_t>((static_cast<uint16_t>(request.writeData[0]) << 8U) |
                               request.writeData[1]);
}

uint16_t payloadWordOf(const TransferRequest& request) {
  if (request.writeLength < 5 || request.writeData == nullptr) {
    return 0;
  }
  return static_cast<uint16_t>((static_cast<uint16_t>(request.writeData[2]) << 8U) |
                               request.writeData[3]);
}

bool timeReached(uint32_t nowMs, uint32_t targetMs) {
  return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

enum class ModelMode : uint8_t {
  IDLE,
  PERIODIC,
  LOW_POWER_PERIODIC,
  POWER_DOWN
};

struct TransferTrace {
  uint16_t command = 0;
  size_t writeLength = 0;
  size_t readLength = 0;
  uint32_t timeoutMs = 0;
  TransferIntent intent = TransferIntent::NORMAL;
};

struct FaultRule {
  bool enabled = false;
  size_t relativeCall = 0;
  TransferCode code = TransferCode::TIMEOUT;
  TransferDisposition disposition = TransferDisposition::INDETERMINATE;
  int32_t detail = 0;
  bool applyHardwareEffect = false;
  bool fillReadBuffer = false;
  uint32_t completedMs = 0;
};

struct ModelTransport {
  TransferTrace trace[MAX_TRACE] = {};
  size_t calls = 0;
  size_t operationCallBase = 0;
  uint32_t callbackCompletedMs = 0;
  bool present = true;
  bool badCrc = false;
  uint8_t badCrcWord = 0;
  uint16_t badCrcCommand = 0;
  ModelMode mode = ModelMode::IDLE;
  uint16_t pendingResponseCommand = 0;
  uint16_t serialWords[3] = {0xF896, 0x9F07, 0x3BBE};
  uint16_t variantWord = 0x1440;
  uint16_t measurementWords[3] = {600, 20000, 30000};
  uint16_t dataReadyWord = 1;
  uint16_t temperatureOffsetRaw = 0;
  uint16_t sensorAltitudeM = 0;
  uint16_t ambientPressureRaw = 1013;
  uint16_t ascEnabled = 1;
  uint16_t ascTarget = 400;
  uint16_t ascInitialPeriod = 44;
  uint16_t ascStandardPeriod = 156;
  uint16_t selfTestResult = 0;
  uint16_t frcResult = 0x8005;
  uint16_t diagnosticWords[3] = {0x1111, 0x2222, 0x3333};
  uint32_t persistWrites = 0;
  uint32_t factoryResetWrites = 0;
  FaultRule fault = {};
};

void resetOperationTrace(ModelTransport& bus) {
  bus.operationCallBase = bus.calls;
  bus.fault = FaultRule{};
}

size_t operationCalls(const ModelTransport& bus) {
  return bus.calls - bus.operationCallBase;
}

void faultRelativeCall(ModelTransport& bus, size_t relativeCall, TransferCode code,
                       TransferDisposition disposition, bool applyHardwareEffect = false,
                       bool fillReadBuffer = false, uint32_t completedMs = 0) {
  bus.fault.enabled = true;
  bus.fault.relativeCall = relativeCall;
  bus.fault.code = code;
  bus.fault.disposition = disposition;
  bus.fault.detail = 77;
  bus.fault.applyHardwareEffect = applyHardwareEffect;
  bus.fault.fillReadBuffer = fillReadBuffer;
  bus.fault.completedMs = completedMs;
}

bool isReadCommand(uint16_t command) {
  switch (command) {
    case cmd::CMD_GET_SERIAL_NUMBER:
    case cmd::CMD_GET_SENSOR_VARIANT:
    case cmd::CMD_GET_DATA_READY_STATUS:
    case cmd::CMD_READ_MEASUREMENT:
    case cmd::CMD_GET_TEMPERATURE_OFFSET:
    case cmd::CMD_GET_SENSOR_ALTITUDE:
    case cmd::CMD_GET_AMBIENT_PRESSURE:
    case cmd::CMD_GET_ASC_ENABLED:
    case cmd::CMD_GET_ASC_TARGET:
    case cmd::CMD_GET_ASC_INITIAL_PERIOD:
    case cmd::CMD_GET_ASC_STANDARD_PERIOD:
      return true;
    default:
      return false;
  }
}

void applyWrite(ModelTransport& bus, const TransferRequest& request) {
  const uint16_t command = commandOf(request);
  // Some SCD41 commands (notably ambient pressure) use the same command word
  // for a two-byte read request and a five-byte write-with-payload request.
  // Model the wire shape as well as the command value so a setter cannot be
  // mistaken for a response-selection write.
  if (request.writeLength == 2 && isReadCommand(command)) {
    bus.pendingResponseCommand = command;
    return;
  }

  switch (command) {
    case cmd::CMD_WAKE_UP:
      bus.mode = ModelMode::IDLE;
      break;
    case cmd::CMD_STOP_PERIODIC_MEASUREMENT:
      bus.mode = ModelMode::IDLE;
      break;
    case cmd::CMD_START_PERIODIC_MEASUREMENT:
      bus.mode = ModelMode::PERIODIC;
      break;
    case cmd::CMD_START_LOW_POWER_PERIODIC_MEASUREMENT:
      bus.mode = ModelMode::LOW_POWER_PERIODIC;
      break;
    case cmd::CMD_POWER_DOWN:
      bus.mode = ModelMode::POWER_DOWN;
      break;
    case cmd::CMD_SET_TEMPERATURE_OFFSET:
      bus.temperatureOffsetRaw = payloadWordOf(request);
      break;
    case cmd::CMD_SET_SENSOR_ALTITUDE:
      bus.sensorAltitudeM = payloadWordOf(request);
      break;
    case cmd::CMD_SET_AMBIENT_PRESSURE:
      bus.ambientPressureRaw = payloadWordOf(request);
      break;
    case cmd::CMD_SET_ASC_ENABLED:
      bus.ascEnabled = payloadWordOf(request);
      break;
    case cmd::CMD_SET_ASC_TARGET:
      bus.ascTarget = payloadWordOf(request);
      break;
    case cmd::CMD_SET_ASC_INITIAL_PERIOD:
      bus.ascInitialPeriod = payloadWordOf(request);
      break;
    case cmd::CMD_SET_ASC_STANDARD_PERIOD:
      bus.ascStandardPeriod = payloadWordOf(request);
      break;
    case cmd::CMD_PERFORM_SELF_TEST:
    case cmd::CMD_PERFORM_FORCED_RECALIBRATION:
      bus.pendingResponseCommand = command;
      break;
    case cmd::CMD_PERSIST_SETTINGS:
      ++bus.persistWrites;
      break;
    case cmd::CMD_PERFORM_FACTORY_RESET:
      ++bus.factoryResetWrites;
      bus.temperatureOffsetRaw = 0;
      bus.sensorAltitudeM = 0;
      bus.ambientPressureRaw = 1013;
      bus.ascEnabled = 1;
      bus.ascTarget = 400;
      bus.ascInitialPeriod = 44;
      bus.ascStandardPeriod = 156;
      bus.mode = ModelMode::IDLE;
      break;
    case cmd::CMD_REINIT:
      bus.mode = ModelMode::IDLE;
      break;
    default:
      if (request.writeLength == 2U) {
        bus.pendingResponseCommand = command;
      }
      break;
  }
}

uint8_t responseWordCount(uint16_t command) {
  return (command == cmd::CMD_GET_SERIAL_NUMBER || command == cmd::CMD_READ_MEASUREMENT) ? 3 : 1;
}

void fillResponse(ModelTransport& bus, uint8_t* out, size_t length) {
  TEST_ASSERT_NOT_NULL(out);
  const uint8_t wordCount = responseWordCount(bus.pendingResponseCommand);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(wordCount) * 3U, length);

  uint16_t words[3] = {};
  switch (bus.pendingResponseCommand) {
    case cmd::CMD_GET_SERIAL_NUMBER:
      std::memcpy(words, bus.serialWords, sizeof(words));
      break;
    case cmd::CMD_GET_SENSOR_VARIANT:
      words[0] = bus.variantWord;
      break;
    case cmd::CMD_GET_DATA_READY_STATUS:
      words[0] = bus.dataReadyWord;
      break;
    case cmd::CMD_READ_MEASUREMENT:
      std::memcpy(words, bus.measurementWords, sizeof(words));
      break;
    case cmd::CMD_GET_TEMPERATURE_OFFSET:
      words[0] = bus.temperatureOffsetRaw;
      break;
    case cmd::CMD_GET_SENSOR_ALTITUDE:
      words[0] = bus.sensorAltitudeM;
      break;
    case cmd::CMD_GET_AMBIENT_PRESSURE:
      words[0] = bus.ambientPressureRaw;
      break;
    case cmd::CMD_GET_ASC_ENABLED:
      words[0] = bus.ascEnabled;
      break;
    case cmd::CMD_GET_ASC_TARGET:
      words[0] = bus.ascTarget;
      break;
    case cmd::CMD_GET_ASC_INITIAL_PERIOD:
      words[0] = bus.ascInitialPeriod;
      break;
    case cmd::CMD_GET_ASC_STANDARD_PERIOD:
      words[0] = bus.ascStandardPeriod;
      break;
    case cmd::CMD_PERFORM_SELF_TEST:
      words[0] = bus.selfTestResult;
      break;
    case cmd::CMD_PERFORM_FORCED_RECALIBRATION:
      words[0] = bus.frcResult;
      break;
    default:
      if (bus.pendingResponseCommand == 0x1234U ||
          bus.pendingResponseCommand == 0x4321U) {
        std::memcpy(words, bus.diagnosticWords, sizeof(words));
      } else {
        TEST_FAIL_MESSAGE("unexpected response phase");
      }
      break;
  }

  for (uint8_t i = 0; i < wordCount; ++i) {
    packWord(words[i], &out[static_cast<size_t>(i) * 3U]);
  }
  if (bus.badCrc && bus.badCrcWord < wordCount &&
      (bus.badCrcCommand == 0U ||
       bus.badCrcCommand == bus.pendingResponseCommand)) {
    out[static_cast<size_t>(bus.badCrcWord) * 3U + 2U] ^= 0x5AU;
  }
}

TransferResult modelTransfer(const TransferRequest& request, void* user) {
  auto& bus = *static_cast<ModelTransport*>(user);
  TEST_ASSERT_TRUE(bus.calls < MAX_TRACE);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDRESS, request.address);
  TEST_ASSERT_TRUE(request.timeoutMs > 0);

  TransferTrace& trace = bus.trace[bus.calls];
  trace.command = commandOf(request);
  trace.writeLength = request.writeLength;
  trace.readLength = request.readLength;
  trace.timeoutMs = request.timeoutMs;
  trace.intent = request.intent;
  ++bus.calls;

  const size_t relativeCall = bus.calls - bus.operationCallBase;
  const bool faulted = bus.fault.enabled && relativeCall == bus.fault.relativeCall;

  if (!bus.present) {
    return TransferResult{TransferCode::NACK, TransferDisposition::NO_EFFECT, 0, 0,
                          bus.callbackCompletedMs};
  }

  if (faulted) {
    if (bus.fault.applyHardwareEffect && request.writeLength != 0) {
      applyWrite(bus, request);
    }
    if (bus.fault.fillReadBuffer && request.readLength != 0) {
      fillResponse(bus, request.readData, request.readLength);
    }
    const size_t transferred =
        bus.fault.code == TransferCode::OK ? request.writeLength + request.readLength : 0U;
    return TransferResult{bus.fault.code, bus.fault.disposition, bus.fault.detail, transferred,
                          bus.fault.completedMs != 0 ? bus.fault.completedMs
                                                    : bus.callbackCompletedMs};
  }

  if (request.writeLength != 0) {
    TEST_ASSERT_NOT_NULL(request.writeData);
    TEST_ASSERT_TRUE(request.writeLength == 2 || request.writeLength == 5);
    if (request.writeLength == 5) {
      TEST_ASSERT_EQUAL_HEX8(crc8(&request.writeData[2], 2), request.writeData[4]);
    }
    applyWrite(bus, request);
    if (commandOf(request) == cmd::CMD_WAKE_UP) {
      TEST_ASSERT_EQUAL(static_cast<uint8_t>(TransferIntent::EXPECTED_WRITE_NACK),
                        static_cast<uint8_t>(request.intent));
      return TransferResult{TransferCode::NACK, TransferDisposition::INDETERMINATE, 0, 0,
                            bus.callbackCompletedMs};
    }
    return TransferResult::Ok(request.writeLength, bus.callbackCompletedMs);
  }

  TEST_ASSERT_EQUAL_UINT32(0U, request.writeLength);
  TEST_ASSERT_TRUE(request.readLength != 0);
  fillResponse(bus, request.readData, request.readLength);
  return TransferResult::Ok(request.readLength, bus.callbackCompletedMs);
}

Config makeConfig(ModelTransport& bus) {
  Config config;
  config.transfer = modelTransfer;
  config.transferUser = &bus;
  config.transferTimeoutMs = 20;
  config.powerUpDelayMs = cmd::EXECUTION_TIME_POWER_UP_MS;
  config.offlineThreshold = 3;
  return config;
}

void bindDevice(Device& device, ModelTransport& bus) {
  TEST_ASSERT_TRUE(device.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.isBound());
  TEST_ASSERT_FALSE(device.isAttached());
}

OperationId startJob(Device& device, const OperationRequest& request, uint32_t nowMs,
                     uint32_t deadlineMs, uint32_t requestId = 1) {
  OperationId id = {};
  const Status status = device.start(request, OperationOptions{requestId, nowMs, deadlineMs}, id);
  TEST_ASSERT_TRUE_MESSAGE(status.inProgress(), status.msg);
  TEST_ASSERT_EQUAL_UINT32(requestId, id.requestId);
  TEST_ASSERT_TRUE(id.generation != 0);
  return id;
}

PollResult pollChecked(Device& device, ModelTransport& bus, uint32_t nowMs,
                       uint8_t budget = 1) {
  bus.callbackCompletedMs = nowMs;
  const size_t before = bus.calls;
  const PollResult poll = device.poll(nowMs, budget);
  const size_t used = bus.calls - before;
  TEST_ASSERT_TRUE(used <= budget);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(used), poll.callbacksUsed);
  return poll;
}

PollResult driveUntilTerminal(Device& device, ModelTransport& bus, uint32_t& nowMs,
                              uint8_t budget = 1, uint16_t maxPolls = 128) {
  PollResult poll = {};
  for (uint16_t i = 0; i < maxPolls; ++i) {
    poll = pollChecked(device, bus, nowMs, budget);
    if (poll.state == OperationState::RESULT_PENDING) {
      return poll;
    }
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationState::ACTIVE),
                      static_cast<uint8_t>(poll.state));
    if (poll.nextDueMs != 0 && !timeReached(nowMs, poll.nextDueMs)) {
      nowMs = poll.nextDueMs;
    } else {
      ++nowMs;
    }
  }
  TEST_FAIL_MESSAGE("operation did not reach a terminal result");
  return poll;
}

OperationResult takeTerminal(Device& device, const OperationId& id) {
  OperationResult result = {};
  const Status status = device.takeResult(id, result);
  TEST_ASSERT_TRUE_MESSAGE(status.ok(), status.msg);
  TEST_ASSERT_TRUE(result.id == id);
  return result;
}

void advanceToNextSafe(Device& device, uint32_t& nowMs) {
  const RuntimeSnapshot runtime = device.runtimeSnapshot();
  if (runtime.nextSafeCommandValid &&
      !timeReached(nowMs, runtime.nextSafeCommandMs)) {
    nowMs = runtime.nextSafeCommandMs;
  }
}

OperationResult completeJob(Device& device, ModelTransport& bus,
                            const OperationRequest& request, uint32_t& nowMs,
                            uint32_t durationMs = 30000, uint32_t requestId = 1,
                            uint8_t budget = 1) {
  advanceToNextSafe(device, nowMs);
  resetOperationTrace(bus);
  const OperationId id = startJob(device, request, nowMs, nowMs + durationMs, requestId);
  driveUntilTerminal(device, bus, nowMs, budget);
  const OperationResult result = takeTerminal(device, id);
  advanceToNextSafe(device, nowMs);
  return result;
}

void attachDevice(Device& device, ModelTransport& bus, uint32_t& nowMs,
                  uint32_t requestId = 1) {
  const OperationResult result =
      completeJob(device, bus, OperationRequest::make(OperationKind::ATTACH), nowMs, 5000,
                  requestId);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_TRUE(device.isAttached());
  TEST_ASSERT_TRUE(device.identity().valid);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SensorVariant::SCD41),
                    static_cast<uint8_t>(device.identity().variant));
}

size_t countCommand(const ModelTransport& bus, size_t firstCall, uint16_t command) {
  size_t count = 0;
  for (size_t i = firstCall; i < bus.calls; ++i) {
    if (bus.trace[i].writeLength != 0 && bus.trace[i].command == command) {
      ++count;
    }
  }
  return count;
}

void assertNoIoStatus(const Status& status, const ModelTransport& bus, size_t callsBefore,
                      Err expected) {
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(expected), static_cast<uint8_t>(status.code));
  TEST_ASSERT_EQUAL_UINT32(callsBefore, bus.calls);
}

template <typename Enum>
struct EnumNameCase {
  Enum value;
  const char* name;
};

template <typename Enum, size_t N>
void assertEnumNames(const EnumNameCase<Enum> (&cases)[N],
                     const char* (*descriptiveName)(Enum),
                     const char* (*compatibilityName)(Enum), Enum invalid) {
  for (const EnumNameCase<Enum>& item : cases) {
    TEST_ASSERT_EQUAL_STRING(item.name, descriptiveName(item.value));
    TEST_ASSERT_EQUAL_STRING(item.name, compatibilityName(item.value));
  }
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", descriptiveName(invalid));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", compatibilityName(invalid));
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_status_and_public_type_contracts() {
  struct ErrorNameCase {
    Err code;
    const char* name;
  };
  const ErrorNameCase errorNames[] = {
      {Err::OK, "OK"},
      {Err::NOT_INITIALIZED, "NOT_INITIALIZED"},
      {Err::INVALID_CONFIG, "INVALID_CONFIG"},
      {Err::I2C_ERROR, "I2C_ERROR"},
      {Err::TIMEOUT, "TIMEOUT"},
      {Err::INVALID_PARAM, "INVALID_PARAM"},
      {Err::DEVICE_NOT_FOUND, "DEVICE_NOT_FOUND"},
      {Err::CRC_MISMATCH, "CRC_MISMATCH"},
      {Err::MEASUREMENT_NOT_READY, "MEASUREMENT_NOT_READY"},
      {Err::BUSY, "BUSY"},
      {Err::IN_PROGRESS, "IN_PROGRESS"},
      {Err::COMMAND_FAILED, "COMMAND_FAILED"},
      {Err::UNSUPPORTED, "UNSUPPORTED"},
      {Err::I2C_NACK_ADDR, "I2C_NACK_ADDR"},
      {Err::I2C_NACK_DATA, "I2C_NACK_DATA"},
      {Err::I2C_NACK_READ, "I2C_NACK_READ"},
      {Err::I2C_TIMEOUT, "I2C_TIMEOUT"},
      {Err::I2C_BUS, "I2C_BUS"},
      {Err::OFFLINE, "OFFLINE"},
      {Err::RESULT_NOT_READY, "RESULT_NOT_READY"},
      {Err::STALE_RESULT, "STALE_RESULT"},
      {Err::CANCELLED, "CANCELLED"},
      {Err::PARTIAL, "PARTIAL"},
      {Err::INDETERMINATE, "INDETERMINATE"},
      {Err::CONFIRMATION_REQUIRED, "CONFIRMATION_REQUIRED"},
      {Err::RECONCILIATION_REQUIRED, "RECONCILIATION_REQUIRED"},
      {Err::I2C_NACK, "I2C_NACK"},
      {Err::I2C_SHORT_TRANSFER, "I2C_SHORT_TRANSFER"},
  };
  for (const ErrorNameCase& item : errorNames) {
    TEST_ASSERT_EQUAL_STRING(item.name, errorName(item.code));
  }
  TEST_ASSERT_EQUAL_STRING("MEASUREMENT_NOT_READY",
                           errorName(Err::CONVERSION_NOT_READY));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CRC_MISMATCH),
                    static_cast<uint8_t>(Err::CRC_ERROR));
  TEST_ASSERT_EQUAL_STRING("CRC_MISMATCH", errorName(Err::CRC_ERROR));
  TEST_ASSERT_EQUAL_STRING("CRC_MISMATCH", toString(Err::CRC_ERROR));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", errorName(static_cast<Err>(0xFFU)));
  TEST_ASSERT_EQUAL_STRING("OK", toString(Err::OK));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<Err>(0xFFU)));
  TEST_ASSERT_EQUAL_STRING("UNINIT", driverStateName(DriverState::UNINIT));
  TEST_ASSERT_EQUAL_STRING("READY", driverStateName(DriverState::READY));
  TEST_ASSERT_EQUAL_STRING("DEGRADED", driverStateName(DriverState::DEGRADED));
  TEST_ASSERT_EQUAL_STRING("OFFLINE", driverStateName(DriverState::OFFLINE));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                           driverStateName(static_cast<DriverState>(0xFFU)));
  TEST_ASSERT_EQUAL_STRING("READY", toString(DriverState::READY));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                           toString(static_cast<DriverState>(0xFFU)));
  const Identity legacyIdentity{UINT64_C(0x123456789ABC),
                                SensorVariant::SCD41, 7U, true};
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x123456789ABC),
                          legacyIdentity.serialNumber);
  TEST_ASSERT_EQUAL_UINT32(7U, legacyIdentity.sensorEpoch);
  TEST_ASSERT_TRUE(legacyIdentity.valid);
  TEST_ASSERT_EQUAL_HEX16(0U, legacyIdentity.variantWord);
  TEST_ASSERT_TRUE(Status::Ok().ok());
  TEST_ASSERT_TRUE(Status::Error(Err::IN_PROGRESS, "pending").inProgress());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationClass::STEADY_STATE),
                    static_cast<uint8_t>(Device::limits(OperationKind::FETCH_SAMPLE).operationClass));
  TEST_ASSERT_EQUAL_UINT8(0U, Device::limits(OperationKind::FETCH_SAMPLE).maxRetries);
  TEST_ASSERT_TRUE(Device::limits(OperationKind::SELF_TEST).maxWaitMs >=
                   cmd::EXECUTION_TIME_SELF_TEST_MS);
  TEST_ASSERT_TRUE(Device::limits(OperationKind::PERSIST_SETTINGS).writesNonvolatile);
  TEST_ASSERT_TRUE(Device::limits(OperationKind::FORCED_RECALIBRATION).writesNonvolatile);
  TEST_ASSERT_TRUE(Device::limits(OperationKind::FACTORY_RESET).destructive);
  TEST_ASSERT_EQUAL_UINT32(1533U, Device::limits(OperationKind::ATTACH).maxWaitMs);
  TEST_ASSERT_EQUAL_UINT8(6U, Device::limits(OperationKind::ATTACH).maxCallbacks);
  TEST_ASSERT_EQUAL_UINT32(3U, Device::limits(OperationKind::READ_IDENTITY).maxWaitMs);
  TEST_ASSERT_EQUAL_UINT8(4U, Device::limits(OperationKind::READ_IDENTITY).maxCallbacks);
  TEST_ASSERT_EQUAL_UINT32(1U,
                           Device::limits(OperationKind::READ_SENSOR_VARIANT).maxWaitMs);
  TEST_ASSERT_EQUAL_UINT8(
      2U, Device::limits(OperationKind::READ_SENSOR_VARIANT).maxCallbacks);
  TEST_ASSERT_EQUAL_UINT32(3U, Device::limits(OperationKind::FETCH_SAMPLE).maxWaitMs);
  TEST_ASSERT_EQUAL_UINT32(5003U, Device::limits(OperationKind::SINGLE_SHOT).maxWaitMs);
  TEST_ASSERT_EQUAL_UINT32(53U,
                           Device::limits(OperationKind::SINGLE_SHOT_RHT_ONLY).maxWaitMs);
  TEST_ASSERT_EQUAL_UINT32(4U,
                           Device::limits(OperationKind::SET_AMBIENT_PRESSURE).maxWaitMs);
  TEST_ASSERT_EQUAL_UINT32(13U,
                           Device::limits(OperationKind::READ_CONFIGURATION).maxWaitMs);
}

void test_public_enum_name_helpers_are_exhaustive() {
  const EnumNameCase<SensorVariant> variants[] = {
      {SensorVariant::UNKNOWN, "UNKNOWN"}, {SensorVariant::SCD40, "SCD40"},
      {SensorVariant::SCD41, "SCD41"},     {SensorVariant::SCD42, "SCD42"},
      {SensorVariant::SCD43, "SCD43"},
  };
  assertEnumNames(variants, sensorVariantName,
                  static_cast<const char* (*)(SensorVariant)>(toString),
                  static_cast<SensorVariant>(0xFEU));

  const EnumNameCase<OperatingMode> modes[] = {
      {OperatingMode::UNKNOWN, "UNKNOWN"},
      {OperatingMode::IDLE, "IDLE"},
      {OperatingMode::PERIODIC, "PERIODIC"},
      {OperatingMode::LOW_POWER_PERIODIC, "LOW_POWER_PERIODIC"},
      {OperatingMode::POWER_DOWN, "POWER_DOWN"},
  };
  assertEnumNames(modes, operatingModeName,
                  static_cast<const char* (*)(OperatingMode)>(toString),
                  static_cast<OperatingMode>(0xFFU));

  const EnumNameCase<ModeEvidence> evidence[] = {
      {ModeEvidence::UNKNOWN, "UNKNOWN"},
      {ModeEvidence::ACKNOWLEDGED, "ACKNOWLEDGED"},
      {ModeEvidence::VERIFIED, "VERIFIED"},
  };
  assertEnumNames(evidence, modeEvidenceName,
                  static_cast<const char* (*)(ModeEvidence)>(toString),
                  static_cast<ModeEvidence>(0xFFU));

  const EnumNameCase<OperationKind> kinds[] = {
      {OperationKind::NONE, "NONE"},
      {OperationKind::ATTACH, "ATTACH"},
      {OperationKind::READ_IDENTITY, "READ_IDENTITY"},
      {OperationKind::START_PERIODIC, "START_PERIODIC"},
      {OperationKind::START_LOW_POWER_PERIODIC, "START_LOW_POWER_PERIODIC"},
      {OperationKind::STOP_PERIODIC, "STOP_PERIODIC"},
      {OperationKind::READ_DATA_READY, "READ_DATA_READY"},
      {OperationKind::FETCH_SAMPLE, "FETCH_SAMPLE"},
      {OperationKind::SINGLE_SHOT, "SINGLE_SHOT"},
      {OperationKind::SINGLE_SHOT_RHT_ONLY, "SINGLE_SHOT_RHT_ONLY"},
      {OperationKind::READ_TEMPERATURE_OFFSET, "READ_TEMPERATURE_OFFSET"},
      {OperationKind::SET_TEMPERATURE_OFFSET, "SET_TEMPERATURE_OFFSET"},
      {OperationKind::READ_SENSOR_ALTITUDE, "READ_SENSOR_ALTITUDE"},
      {OperationKind::SET_SENSOR_ALTITUDE, "SET_SENSOR_ALTITUDE"},
      {OperationKind::READ_AMBIENT_PRESSURE, "READ_AMBIENT_PRESSURE"},
      {OperationKind::SET_AMBIENT_PRESSURE, "SET_AMBIENT_PRESSURE"},
      {OperationKind::READ_ASC_ENABLED, "READ_ASC_ENABLED"},
      {OperationKind::SET_ASC_ENABLED, "SET_ASC_ENABLED"},
      {OperationKind::READ_ASC_TARGET, "READ_ASC_TARGET"},
      {OperationKind::SET_ASC_TARGET, "SET_ASC_TARGET"},
      {OperationKind::READ_ASC_INITIAL_PERIOD, "READ_ASC_INITIAL_PERIOD"},
      {OperationKind::SET_ASC_INITIAL_PERIOD, "SET_ASC_INITIAL_PERIOD"},
      {OperationKind::READ_ASC_STANDARD_PERIOD, "READ_ASC_STANDARD_PERIOD"},
      {OperationKind::SET_ASC_STANDARD_PERIOD, "SET_ASC_STANDARD_PERIOD"},
      {OperationKind::READ_CONFIGURATION, "READ_CONFIGURATION"},
      {OperationKind::POWER_DOWN, "POWER_DOWN"},
      {OperationKind::WAKE_UP, "WAKE_UP"},
      {OperationKind::REINIT, "REINIT"},
      {OperationKind::SELF_TEST, "SELF_TEST"},
      {OperationKind::FORCED_RECALIBRATION, "FORCED_RECALIBRATION"},
      {OperationKind::PERSIST_SETTINGS, "PERSIST_SETTINGS"},
      {OperationKind::FACTORY_RESET, "FACTORY_RESET"},
      {OperationKind::DIAGNOSTIC_READ_WORDS, "DIAGNOSTIC_READ_WORDS"},
      {OperationKind::DIAGNOSTIC_WRITE_COMMAND, "DIAGNOSTIC_WRITE_COMMAND"},
      {OperationKind::DIAGNOSTIC_WRITE_WORD, "DIAGNOSTIC_WRITE_WORD"},
      {OperationKind::READ_SENSOR_VARIANT, "READ_SENSOR_VARIANT"},
  };
  assertEnumNames(kinds, operationKindName,
                  static_cast<const char* (*)(OperationKind)>(toString),
                  static_cast<OperationKind>(0xFFU));

  const EnumNameCase<OperationState> states[] = {
      {OperationState::IDLE, "IDLE"},
      {OperationState::ACTIVE, "ACTIVE"},
      {OperationState::RESULT_PENDING, "RESULT_PENDING"},
  };
  assertEnumNames(states, operationStateName,
                  static_cast<const char* (*)(OperationState)>(toString),
                  static_cast<OperationState>(0xFFU));

  const EnumNameCase<OperationOutcome> outcomes[] = {
      {OperationOutcome::SUCCEEDED, "SUCCEEDED"},
      {OperationOutcome::NO_DATA, "NO_DATA"},
      {OperationOutcome::FAILED, "FAILED"},
      {OperationOutcome::CANCELLED, "CANCELLED"},
      {OperationOutcome::TIMED_OUT, "TIMED_OUT"},
      {OperationOutcome::PARTIAL, "PARTIAL"},
      {OperationOutcome::INDETERMINATE, "INDETERMINATE"},
  };
  assertEnumNames(outcomes, operationOutcomeName,
                  static_cast<const char* (*)(OperationOutcome)>(toString),
                  static_cast<OperationOutcome>(0xFFU));

  const EnumNameCase<EffectState> effects[] = {
      {EffectState::NONE, "NONE"},
      {EffectState::NOT_ATTEMPTED, "NOT_ATTEMPTED"},
      {EffectState::ATTEMPTED, "ATTEMPTED"},
      {EffectState::ACKNOWLEDGED, "ACKNOWLEDGED"},
      {EffectState::VERIFIED, "VERIFIED"},
      {EffectState::UNKNOWN, "UNKNOWN"},
  };
  assertEnumNames(effects, effectStateName,
                  static_cast<const char* (*)(EffectState)>(toString),
                  static_cast<EffectState>(0xFFU));

  const EnumNameCase<OperationPhase> phases[] = {
      {OperationPhase::NONE, "NONE"},
      {OperationPhase::WAIT_POWER_UP, "WAIT_POWER_UP"},
      {OperationPhase::SEND_WAKE, "SEND_WAKE"},
      {OperationPhase::WAIT_WAKE, "WAIT_WAKE"},
      {OperationPhase::SEND_STOP, "SEND_STOP"},
      {OperationPhase::WAIT_STOP, "WAIT_STOP"},
      {OperationPhase::SEND_COMMAND, "SEND_COMMAND"},
      {OperationPhase::WAIT_EXECUTION, "WAIT_EXECUTION"},
      {OperationPhase::SEND_READY_COMMAND, "SEND_READY_COMMAND"},
      {OperationPhase::READ_READY_RESPONSE, "READ_READY_RESPONSE"},
      {OperationPhase::SEND_READ_COMMAND, "SEND_READ_COMMAND"},
      {OperationPhase::READ_RESPONSE, "READ_RESPONSE"},
      {OperationPhase::SEND_VERIFY_COMMAND, "SEND_VERIFY_COMMAND"},
      {OperationPhase::READ_VERIFY_RESPONSE, "READ_VERIFY_RESPONSE"},
      {OperationPhase::READ_DEFERRED_RESULT, "READ_DEFERRED_RESULT"},
  };
  assertEnumNames(phases, operationPhaseName,
                  static_cast<const char* (*)(OperationPhase)>(toString),
                  static_cast<OperationPhase>(0xFFU));
}

void test_public_health_compatibility_accessors_match_transfer_channel() {
  ModelTransport bus;
  Device device;
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_FALSE(device.isOnline());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.driverState()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.healthSnapshot().state));
  TEST_ASSERT_EQUAL_UINT32(0U, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0U, device.lastErrorMs());
  TEST_ASSERT_TRUE(device.lastError().ok());
  TEST_ASSERT_EQUAL_UINT8(0U, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0U, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(0U, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0U, bus.calls);

  Config config = makeConfig(bus);
  config.offlineThreshold = 1U;
  TEST_ASSERT_TRUE(device.begin(config).ok());
  TEST_ASSERT_TRUE(device.isInitialized());
  TEST_ASSERT_TRUE(device.isBound());
  TEST_ASSERT_TRUE(device.isOnline());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.driverState()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.healthSnapshot().state));
  TEST_ASSERT_EQUAL_UINT32(0U, bus.calls);

  uint32_t nowMs = 100U;
  attachDevice(device, bus, nowMs, 600U);
  HealthSnapshot health = device.healthSnapshot();
  const size_t afterAttachCalls = bus.calls;
  TEST_ASSERT_TRUE(device.isOnline());
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferOkMs, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferErrorMs, device.lastErrorMs());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(health.lastTransferError.code),
                    static_cast<uint8_t>(device.lastError().code));
  TEST_ASSERT_EQUAL_INT32(health.lastTransferError.detail,
                          device.lastError().detail);
  TEST_ASSERT_EQUAL_UINT8(health.consecutiveTransferFailures,
                          device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferFailures,
                           device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferSuccess, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(afterAttachCalls, bus.calls);

  resetOperationTrace(bus);
  faultRelativeCall(bus, 1U, TransferCode::TIMEOUT,
                    TransferDisposition::NO_EFFECT, false, false, nowMs);
  const OperationId failed = startJob(
      device, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
      nowMs + 100U, 601U);
  driveUntilTerminal(device, bus, nowMs);
  const OperationResult failure = takeTerminal(device, failed);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                    static_cast<uint8_t>(failure.status.code));
  health = device.healthSnapshot();
  const size_t afterFailureCalls = bus.calls;
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::OFFLINE),
                    static_cast<uint8_t>(device.state()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.driverState()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(health.state));
  TEST_ASSERT_FALSE(device.isOnline());
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferOkMs, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferErrorMs, device.lastErrorMs());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(health.lastTransferError.code),
                    static_cast<uint8_t>(device.lastError().code));
  TEST_ASSERT_EQUAL_INT32(health.lastTransferError.detail,
                          device.lastError().detail);
  TEST_ASSERT_EQUAL_UINT8(health.consecutiveTransferFailures,
                          device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferFailures,
                           device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferSuccess, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(afterFailureCalls, bus.calls);

  Config invalid = makeConfig(bus);
  invalid.transfer = nullptr;
  const Status rejected = device.begin(invalid);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_CONFIG),
                    static_cast<uint8_t>(rejected.code));
  TEST_ASSERT_TRUE(device.isInitialized());
  TEST_ASSERT_FALSE(device.isOnline());
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferOkMs, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferErrorMs, device.lastErrorMs());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferFailures,
                           device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferSuccess, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(afterFailureCalls, bus.calls);

  device.end();
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_FALSE(device.isOnline());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.driverState()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.healthSnapshot().state));
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferOkMs, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(health.lastTransferErrorMs, device.lastErrorMs());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferFailures,
                           device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(health.totalTransferSuccess, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(afterFailureCalls, bus.calls);

  TEST_ASSERT_TRUE(device.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.isInitialized());
  TEST_ASSERT_TRUE(device.isOnline());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.driverState()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device.state()),
                    static_cast<uint8_t>(device.healthSnapshot().state));
  TEST_ASSERT_EQUAL_UINT32(0U, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0U, device.lastErrorMs());
  TEST_ASSERT_TRUE(device.lastError().ok());
  TEST_ASSERT_EQUAL_UINT8(0U, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0U, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(0U, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(afterFailureCalls, bus.calls);
}

void test_begin_is_zero_io_and_validates_before_rebinding() {
  ModelTransport busA;
  Device device;
  Config configA = makeConfig(busA);
  TEST_ASSERT_TRUE(device.begin(configA).ok());
  TEST_ASSERT_EQUAL_UINT32(0U, busA.calls);

  ModelTransport busB;
  Config invalid = makeConfig(busB);
  invalid.transfer = nullptr;
  const Status invalidStatus = device.begin(invalid);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_CONFIG),
                    static_cast<uint8_t>(invalidStatus.code));
  TEST_ASSERT_TRUE(device.isBound());
  TEST_ASSERT_EQUAL_UINT32(0U, busA.calls);
  TEST_ASSERT_EQUAL_UINT32(0U, busB.calls);

  OperationId id = {};
  TEST_ASSERT_TRUE(device.start(OperationRequest::make(OperationKind::ATTACH),
                                OperationOptions{7, 10, 5000}, id)
                       .inProgress());
  pollChecked(device, busA, 10, 1);
  TEST_ASSERT_TRUE(busA.calls <= 1U);
  TEST_ASSERT_EQUAL_UINT32(0U, busB.calls);
}

void test_config_validation_boundaries_are_zero_io() {
  ModelTransport bus;
  Config config = makeConfig(bus);
  Device device;

  config.transferTimeoutMs = 0;
  assertNoIoStatus(device.begin(config), bus, 0, Err::INVALID_CONFIG);
  config = makeConfig(bus);
  config.transferTimeoutMs = 1001;
  assertNoIoStatus(device.begin(config), bus, 0, Err::INVALID_CONFIG);
  config = makeConfig(bus);
  config.powerUpDelayMs = cmd::EXECUTION_TIME_POWER_UP_MS - 1U;
  assertNoIoStatus(device.begin(config), bus, 0, Err::INVALID_CONFIG);
  config = makeConfig(bus);
  config.powerUpDelayMs = 1001;
  assertNoIoStatus(device.begin(config), bus, 0, Err::INVALID_CONFIG);

  config = makeConfig(bus);
  config.transferTimeoutMs = 1000;
  config.powerUpDelayMs = 1000;
  TEST_ASSERT_TRUE(device.begin(config).ok());
  TEST_ASSERT_EQUAL_UINT32(0U, bus.calls);
}

void test_start_is_zero_io_and_result_backpressure_is_exact() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  resetOperationTrace(bus);

  const OperationId first =
      startJob(device, OperationRequest::make(OperationKind::ATTACH), nowMs, 5000, 41);
  TEST_ASSERT_EQUAL_UINT32(0U, operationCalls(bus));

  OperationId rejected = {99, 99};
  const size_t beforeRejected = bus.calls;
  assertNoIoStatus(device.start(OperationRequest::make(OperationKind::READ_IDENTITY),
                                OperationOptions{42, nowMs, 5000}, rejected),
                   bus, beforeRejected, Err::BUSY);

  driveUntilTerminal(device, bus, nowMs);
  const size_t beforePending = bus.calls;
  assertNoIoStatus(device.start(OperationRequest::make(OperationKind::READ_IDENTITY),
                                OperationOptions{43, nowMs, nowMs + 100}, rejected),
                   bus, beforePending, Err::BUSY);

  const OperationResult terminal = takeTerminal(device, first);
  TEST_ASSERT_EQUAL_UINT32(41U, terminal.id.requestId);

  const size_t beforeSafetyBusy = bus.calls;
  assertNoIoStatus(device.start(OperationRequest::make(OperationKind::READ_IDENTITY),
                                OperationOptions{43, nowMs, nowMs + 100}, rejected),
                   bus, beforeSafetyBusy, Err::BUSY);
  advanceToNextSafe(device, nowMs);
  const OperationId second =
      startJob(device, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
               nowMs + 100, 43);
  TEST_ASSERT_TRUE(second.generation != first.generation);
}

void test_attach_budget_waiting_and_expected_wake_nack() {
  ModelTransport bus;
  bus.mode = ModelMode::POWER_DOWN;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 100;
  resetOperationTrace(bus);
  const OperationId id =
      startJob(device, OperationRequest::make(OperationKind::ATTACH), nowMs, nowMs + 5000, 2);

  PollResult poll = pollChecked(device, bus, nowMs, 1);
  TEST_ASSERT_EQUAL_UINT8(0U, poll.callbacksUsed);
  TEST_ASSERT_EQUAL_UINT32(0U, operationCalls(bus));
  TEST_ASSERT_TRUE(poll.nextDueMs != 0);

  if (!timeReached(nowMs, poll.nextDueMs)) {
    nowMs = poll.nextDueMs - 1U;
    poll = pollChecked(device, bus, nowMs, 1);
    TEST_ASSERT_EQUAL_UINT8(0U, poll.callbacksUsed);
  }

  driveUntilTerminal(device, bus, nowMs, 1);
  const OperationResult result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::VERIFIED),
                    static_cast<uint8_t>(result.effect));

  bool sawExpectedNackIntent = false;
  for (size_t i = bus.operationCallBase; i < bus.calls; ++i) {
    if (bus.trace[i].command == cmd::CMD_WAKE_UP) {
      sawExpectedNackIntent =
          bus.trace[i].intent == TransferIntent::EXPECTED_WRITE_NACK;
    }
  }
  TEST_ASSERT_TRUE(sawExpectedNackIntent);
  TEST_ASSERT_EQUAL_UINT32(1U, device.healthSnapshot().expectedNacks);

  ModelTransport idleBus;
  Device idleDevice;
  bindDevice(idleDevice, idleBus);
  uint32_t idleNowMs = 10;
  resetOperationTrace(idleBus);
  faultRelativeCall(idleBus, 2, TransferCode::NACK,
                    TransferDisposition::NO_EFFECT);
  const OperationId idleId = startJob(
      idleDevice, OperationRequest::make(OperationKind::ATTACH), idleNowMs,
      idleNowMs + 5000U, 3);
  driveUntilTerminal(idleDevice, idleBus, idleNowMs);
  const OperationResult idleResult = takeTerminal(idleDevice, idleId);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(idleResult.outcome));
  TEST_ASSERT_EQUAL_UINT32(2U, idleDevice.healthSnapshot().expectedNacks);
}

void test_poll_budget_zero_never_touches_transport() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  OperationId id = {};
  TEST_ASSERT_TRUE(device.start(OperationRequest::make(OperationKind::ATTACH),
                                OperationOptions{1, 10, 5000}, id)
                       .inProgress());
  const size_t before = bus.calls;
  const PollResult poll = device.poll(5000, 0);
  TEST_ASSERT_EQUAL_UINT8(0U, poll.callbacksUsed);
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
}

void test_exactly_once_and_stale_result_do_not_modify_output() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  resetOperationTrace(bus);
  const OperationId id =
      startJob(device, OperationRequest::make(OperationKind::ATTACH), nowMs, 5000, 9);
  driveUntilTerminal(device, bus, nowMs);

  OperationResult sentinel = {};
  uint8_t before[sizeof(sentinel)] = {};
  std::memcpy(before, &sentinel, sizeof(sentinel));
  const Status stale = device.takeResult(OperationId{id.requestId + 1U, id.generation}, sentinel);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::STALE_RESULT), static_cast<uint8_t>(stale.code));
  TEST_ASSERT_EQUAL_MEMORY(before, &sentinel, sizeof(sentinel));

  TEST_ASSERT_TRUE(device.takeResult(id, sentinel).ok());
  TEST_ASSERT_TRUE(sentinel.id == id);
  std::memcpy(before, &sentinel, sizeof(sentinel));
  const Status secondTake = device.takeResult(id, sentinel);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::RESULT_NOT_READY),
                    static_cast<uint8_t>(secondTake.code));
  TEST_ASSERT_EQUAL_MEMORY(before, &sentinel, sizeof(sentinel));
}

void test_deadline_before_first_transfer_and_exact_boundary() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  OperationId id = {};
  const size_t before = bus.calls;
  const Status expired = device.start(OperationRequest::make(OperationKind::ATTACH),
                                      OperationOptions{1, 100, 100}, id);
  TEST_ASSERT_TRUE(expired.code == Err::TIMEOUT || expired.code == Err::INVALID_PARAM);
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);

  const Status past = device.start(OperationRequest::make(OperationKind::ATTACH),
                                   OperationOptions{2, 100, 99}, id);
  TEST_ASSERT_TRUE(past.code == Err::TIMEOUT || past.code == Err::INVALID_PARAM);
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
}

void test_deadline_after_callback_reports_timeout_without_retry() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  resetOperationTrace(bus);
  faultRelativeCall(bus, 1, TransferCode::OK, TransferDisposition::COMPLETE, true, false, 1000);
  const OperationId id =
      startJob(device, OperationRequest::make(OperationKind::ATTACH), nowMs, 100, 5);
  driveUntilTerminal(device, bus, nowMs);
  const OperationResult result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::TIMED_OUT),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::TIMEOUT), static_cast<uint8_t>(result.status.code));
  TEST_ASSERT_EQUAL_UINT32(1U, operationCalls(bus));
}

void test_deadline_and_waits_handle_u32_wrap() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 0xFFFFFFF0U;
  resetOperationTrace(bus);
  const OperationId id = startJob(device, OperationRequest::make(OperationKind::ATTACH), nowMs,
                                  nowMs + 5000U, 7);
  driveUntilTerminal(device, bus, nowMs);
  const OperationResult result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_TRUE(nowMs < 0xFFFFFFF0U);
}

void test_owner_clock_cannot_move_backwards() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  const OperationId id =
      startJob(device, OperationRequest::make(OperationKind::ATTACH), 100,
               5000, 8);
  const size_t before = bus.calls;
  const PollResult invalidPoll = device.poll(99, 1);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(invalidPoll.status.code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationState::ACTIVE),
                    static_cast<uint8_t>(invalidPoll.state));
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(device.cancel(id, 99).code));
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
  TEST_ASSERT_TRUE(device.cancel(id, 100).ok());
}

void test_cancel_before_io_is_exact_and_prevents_stale_completion() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  const OperationId cancelled =
      startJob(device, OperationRequest::make(OperationKind::ATTACH), 10, 5000, 10);
  const size_t before = bus.calls;
  TEST_ASSERT_TRUE(device.cancel(cancelled, 11).ok());
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
  const OperationResult cancelledResult = takeTerminal(device, cancelled);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                    static_cast<uint8_t>(cancelledResult.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::NOT_ATTEMPTED),
                    static_cast<uint8_t>(cancelledResult.effect));

  uint32_t nowMs = 12;
  resetOperationTrace(bus);
  const OperationId next =
      startJob(device, OperationRequest::make(OperationKind::ATTACH), nowMs, 5000, 11);
  driveUntilTerminal(device, bus, nowMs);
  TEST_ASSERT_TRUE(takeTerminal(device, next).id == next);
  TEST_ASSERT_TRUE(next.generation != cancelled.generation);
}

void test_cancel_after_effectful_write_requires_reconciliation_and_no_retry() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  resetOperationTrace(bus);
  const OperationId id = startJob(device, OperationRequest::make(OperationKind::SINGLE_SHOT),
                                  nowMs, nowMs + 6000, 20);
  PollResult poll = pollChecked(device, bus, nowMs, 1);
  while (operationCalls(bus) == 0) {
    nowMs = poll.nextDueMs != 0 ? poll.nextDueMs : nowMs + 1U;
    poll = pollChecked(device, bus, nowMs, 1);
  }
  TEST_ASSERT_EQUAL_UINT32(1U,
                           countCommand(bus, bus.operationCallBase, cmd::CMD_MEASURE_SINGLE_SHOT));
  TEST_ASSERT_TRUE(device.cancel(id, nowMs + 1U).ok());
  const OperationResult result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_TRUE(result.effect == EffectState::ATTEMPTED ||
                   result.effect == EffectState::ACKNOWLEDGED ||
                   result.effect == EffectState::UNKNOWN);
  TEST_ASSERT_TRUE(result.reconciliationRequired);
  TEST_ASSERT_TRUE(device.runtimeSnapshot().reconciliationRequired);

  const size_t before = bus.calls;
  OperationId rejected = {};
  const Status blocked = device.start(OperationRequest::make(OperationKind::READ_IDENTITY),
                                      OperationOptions{21, nowMs + 2U, nowMs + 100U}, rejected);
  TEST_ASSERT_TRUE(blocked.code == Err::RECONCILIATION_REQUIRED || blocked.code == Err::BUSY);
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
}

void test_attach_failure_at_each_transfer_is_terminal_and_bounded() {
  ModelTransport baselineBus;
  Device baselineDevice;
  bindDevice(baselineDevice, baselineBus);
  uint32_t baselineNow = 10;
  attachDevice(baselineDevice, baselineBus, baselineNow);
  const size_t attachTransfers = operationCalls(baselineBus);
  TEST_ASSERT_TRUE(attachTransfers > 1U);
  TEST_ASSERT_TRUE(attachTransfers <= Device::limits(OperationKind::ATTACH).maxCallbacks);

  for (size_t failedStage = 1; failedStage <= attachTransfers; ++failedStage) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    resetOperationTrace(bus);
    faultRelativeCall(bus, failedStage, TransferCode::TIMEOUT,
                      TransferDisposition::NO_EFFECT);
    const OperationId id =
        startJob(device, OperationRequest::make(OperationKind::ATTACH), nowMs, 5000,
                 static_cast<uint32_t>(100 + failedStage));
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_TRUE(result.outcome == OperationOutcome::FAILED ||
                     result.outcome == OperationOutcome::INDETERMINATE);
    TEST_ASSERT_TRUE(operationCalls(bus) <= failedStage);
    TEST_ASSERT_FALSE(device.isAttached());
  }
}

void test_attach_converges_from_known_modes_and_hotplug() {
  const ModelMode startingModes[] = {ModelMode::IDLE, ModelMode::PERIODIC,
                                     ModelMode::LOW_POWER_PERIODIC, ModelMode::POWER_DOWN};
  for (ModelMode mode : startingModes) {
    ModelTransport bus;
    bus.mode = mode;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs, static_cast<uint32_t>(200 + static_cast<uint8_t>(mode)));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(ModelMode::IDLE),
                      static_cast<uint8_t>(bus.mode));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::IDLE),
                      static_cast<uint8_t>(device.runtimeSnapshot().operatingMode));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(ModeEvidence::VERIFIED),
                      static_cast<uint8_t>(device.runtimeSnapshot().modeEvidence));
  }

  ModelTransport bus;
  bus.present = false;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  const OperationResult absent =
      completeJob(device, bus, OperationRequest::make(OperationKind::ATTACH), nowMs, 5000, 210);
  TEST_ASSERT_TRUE(absent.outcome != OperationOutcome::SUCCEEDED);
  TEST_ASSERT_FALSE(device.isAttached());

  bus.present = true;
  attachDevice(device, bus, nowMs, 211);
  TEST_ASSERT_TRUE(device.isAttached());
}

void test_hotplug_attach_starts_a_new_cache_epoch() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs, 220);
  completeJob(device, bus, OperationRequest::setTemperatureOffsetMilliC(4000),
              nowMs, 1000, 221);
  TEST_ASSERT_TRUE(device.configurationSnapshot().dirtyMask != 0U);
  const uint32_t previousEpoch = device.runtimeSnapshot().sensorEpoch;

  ++bus.serialWords[2];
  attachDevice(device, bus, nowMs, 222);
  TEST_ASSERT_TRUE(device.runtimeSnapshot().sensorEpoch > previousEpoch);
  TEST_ASSERT_EQUAL_HEX16(0U, device.configurationSnapshot().verifiedMask);
  TEST_ASSERT_EQUAL_HEX16(0U, device.configurationSnapshot().dirtyMask);
}

void test_read_identity_response_fault_and_crc_are_atomic() {
  const TransferCode failureCodes[] = {TransferCode::NACK, TransferCode::TIMEOUT,
                                       TransferCode::BUS_ERROR, TransferCode::SHORT_TRANSFER,
                                       TransferCode::FAILED};
  for (TransferCode code : failureCodes) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    const Identity beforeIdentity = device.identity();
    resetOperationTrace(bus);
    faultRelativeCall(bus, 2, code, TransferDisposition::NO_EFFECT, false, true);
    const OperationId id = startJob(device, OperationRequest::make(OperationKind::READ_IDENTITY),
                                    nowMs, nowMs + 100, 30);
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_TRUE(result.outcome != OperationOutcome::SUCCEEDED);
    const Identity afterIdentity = device.identity();
    TEST_ASSERT_EQUAL_MEMORY(&beforeIdentity, &afterIdentity, sizeof(beforeIdentity));
  }

  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  const Identity beforeIdentity = device.identity();
  bus.badCrc = true;
  bus.badCrcWord = 2;
  const OperationResult crcResult =
      completeJob(device, bus, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs, 100,
                  31);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CRC_MISMATCH),
                    static_cast<uint8_t>(crcResult.status.code));
  const Identity afterIdentity = device.identity();
  TEST_ASSERT_EQUAL_MEMORY(&beforeIdentity, &afterIdentity, sizeof(beforeIdentity));

  bus.badCrcCommand = cmd::CMD_GET_SENSOR_VARIANT;
  bus.badCrcWord = 0;
  const OperationResult variantCrcResult =
      completeJob(device, bus,
                  OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
                  100, 32);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CRC_MISMATCH),
                    static_cast<uint8_t>(variantCrcResult.status.code));
  const Identity afterVariantCrc = device.identity();
  TEST_ASSERT_EQUAL_MEMORY(&beforeIdentity, &afterVariantCrc,
                           sizeof(beforeIdentity));
}

void test_dedicated_variant_command_contract_and_strict_failures() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs, 32);

  TEST_ASSERT_EQUAL_UINT32(6U, operationCalls(bus));
  TEST_ASSERT_EQUAL_UINT32(1U, countCommand(bus, bus.operationCallBase,
                                           cmd::CMD_GET_SERIAL_NUMBER));
  TEST_ASSERT_EQUAL_UINT32(1U, countCommand(bus, bus.operationCallBase,
                                           cmd::CMD_GET_SENSOR_VARIANT));
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xF8969F073BBE),
                          device.identity().serialNumber);
  TEST_ASSERT_EQUAL_HEX16(0x1440U, device.identity().variantWord);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SensorVariant::SCD41),
                    static_cast<uint8_t>(device.identity().variant));

  const Identity attachedIdentity = device.identity();
  const OperationResult variant = completeJob(
      device, bus, OperationRequest::make(OperationKind::READ_SENSOR_VARIANT),
      nowMs, 100, 33);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(variant.outcome));
  TEST_ASSERT_EQUAL_UINT32(2U, operationCalls(bus));
  TEST_ASSERT_EQUAL_UINT32(1U, countCommand(bus, bus.operationCallBase,
                                           cmd::CMD_GET_SENSOR_VARIANT));
  TEST_ASSERT_EQUAL_UINT32(0U, countCommand(bus, bus.operationCallBase,
                                           cmd::CMD_GET_SERIAL_NUMBER));
  TEST_ASSERT_EQUAL_HEX16(0x1440U, variant.value.value);
  TEST_ASSERT_EQUAL_MEMORY(&attachedIdentity, &variant.value.identity,
                           sizeof(attachedIdentity));

  const OperationResult identity = completeJob(
      device, bus, OperationRequest::make(OperationKind::READ_IDENTITY),
      nowMs, 100, 34);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(identity.outcome));
  TEST_ASSERT_EQUAL_UINT32(4U, operationCalls(bus));
  TEST_ASSERT_EQUAL_UINT32(1U, countCommand(bus, bus.operationCallBase,
                                           cmd::CMD_GET_SERIAL_NUMBER));
  TEST_ASSERT_EQUAL_UINT32(1U, countCommand(bus, bus.operationCallBase,
                                           cmd::CMD_GET_SENSOR_VARIANT));
  TEST_ASSERT_EQUAL_MEMORY(&attachedIdentity, &identity.value.identity,
                           sizeof(attachedIdentity));

  const Identity beforeCrcFailure = device.identity();
  bus.badCrc = true;
  bus.badCrcWord = 0;
  const OperationResult badVariant = completeJob(
      device, bus, OperationRequest::make(OperationKind::READ_SENSOR_VARIANT),
      nowMs, 100, 35);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CRC_MISMATCH),
                    static_cast<uint8_t>(badVariant.status.code));
  const Identity afterCrcFailure = device.identity();
  TEST_ASSERT_EQUAL_MEMORY(&beforeCrcFailure, &afterCrcFailure,
                           sizeof(beforeCrcFailure));

  bus.badCrc = false;
  bus.badCrcCommand = 0U;
  bus.variantWord = 0x0440U;
  const OperationResult changedVariant = completeJob(
      device, bus, OperationRequest::make(OperationKind::READ_SENSOR_VARIANT),
      nowMs, 100, 36);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                    static_cast<uint8_t>(changedVariant.status.code));
  TEST_ASSERT_TRUE(changedVariant.reconciliationRequired);
  TEST_ASSERT_FALSE(device.isAttached());
  TEST_ASSERT_FALSE(device.identity().valid);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0), device.identity().serialNumber);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SensorVariant::SCD40),
                    static_cast<uint8_t>(device.identity().variant));
  TEST_ASSERT_EQUAL_HEX16(0x0440U, device.identity().variantWord);

  ModelTransport attachCrcBus;
  attachCrcBus.badCrc = true;
  attachCrcBus.badCrcCommand = cmd::CMD_GET_SENSOR_VARIANT;
  Device attachCrcDevice;
  bindDevice(attachCrcDevice, attachCrcBus);
  uint32_t attachCrcNowMs = 10;
  const OperationResult attachCrc = completeJob(
      attachCrcDevice, attachCrcBus,
      OperationRequest::make(OperationKind::ATTACH), attachCrcNowMs, 5000, 37);
  TEST_ASSERT_EQUAL_UINT32(6U, operationCalls(attachCrcBus));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::FAILED),
                    static_cast<uint8_t>(attachCrc.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CRC_MISMATCH),
                    static_cast<uint8_t>(attachCrc.status.code));
  TEST_ASSERT_FALSE(attachCrcDevice.isAttached());
  TEST_ASSERT_FALSE(attachCrcDevice.identity().valid);
  TEST_ASSERT_TRUE(attachCrc.reconciliationRequired);

  struct UnsupportedVariantCase {
    uint16_t word;
    SensorVariant variant;
  };
  const UnsupportedVariantCase unsupported[] = {
      {0x0440U, SensorVariant::SCD40},
      {0x5441U, SensorVariant::SCD43},
      {0x2440U, SensorVariant::UNKNOWN},
      {0xF440U, SensorVariant::UNKNOWN},
  };
  for (const UnsupportedVariantCase& item : unsupported) {
    ModelTransport strictBus;
    strictBus.variantWord = item.word;
    Device strictDevice;
    bindDevice(strictDevice, strictBus);
    uint32_t strictNowMs = 10;
    const OperationResult result = completeJob(
        strictDevice, strictBus, OperationRequest::make(OperationKind::ATTACH),
        strictNowMs, 5000, 36);
    TEST_ASSERT_EQUAL_UINT32(6U, operationCalls(strictBus));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::FAILED),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                      static_cast<uint8_t>(result.status.code));
    TEST_ASSERT_TRUE(result.reconciliationRequired);
    TEST_ASSERT_FALSE(strictDevice.isAttached());
    TEST_ASSERT_TRUE(strictDevice.identity().valid);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(item.variant),
                      static_cast<uint8_t>(strictDevice.identity().variant));
    TEST_ASSERT_EQUAL_HEX16(item.word, strictDevice.identity().variantWord);
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xF8969F073BBE),
                            strictDevice.identity().serialNumber);
  }
}

void test_periodic_fetch_phase_faults_preserve_last_sample() {
  ModelTransport baselineBus;
  Device baselineDevice;
  bindDevice(baselineDevice, baselineBus);
  uint32_t baselineNow = 10;
  attachDevice(baselineDevice, baselineBus, baselineNow);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(completeJob(
                        baselineDevice, baselineBus,
                        OperationRequest::make(OperationKind::START_PERIODIC), baselineNow, 100,
                        40)
                                             .outcome));
  completeJob(baselineDevice, baselineBus,
              OperationRequest::make(OperationKind::FETCH_SAMPLE), baselineNow, 1000, 41);
  const size_t fetchTransfers = operationCalls(baselineBus);
  TEST_ASSERT_EQUAL_UINT32(4U, fetchTransfers);

  for (size_t failedStage = 1; failedStage <= fetchTransfers; ++failedStage) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    completeJob(device, bus, OperationRequest::make(OperationKind::START_PERIODIC), nowMs, 100,
                42);
    const OperationResult first =
        completeJob(device, bus, OperationRequest::make(OperationKind::FETCH_SAMPLE), nowMs, 1000,
                    43);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                      static_cast<uint8_t>(first.outcome));
    const FixedSample retained = first.value.sample;

    resetOperationTrace(bus);
    faultRelativeCall(bus, failedStage, TransferCode::TIMEOUT,
                      TransferDisposition::NO_EFFECT);
    const OperationId id = startJob(device, OperationRequest::make(OperationKind::FETCH_SAMPLE),
                                    nowMs, nowMs + 1000, 44);
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult failed = takeTerminal(device, id);
    TEST_ASSERT_TRUE(failed.outcome != OperationOutcome::SUCCEEDED);
    FixedSample cached = {};
    TEST_ASSERT_TRUE(device.peekLatestSample(cached).ok());
    TEST_ASSERT_EQUAL_MEMORY(&retained, &cached, sizeof(cached));
  }
}

void test_not_ready_fetch_is_terminal_no_data_without_hidden_retry() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  completeJob(device, bus, OperationRequest::make(OperationKind::START_PERIODIC), nowMs, 100,
              45);
  bus.dataReadyWord = 0;
  const OperationResult result =
      completeJob(device, bus, OperationRequest::make(OperationKind::FETCH_SAMPLE), nowMs, 1000,
                  46);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::NO_DATA),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::MEASUREMENT_NOT_READY),
                    static_cast<uint8_t>(result.status.code));
  TEST_ASSERT_EQUAL_UINT32(2U, operationCalls(bus));
}

void test_fixed_point_sample_take_and_peek_contract() {
  ModelTransport bus;
  bus.measurementWords[0] = 777;
  bus.measurementWords[1] = 32768;
  bus.measurementWords[2] = 49152;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  completeJob(device, bus, OperationRequest::make(OperationKind::START_PERIODIC), nowMs, 100, 50);
  const OperationResult result =
      completeJob(device, bus, OperationRequest::make(OperationKind::FETCH_SAMPLE), nowMs, 1000,
                  51);
  TEST_ASSERT_EQUAL_UINT16(777U, result.value.sample.co2Ppm);
  TEST_ASSERT_EQUAL_INT32(Device::convertTemperatureMilliC(32768),
                          result.value.sample.temperatureMilliC);
  TEST_ASSERT_EQUAL_UINT32(Device::convertHumidityMilliPercent(49152),
                           result.value.sample.humidityMilliPercent);
  TEST_ASSERT_TRUE((result.value.sample.flags & SAMPLE_CO2_VALID) != 0U);
  TEST_ASSERT_TRUE((result.value.sample.flags & SAMPLE_TEMPERATURE_VALID) != 0U);
  TEST_ASSERT_TRUE((result.value.sample.flags & SAMPLE_HUMIDITY_VALID) != 0U);
  TEST_ASSERT_TRUE((result.value.sample.flags & SAMPLE_FRESH) != 0U);

  FixedSample peekA = {};
  FixedSample peekB = {};
  TEST_ASSERT_TRUE(device.peekLatestSample(peekA).ok());
  TEST_ASSERT_TRUE(device.peekLatestSample(peekB).ok());
  TEST_ASSERT_EQUAL_MEMORY(&peekA, &peekB, sizeof(peekA));
}

void test_configuration_read_marks_fields_only_after_complete_crc() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  const OperationResult baseline =
      completeJob(device, bus, OperationRequest::make(OperationKind::READ_CONFIGURATION), nowMs,
                  1000, 60);
  TEST_ASSERT_EQUAL_UINT16(ALL_CONFIGURATION_FIELDS,
                           baseline.value.configuration.verifiedMask);
  const size_t transfers = operationCalls(bus);
  TEST_ASSERT_EQUAL_UINT32(14U, transfers);

  for (size_t failedStage = 1; failedStage <= transfers; ++failedStage) {
    ModelTransport stageBus;
    Device stageDevice;
    bindDevice(stageDevice, stageBus);
    uint32_t stageNow = 10;
    attachDevice(stageDevice, stageBus, stageNow);
    resetOperationTrace(stageBus);
    faultRelativeCall(stageBus, failedStage, TransferCode::TIMEOUT,
                      TransferDisposition::NO_EFFECT);
    const OperationId id =
        startJob(stageDevice, OperationRequest::make(OperationKind::READ_CONFIGURATION), stageNow,
                 stageNow + 1000, static_cast<uint32_t>(1000 + failedStage));
    driveUntilTerminal(stageDevice, stageBus, stageNow);
    const OperationResult result = takeTerminal(stageDevice, id);
    TEST_ASSERT_TRUE(result.outcome != OperationOutcome::SUCCEEDED);
    const size_t completeFields = (failedStage - 1U) / 2U;
    uint16_t expectedMask = 0;
    const ConfigurationField orderedFields[] = {
        ConfigurationField::TEMPERATURE_OFFSET, ConfigurationField::SENSOR_ALTITUDE,
        ConfigurationField::AMBIENT_PRESSURE, ConfigurationField::ASC_ENABLED,
        ConfigurationField::ASC_TARGET, ConfigurationField::ASC_INITIAL_PERIOD,
        ConfigurationField::ASC_STANDARD_PERIOD};
    for (size_t i = 0; i < completeFields; ++i) {
      expectedMask |= configurationFieldMask(orderedFields[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(expectedMask, result.completedFieldMask);
    TEST_ASSERT_EQUAL_HEX16(expectedMask,
                            stageDevice.configurationSnapshot().verifiedMask & expectedMask);
  }
}

void test_invalid_returned_settings_are_not_published_or_cached() {
  struct InvalidSettingCase {
    OperationKind kind;
    ConfigurationField field;
    uint16_t invalidWord;
  };
  const InvalidSettingCase cases[] = {
      {OperationKind::READ_SENSOR_ALTITUDE,
       ConfigurationField::SENSOR_ALTITUDE, 3001U},
      {OperationKind::READ_AMBIENT_PRESSURE,
       ConfigurationField::AMBIENT_PRESSURE, 699U},
      {OperationKind::READ_AMBIENT_PRESSURE,
       ConfigurationField::AMBIENT_PRESSURE, 1201U},
      {OperationKind::READ_ASC_ENABLED, ConfigurationField::ASC_ENABLED, 2U},
      {OperationKind::READ_ASC_INITIAL_PERIOD,
       ConfigurationField::ASC_INITIAL_PERIOD, 2U},
      {OperationKind::READ_ASC_STANDARD_PERIOD,
       ConfigurationField::ASC_STANDARD_PERIOD, 65535U},
  };

  uint32_t requestId = 5000U;
  for (const InvalidSettingCase& item : cases) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10U;
    attachDevice(device, bus, nowMs, requestId++);
    completeJob(device, bus,
                OperationRequest::make(OperationKind::READ_CONFIGURATION),
                nowMs, 1000U, requestId++);
    const ConfigurationSnapshot before = device.configurationSnapshot();
    const HealthSnapshot healthBefore = device.healthSnapshot();

    switch (item.field) {
      case ConfigurationField::SENSOR_ALTITUDE:
        bus.sensorAltitudeM = item.invalidWord;
        break;
      case ConfigurationField::AMBIENT_PRESSURE:
        bus.ambientPressureRaw = item.invalidWord;
        break;
      case ConfigurationField::ASC_ENABLED:
        bus.ascEnabled = item.invalidWord;
        break;
      case ConfigurationField::ASC_INITIAL_PERIOD:
        bus.ascInitialPeriod = item.invalidWord;
        break;
      case ConfigurationField::ASC_STANDARD_PERIOD:
        bus.ascStandardPeriod = item.invalidWord;
        break;
      default:
        TEST_FAIL_MESSAGE("invalid test field");
        break;
    }

    const OperationResult result = completeJob(
        device, bus, OperationRequest::make(item.kind), nowMs, 100U,
        requestId++);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::FAILED),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::COMMAND_FAILED),
                      static_cast<uint8_t>(result.status.code));
    TEST_ASSERT_EQUAL_INT32(item.invalidWord, result.status.detail);
    TEST_ASSERT_EQUAL_UINT32(2U, operationCalls(bus));

    ConfigurationSnapshot expected = before;
    expected.verifiedMask &= static_cast<uint16_t>(
        ~configurationFieldMask(item.field));
    const ConfigurationSnapshot after = device.configurationSnapshot();
    TEST_ASSERT_EQUAL_MEMORY(&expected, &after, sizeof(after));
    TEST_ASSERT_EQUAL_UINT32(healthBefore.totalProtocolFailures + 1U,
                             device.healthSnapshot().totalProtocolFailures);
  }

  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10U;
  attachDevice(device, bus, nowMs, requestId++);
  completeJob(device, bus,
              OperationRequest::make(OperationKind::READ_CONFIGURATION),
              nowMs, 1000U, requestId++);
  const ConfigurationSnapshot before = device.configurationSnapshot();
  bus.ambientPressureRaw = 699U;
  const OperationResult partial = completeJob(
      device, bus, OperationRequest::make(OperationKind::READ_CONFIGURATION),
      nowMs, 1000U, requestId++);
  const uint16_t precedingFields =
      configurationFieldMask(ConfigurationField::TEMPERATURE_OFFSET) |
      configurationFieldMask(ConfigurationField::SENSOR_ALTITUDE);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::PARTIAL),
                    static_cast<uint8_t>(partial.outcome));
  TEST_ASSERT_EQUAL_HEX16(precedingFields, partial.completedFieldMask);
  TEST_ASSERT_EQUAL_UINT32(before.ambientPressurePa,
                           device.configurationSnapshot().ambientPressurePa);
  TEST_ASSERT_EQUAL_HEX16(
      0U, device.configurationSnapshot().verifiedMask &
              configurationFieldMask(ConfigurationField::AMBIENT_PRESSURE));

  ModelTransport fullDomainBus;
  fullDomainBus.temperatureOffsetRaw = 0xFFFFU;
  fullDomainBus.ascTarget = 0xFFFFU;
  Device fullDomainDevice;
  bindDevice(fullDomainDevice, fullDomainBus);
  uint32_t fullDomainNowMs = 10U;
  attachDevice(fullDomainDevice, fullDomainBus, fullDomainNowMs, requestId++);
  const OperationResult fullDomain = completeJob(
      fullDomainDevice, fullDomainBus,
      OperationRequest::make(OperationKind::READ_CONFIGURATION),
      fullDomainNowMs, 1000U, requestId++);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(fullDomain.outcome));
  TEST_ASSERT_EQUAL_INT32(175000,
                          fullDomain.value.configuration.temperatureOffsetMilliC);
  TEST_ASSERT_EQUAL_UINT16(65535U,
                           fullDomain.value.configuration.ascTargetPpm);
}

void test_setting_write_readback_dirty_and_no_unchanged_rewrite() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  completeJob(device, bus, OperationRequest::make(OperationKind::READ_CONFIGURATION), nowMs, 1000,
              70);

  resetOperationTrace(bus);
  const OperationResult changed = completeJob(device, bus,
                                              OperationRequest::setTemperatureOffsetMilliC(4000),
                                              nowMs, 1000, 71);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(changed.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::VERIFIED),
                    static_cast<uint8_t>(changed.effect));
  const ConfigurationSnapshot changedConfig = device.configurationSnapshot();
  const uint16_t offsetMask = configurationFieldMask(ConfigurationField::TEMPERATURE_OFFSET);
  TEST_ASSERT_EQUAL_INT32(4000, changedConfig.temperatureOffsetMilliC);
  TEST_ASSERT_TRUE((changedConfig.verifiedMask & offsetMask) != 0U);
  TEST_ASSERT_TRUE((changedConfig.dirtyMask & offsetMask) != 0U);

  const OperationResult pressure = completeJob(
      device, bus, OperationRequest::setAmbientPressurePa(100000), nowMs, 1000,
      73);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(pressure.outcome));
  TEST_ASSERT_EQUAL_UINT32(100000U, pressure.value.value);
  TEST_ASSERT_EQUAL_UINT32(100000U,
                           pressure.value.configuration.ambientPressurePa);

  const size_t beforeSame = bus.calls;
  const OperationResult same = completeJob(device, bus,
                                           OperationRequest::setTemperatureOffsetMilliC(4000),
                                           nowMs, 1000, 72);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(same.outcome));
  TEST_ASSERT_EQUAL_UINT32(0U,
                           countCommand(bus, beforeSame, cmd::CMD_SET_TEMPERATURE_OFFSET));
}

void test_ambiguous_setting_write_is_not_retried_and_cache_is_invalid() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  completeJob(device, bus, OperationRequest::make(OperationKind::READ_CONFIGURATION), nowMs, 1000,
              80);

  resetOperationTrace(bus);
  // A verified setting change first reads the current value (callbacks 1-2).
  // Callback 3 is the first effectful write and is the ambiguity boundary.
  faultRelativeCall(bus, 3, TransferCode::TIMEOUT, TransferDisposition::INDETERMINATE, true);
  const OperationId id = startJob(device, OperationRequest::setTemperatureOffsetMilliC(5000),
                                  nowMs, nowMs + 1000, 81);
  driveUntilTerminal(device, bus, nowMs);
  const OperationResult result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::UNKNOWN),
                    static_cast<uint8_t>(result.effect));
  TEST_ASSERT_TRUE(result.reconciliationRequired);
  TEST_ASSERT_EQUAL_UINT32(1U,
                           countCommand(bus, bus.operationCallBase,
                                        cmd::CMD_SET_TEMPERATURE_OFFSET));
  const uint16_t mask = configurationFieldMask(ConfigurationField::TEMPERATURE_OFFSET);
  TEST_ASSERT_EQUAL_HEX16(0U, device.configurationSnapshot().verifiedMask & mask);
  TEST_ASSERT_NOT_EQUAL_HEX16(0U,
                             device.configurationSnapshot().dirtyMask & mask);
  TEST_ASSERT_EQUAL_HEX16(device.configurationSnapshot().verifiedMask,
                          result.value.configuration.verifiedMask);
  TEST_ASSERT_EQUAL_HEX16(device.configurationSnapshot().dirtyMask,
                          result.value.configuration.dirtyMask);

  attachDevice(device, bus, nowMs, 82);
  const size_t beforePersist = bus.calls;
  OperationId persistId = {};
  assertNoIoStatus(
      device.start(OperationRequest::persistSettings(),
                   OperationOptions{83, nowMs, nowMs + 2000}, persistId),
      bus, beforePersist, Err::RECONCILIATION_REQUIRED);

  completeJob(device, bus,
              OperationRequest::make(OperationKind::READ_TEMPERATURE_OFFSET),
              nowMs, 100, 84);
  TEST_ASSERT_NOT_EQUAL_HEX16(0U,
                             device.configurationSnapshot().verifiedMask & mask);
  resetOperationTrace(bus);
  const OperationResult persisted = completeJob(
      device, bus, OperationRequest::persistSettings(), nowMs, 2000, 85);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(persisted.outcome));
  TEST_ASSERT_EQUAL_UINT32(
      1U, countCommand(bus, bus.operationCallBase, cmd::CMD_PERSIST_SETTINGS));
}

void test_setting_verification_fault_does_not_reclassify_the_mutation() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  resetOperationTrace(bus);
  faultRelativeCall(bus, 4, TransferCode::TIMEOUT,
                    TransferDisposition::INDETERMINATE, true);
  const OperationId id =
      startJob(device, OperationRequest::setTemperatureOffsetMilliC(5000),
               nowMs, nowMs + 1000, 82);
  driveUntilTerminal(device, bus, nowMs);
  const OperationResult result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::FAILED),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::ACKNOWLEDGED),
                    static_cast<uint8_t>(result.effect));
  TEST_ASSERT_FALSE(result.reconciliationRequired);
  TEST_ASSERT_TRUE(device.isAttached());
  TEST_ASSERT_EQUAL_UINT32(
      1U, countCommand(bus, bus.operationCallBase,
                       cmd::CMD_SET_TEMPERATURE_OFFSET));
  const uint16_t mask =
      configurationFieldMask(ConfigurationField::TEMPERATURE_OFFSET);
  TEST_ASSERT_EQUAL_HEX16(0U,
                          device.configurationSnapshot().verifiedMask & mask);
  TEST_ASSERT_NOT_EQUAL_HEX16(0U,
                             device.configurationSnapshot().dirtyMask & mask);
  TEST_ASSERT_EQUAL_HEX16(device.configurationSnapshot().verifiedMask,
                          result.value.configuration.verifiedMask);
  TEST_ASSERT_EQUAL_HEX16(device.configurationSnapshot().dirtyMask,
                          result.value.configuration.dirtyMask);

  resetOperationTrace(bus);
  const OperationResult retry = completeJob(
      device, bus, OperationRequest::setTemperatureOffsetMilliC(5000), nowMs,
      1000, 83);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(retry.outcome));
  TEST_ASSERT_EQUAL_UINT32(
      0U, countCommand(bus, bus.operationCallBase,
                       cmd::CMD_SET_TEMPERATURE_OFFSET));
  TEST_ASSERT_NOT_EQUAL_HEX16(0U,
                             device.configurationSnapshot().dirtyMask & mask);

  resetOperationTrace(bus);
  const OperationResult persisted = completeJob(
      device, bus, OperationRequest::persistSettings(), nowMs, 2000, 84);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(persisted.outcome));
  TEST_ASSERT_EQUAL_UINT32(
      1U, countCommand(bus, bus.operationCallBase, cmd::CMD_PERSIST_SETTINGS));
  TEST_ASSERT_EQUAL_HEX16(0U, device.configurationSnapshot().dirtyMask & mask);
}

void test_setting_write_crossing_deadline_retains_dirty_evidence() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  resetOperationTrace(bus);
  const uint32_t deadlineMs = nowMs + 100U;
  faultRelativeCall(bus, 3, TransferCode::OK, TransferDisposition::COMPLETE,
                    true, false, deadlineMs);
  const OperationId id = startJob(
      device, OperationRequest::setTemperatureOffsetMilliC(5000), nowMs,
      deadlineMs, 85);
  driveUntilTerminal(device, bus, nowMs);
  const OperationResult result = takeTerminal(device, id);
  const uint16_t mask =
      configurationFieldMask(ConfigurationField::TEMPERATURE_OFFSET);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::TIMED_OUT),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::ACKNOWLEDGED),
                    static_cast<uint8_t>(result.effect));
  TEST_ASSERT_TRUE(result.reconciliationRequired);
  TEST_ASSERT_NOT_EQUAL_HEX16(0U,
                             device.configurationSnapshot().dirtyMask & mask);
  TEST_ASSERT_EQUAL_HEX16(0U,
                          device.configurationSnapshot().verifiedMask & mask);
  TEST_ASSERT_EQUAL_HEX16(device.configurationSnapshot().verifiedMask,
                          result.value.configuration.verifiedMask);
  TEST_ASSERT_EQUAL_HEX16(device.configurationSnapshot().dirtyMask,
                          result.value.configuration.dirtyMask);
}

void test_runtime_pressure_does_not_create_eeprom_work() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  const OperationResult pressure = completeJob(
      device, bus, OperationRequest::setAmbientPressurePa(100000U), nowMs,
      1000U, 86);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(pressure.outcome));
  TEST_ASSERT_EQUAL_HEX16(0U, device.configurationSnapshot().dirtyMask);
  TEST_ASSERT_NOT_EQUAL_HEX16(
      0U, device.configurationSnapshot().verifiedMask &
              configurationFieldMask(ConfigurationField::AMBIENT_PRESSURE));

  resetOperationTrace(bus);
  const OperationResult persist = completeJob(
      device, bus, OperationRequest::persistSettings(), nowMs, 1000U, 87);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(persist.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::NOT_ATTEMPTED),
                    static_cast<uint8_t>(persist.effect));
  TEST_ASSERT_EQUAL_UINT32(0U, operationCalls(bus));
  TEST_ASSERT_EQUAL_UINT32(0U, bus.persistWrites);
}

void test_periodic_admission_rejects_idle_only_work_without_io() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  completeJob(device, bus, OperationRequest::make(OperationKind::START_PERIODIC), nowMs, 100, 90);

  const OperationRequest rejected[] = {
      OperationRequest::make(OperationKind::READ_IDENTITY),
      OperationRequest::make(OperationKind::START_PERIODIC),
      OperationRequest::make(OperationKind::START_LOW_POWER_PERIODIC),
      OperationRequest::make(OperationKind::SINGLE_SHOT),
      OperationRequest::make(OperationKind::SINGLE_SHOT_RHT_ONLY),
      OperationRequest::make(OperationKind::READ_TEMPERATURE_OFFSET),
      OperationRequest::setTemperatureOffsetMilliC(3000),
      OperationRequest::make(OperationKind::READ_SENSOR_ALTITUDE),
      OperationRequest::setSensorAltitudeM(100),
      OperationRequest::make(OperationKind::READ_ASC_ENABLED),
      OperationRequest::setAscEnabled(false),
      OperationRequest::make(OperationKind::READ_ASC_TARGET),
      OperationRequest::setAscTargetPpm(500),
      OperationRequest::make(OperationKind::READ_ASC_INITIAL_PERIOD),
      OperationRequest::setAscInitialPeriodHours(48),
      OperationRequest::make(OperationKind::READ_ASC_STANDARD_PERIOD),
      OperationRequest::setAscStandardPeriodHours(160),
      OperationRequest::make(OperationKind::READ_CONFIGURATION),
      OperationRequest::make(OperationKind::POWER_DOWN),
      OperationRequest::make(OperationKind::WAKE_UP),
      OperationRequest::make(OperationKind::REINIT),
      OperationRequest::make(OperationKind::SELF_TEST),
      OperationRequest::forcedRecalibration(400),
      OperationRequest::persistSettings(),
      OperationRequest::factoryReset(),
      OperationRequest::diagnosticReadWords(0x1234U, 1U),
      OperationRequest::diagnosticWriteCommand(0x1234U),
      OperationRequest::diagnosticWriteWord(0x1234U, 0xBEEFU)};
  for (const OperationRequest& request : rejected) {
    const size_t before = bus.calls;
    OperationId id = {};
    const Status status =
        device.start(request, OperationOptions{91, nowMs, nowMs + 20000}, id);
    TEST_ASSERT_TRUE(status.code == Err::BUSY || status.code == Err::UNSUPPORTED);
    TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
  }

  const OperationRequest allowed[] = {
      OperationRequest::make(OperationKind::READ_DATA_READY),
      OperationRequest::make(OperationKind::FETCH_SAMPLE),
      OperationRequest::make(OperationKind::READ_AMBIENT_PRESSURE),
      OperationRequest::setAmbientPressurePa(100000),
      OperationRequest::make(OperationKind::STOP_PERIODIC)};
  uint32_t requestId = 92U;
  for (const OperationRequest& request : allowed) {
    OperationId id = {};
    TEST_ASSERT_TRUE(
        device.start(request,
                     OperationOptions{requestId++, nowMs, nowMs + 1000U}, id)
            .inProgress());
    TEST_ASSERT_TRUE(device.cancel(id, nowMs).ok());
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_TRUE(device.isAttached());
  }
}

void test_typed_stop_while_idle_is_zero_io_precondition_failure() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10U;
  attachDevice(device, bus, nowMs);
  const size_t before = bus.calls;
  OperationId id = {};
  assertNoIoStatus(
      device.start(OperationRequest::make(OperationKind::STOP_PERIODIC),
                   OperationOptions{99U, nowMs, nowMs + 1000U}, id),
      bus, before, Err::BUSY);
  TEST_ASSERT_TRUE(device.isAttached());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::IDLE),
                    static_cast<uint8_t>(
                        device.runtimeSnapshot().operatingMode));
}

void test_maintenance_confirmation_limits_and_no_retry() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  OperationId id = {};
  const size_t before = bus.calls;
  assertNoIoStatus(device.start(OperationRequest::make(OperationKind::PERSIST_SETTINGS),
                                OperationOptions{100, nowMs, nowMs + 2000}, id),
                   bus, before, Err::CONFIRMATION_REQUIRED);
  assertNoIoStatus(device.start(OperationRequest::make(OperationKind::FACTORY_RESET),
                                OperationOptions{101, nowMs, nowMs + 5000}, id),
                   bus, before, Err::CONFIRMATION_REQUIRED);

  const OperationResult noChangePersist =
      completeJob(device, bus, OperationRequest::persistSettings(), nowMs, 2000, 101);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(noChangePersist.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::NOT_ATTEMPTED),
                    static_cast<uint8_t>(noChangePersist.effect));
  TEST_ASSERT_EQUAL_UINT32(0U, operationCalls(bus));
  TEST_ASSERT_EQUAL_UINT32(0U, bus.persistWrites);

  completeJob(device, bus, OperationRequest::setTemperatureOffsetMilliC(4000), nowMs, 1000, 102);
  resetOperationTrace(bus);
  faultRelativeCall(bus, 1, TransferCode::TIMEOUT, TransferDisposition::INDETERMINATE, true);
  id = startJob(device, OperationRequest::persistSettings(), nowMs, nowMs + 2000, 103);
  driveUntilTerminal(device, bus, nowMs);
  const OperationResult ambiguous = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                    static_cast<uint8_t>(ambiguous.outcome));
  TEST_ASSERT_EQUAL_UINT32(1U,
                           countCommand(bus, bus.operationCallBase, cmd::CMD_PERSIST_SETTINGS));
  TEST_ASSERT_TRUE(device.configurationSnapshot().persistenceIndeterminate);
  TEST_ASSERT_TRUE(device.configurationSnapshot().dirtyMask != 0U);

  attachDevice(device, bus, nowMs, 104);
  TEST_ASSERT_TRUE(device.configurationSnapshot().persistenceIndeterminate);
  const OperationResult blockedRetry =
      completeJob(device, bus, OperationRequest::persistSettings(), nowMs, 2000,
                  105);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                    static_cast<uint8_t>(blockedRetry.outcome));
  TEST_ASSERT_EQUAL_UINT32(0U, operationCalls(bus));
}

void test_transport_contract_failures_are_observable_and_passive() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 100;
  attachDevice(device, bus, nowMs);

  const HealthSnapshot beforeShort = device.healthSnapshot();
  resetOperationTrace(bus);
  faultRelativeCall(bus, 1, TransferCode::OK,
                    TransferDisposition::INDETERMINATE);
  OperationId id = startJob(
      device, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
      nowMs + 100, 120);
  driveUntilTerminal(device, bus, nowMs);
  OperationResult result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::I2C_SHORT_TRANSFER),
                    static_cast<uint8_t>(result.status.code));
  TEST_ASSERT_EQUAL_UINT32(beforeShort.totalTransferFailures + 1U,
                           device.healthSnapshot().totalTransferFailures);
  TEST_ASSERT_EQUAL_UINT32(beforeShort.totalTransferSuccess,
                           device.healthSnapshot().totalTransferSuccess);

  advanceToNextSafe(device, nowMs);
  resetOperationTrace(bus);
  faultRelativeCall(bus, 1, TransferCode::OK, TransferDisposition::COMPLETE,
                    false, false, nowMs - 1U);
  id = startJob(device, OperationRequest::make(OperationKind::READ_IDENTITY),
                nowMs, nowMs + 100, 121);
  driveUntilTerminal(device, bus, nowMs);
  result = takeTerminal(device, id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::I2C_ERROR),
                    static_cast<uint8_t>(result.status.code));
  TEST_ASSERT_EQUAL_UINT32(result.startedMs,
                           device.healthSnapshot().lastTransferErrorMs);
}

void test_transport_contradictions_and_spacing_are_conservative() {
  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);

    resetOperationTrace(bus);
    faultRelativeCall(bus, 3, TransferCode::NACK,
                      TransferDisposition::COMPLETE, true);
    const OperationId id = startJob(
        device, OperationRequest::setTemperatureOffsetMilliC(5000), nowMs,
        nowMs + 1000U, 125);
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::UNKNOWN),
                      static_cast<uint8_t>(result.effect));
    TEST_ASSERT_TRUE(result.reconciliationRequired);
  }

  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    resetOperationTrace(bus);
    faultRelativeCall(bus, 1, TransferCode::NACK,
                      TransferDisposition::NOT_STARTED);
    const OperationId id = startJob(
        device, OperationRequest::make(OperationKind::ATTACH), nowMs,
        nowMs + 5000U, 126);
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_TRUE(result.outcome != OperationOutcome::SUCCEEDED);
    TEST_ASSERT_EQUAL_UINT32(0U, device.healthSnapshot().expectedNacks);
    TEST_ASSERT_FALSE(device.runtimeSnapshot().nextSafeCommandValid);
  }

  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    resetOperationTrace(bus);
    faultRelativeCall(bus, 1, TransferCode::TIMEOUT,
                      TransferDisposition::NO_EFFECT);
    const OperationId failed = startJob(
        device, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
        nowMs + 100U, 127);
    driveUntilTerminal(device, bus, nowMs);
    (void)takeTerminal(device, failed);
    const RuntimeSnapshot spacing = device.runtimeSnapshot();
    TEST_ASSERT_TRUE(spacing.nextSafeCommandValid);
    const size_t before = bus.calls;
    OperationId blocked = {};
    assertNoIoStatus(
        device.start(OperationRequest::make(OperationKind::READ_IDENTITY),
                     OperationOptions{128, nowMs, nowMs + 100U}, blocked),
        bus, before, Err::BUSY);
    nowMs = spacing.nextSafeCommandMs;
    TEST_ASSERT_TRUE(
        device.start(OperationRequest::make(OperationKind::READ_IDENTITY),
                     OperationOptions{128, nowMs, nowMs + 100U}, blocked)
            .inProgress());
    TEST_ASSERT_TRUE(device.cancel(blocked, nowMs).ok());
    (void)takeTerminal(device, blocked);
  }
}

void test_health_channels_track_terminal_protocol_and_transport_truth() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  const HealthSnapshot attached = device.healthSnapshot();
  TEST_ASSERT_EQUAL_UINT32(1U, attached.totalOperationSuccess);
  TEST_ASSERT_TRUE(attached.totalTransferSuccess > 0U);

  bus.badCrc = true;
  const OperationResult crcFailure = completeJob(
      device, bus, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
      100U, 129);
  bus.badCrc = false;
  const HealthSnapshot failed = device.healthSnapshot();
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CRC_MISMATCH),
                    static_cast<uint8_t>(crcFailure.status.code));
  TEST_ASSERT_EQUAL_UINT32(attached.totalProtocolFailures + 1U,
                           failed.totalProtocolFailures);
  TEST_ASSERT_EQUAL_UINT32(attached.totalCrcFailures + 1U,
                           failed.totalCrcFailures);
  TEST_ASSERT_EQUAL_UINT32(attached.totalOperationFailures + 1U,
                           failed.totalOperationFailures);
  TEST_ASSERT_TRUE(failed.lastOperationId == crcFailure.id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationKind::READ_IDENTITY),
                    static_cast<uint8_t>(failed.lastOperationKind));
  TEST_ASSERT_TRUE(failed.lastOperationErrorId == crcFailure.id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationKind::READ_IDENTITY),
                    static_cast<uint8_t>(failed.lastOperationErrorKind));

  OperationId cancelled = {};
  TEST_ASSERT_TRUE(
      device.start(OperationRequest::make(OperationKind::READ_IDENTITY),
                   OperationOptions{130, nowMs, nowMs + 100U}, cancelled)
          .inProgress());
  TEST_ASSERT_TRUE(device.cancel(cancelled, nowMs).ok());
  const OperationResult cancelledResult = takeTerminal(device, cancelled);
  const HealthSnapshot afterCancel = device.healthSnapshot();
  TEST_ASSERT_EQUAL_UINT32(failed.totalOperationCancelled + 1U,
                           afterCancel.totalOperationCancelled);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CANCELLED),
                    static_cast<uint8_t>(afterCancel.lastOperationError.code));
  TEST_ASSERT_TRUE(afterCancel.lastOperationId == cancelledResult.id);

  const OperationResult recovered = completeJob(
      device, bus, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
      100U, 131);
  const HealthSnapshot afterSuccess = device.healthSnapshot();
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(recovered.outcome));
  TEST_ASSERT_EQUAL_UINT32(afterCancel.totalOperationSuccess + 1U,
                           afterSuccess.totalOperationSuccess);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::CANCELLED),
                    static_cast<uint8_t>(afterSuccess.lastOperationError.code));
  TEST_ASSERT_TRUE(afterSuccess.lastOperationErrorId == cancelledResult.id);
  TEST_ASSERT_TRUE(afterSuccess.lastOperationId == recovered.id);
}

void test_diagnostic_commands_are_explicit_and_invalidate_managed_state() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  OperationId id = {};
  const size_t before = bus.calls;
  assertNoIoStatus(
      device.start(OperationRequest::diagnosticReadWords(
                       cmd::CMD_GET_SERIAL_NUMBER, 3),
                   OperationOptions{122, nowMs, nowMs + 100}, id),
      bus, before, Err::UNSUPPORTED);

  OperationRequest oversized =
      OperationRequest::make(OperationKind::DIAGNOSTIC_WRITE_WORD);
  oversized.command = 0x1234U;
  oversized.value = 65536U;
  assertNoIoStatus(device.start(oversized,
                                OperationOptions{123, nowMs, nowMs + 100}, id),
                   bus, before, Err::INVALID_PARAM);

  const OperationResult diagnostic = completeJob(
      device, bus, OperationRequest::diagnosticWriteCommand(0x1234), nowMs,
      100, 124);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(diagnostic.outcome));
  TEST_ASSERT_TRUE(diagnostic.reconciliationRequired);
  TEST_ASSERT_FALSE(device.isAttached());
}

void test_reset_verify_failure_invalidates_runtime_cache() {
  const OperationRequest requests[] = {
      OperationRequest::make(OperationKind::REINIT),
      OperationRequest::factoryReset()};
  uint32_t requestId = 180;
  for (const OperationRequest& request : requests) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs, requestId++);
    completeJob(device, bus,
                OperationRequest::make(OperationKind::START_PERIODIC), nowMs,
                100, requestId++);
    completeJob(device, bus, OperationRequest::make(OperationKind::FETCH_SAMPLE),
                nowMs, 100, requestId++);
    completeJob(device, bus,
                OperationRequest::make(OperationKind::STOP_PERIODIC), nowMs,
                1000, requestId++);
    completeJob(device, bus,
                OperationRequest::make(OperationKind::READ_CONFIGURATION), nowMs,
                1000, requestId++);
    FixedSample sample = {};
    TEST_ASSERT_TRUE(device.peekLatestSample(sample).ok());
    TEST_ASSERT_NOT_EQUAL_HEX16(0U,
                               device.configurationSnapshot().verifiedMask);

    resetOperationTrace(bus);
    faultRelativeCall(bus, 2, TransferCode::TIMEOUT,
                      TransferDisposition::NO_EFFECT);
    const OperationId id =
        startJob(device, request, nowMs, nowMs + 20000U, requestId++);
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::FAILED),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(EffectState::ACKNOWLEDGED),
                      static_cast<uint8_t>(result.effect));
    TEST_ASSERT_TRUE(result.reconciliationRequired);
    TEST_ASSERT_FALSE(device.isAttached());
    TEST_ASSERT_EQUAL_HEX16(0U,
                            device.configurationSnapshot().verifiedMask);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::MEASUREMENT_NOT_READY),
                      static_cast<uint8_t>(device.peekLatestSample(sample).code));
  }
}

void test_wake_and_reattach_failures_never_retain_stale_mode() {
  for (size_t failedStage = 2U; failedStage <= 4U; ++failedStage) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    completeJob(device, bus,
                OperationRequest::make(OperationKind::START_PERIODIC), nowMs,
                100U, static_cast<uint32_t>(200U + failedStage));
    resetOperationTrace(bus);
    faultRelativeCall(bus, failedStage, TransferCode::TIMEOUT,
                      TransferDisposition::NO_EFFECT);
    const OperationId id = startJob(
        device, OperationRequest::make(OperationKind::ATTACH), nowMs,
        nowMs + 5000U, static_cast<uint32_t>(210U + failedStage));
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_TRUE(result.outcome != OperationOutcome::SUCCEEDED);
    TEST_ASSERT_FALSE(device.isAttached());
    TEST_ASSERT_TRUE(device.runtimeSnapshot().reconciliationRequired);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::UNKNOWN),
                      static_cast<uint8_t>(device.runtimeSnapshot().operatingMode));
  }

  for (size_t failedStage = 2U; failedStage <= 3U; ++failedStage) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    completeJob(device, bus, OperationRequest::make(OperationKind::POWER_DOWN),
                nowMs, 100U, static_cast<uint32_t>(220U + failedStage));
    resetOperationTrace(bus);
    faultRelativeCall(bus, failedStage, TransferCode::TIMEOUT,
                      TransferDisposition::NO_EFFECT);
    const OperationId id = startJob(
        device, OperationRequest::make(OperationKind::WAKE_UP), nowMs,
        nowMs + 1000U, static_cast<uint32_t>(230U + failedStage));
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_TRUE(result.outcome != OperationOutcome::SUCCEEDED);
    TEST_ASSERT_FALSE(device.isAttached());
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::UNKNOWN),
                      static_cast<uint8_t>(result.operatingMode));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(ModeEvidence::UNKNOWN),
                      static_cast<uint8_t>(result.modeEvidence));
  }
}

void test_sample_provenance_survives_rebind_and_resets_on_mode_change() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  const uint32_t firstEpoch = device.identity().sensorEpoch;
  completeJob(device, bus, OperationRequest::make(OperationKind::START_PERIODIC),
              nowMs, 100U, 240);
  const OperationResult first = completeJob(
      device, bus, OperationRequest::make(OperationKind::FETCH_SAMPLE), nowMs,
      100U, 241);
  const OperationResult second = completeJob(
      device, bus, OperationRequest::make(OperationKind::FETCH_SAMPLE), nowMs,
      100U, 242);
  TEST_ASSERT_EQUAL_UINT32(1U, first.value.sample.sequence);
  TEST_ASSERT_EQUAL_UINT32(2U, second.value.sample.sequence);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::PERIODIC),
                    static_cast<uint8_t>(first.value.sample.mode));

  completeJob(device, bus, OperationRequest::make(OperationKind::STOP_PERIODIC),
              nowMs, 1000U, 243);
  completeJob(device, bus,
              OperationRequest::make(OperationKind::START_LOW_POWER_PERIODIC),
              nowMs, 100U, 244);
  const OperationResult lowPower = completeJob(
      device, bus, OperationRequest::make(OperationKind::FETCH_SAMPLE), nowMs,
      100U, 245);
  TEST_ASSERT_EQUAL_UINT32(1U, lowPower.value.sample.sequence);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::LOW_POWER_PERIODIC),
                    static_cast<uint8_t>(lowPower.value.sample.mode));

  device.end();
  TEST_ASSERT_TRUE(device.begin(makeConfig(bus)).ok());
  advanceToNextSafe(device, nowMs);
  attachDevice(device, bus, nowMs, 246);
  TEST_ASSERT_TRUE(device.identity().sensorEpoch > firstEpoch);
}

void test_cancelled_long_operation_blocks_rebind_until_sensor_safe() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);
  resetOperationTrace(bus);
  const OperationId selfTest = startJob(
      device, OperationRequest::make(OperationKind::SELF_TEST), nowMs,
      nowMs + 12000U, 250);
  while (operationCalls(bus) == 0U) {
    const PollResult poll = pollChecked(device, bus, nowMs, 1U);
    if (operationCalls(bus) == 0U && poll.nextDueMs != 0U &&
        !timeReached(nowMs, poll.nextDueMs)) {
      nowMs = poll.nextDueMs;
    } else if (operationCalls(bus) == 0U) {
      ++nowMs;
    }
  }
  TEST_ASSERT_TRUE(device.cancel(selfTest, nowMs).ok());
  (void)takeTerminal(device, selfTest);
  const RuntimeSnapshot safety = device.runtimeSnapshot();
  TEST_ASSERT_TRUE(safety.nextSafeCommandValid);
  TEST_ASSERT_FALSE(timeReached(nowMs, safety.nextSafeCommandMs));

  const size_t before = bus.calls;
  OperationId blocked = {};
  assertNoIoStatus(
      device.start(OperationRequest::make(OperationKind::ATTACH),
                   OperationOptions{251, nowMs, nowMs + 20000U}, blocked),
      bus, before, Err::BUSY);
  device.end();
  TEST_ASSERT_TRUE(device.begin(makeConfig(bus)).ok());
  assertNoIoStatus(
      device.start(OperationRequest::make(OperationKind::ATTACH),
                   OperationOptions{252, nowMs, nowMs + 20000U}, blocked),
      bus, before, Err::BUSY);

  nowMs = safety.nextSafeCommandMs;
  TEST_ASSERT_TRUE(
      device.start(OperationRequest::make(OperationKind::ATTACH),
                   OperationOptions{253, nowMs, nowMs + 5000U}, blocked)
          .inProgress());
  TEST_ASSERT_TRUE(device.cancel(blocked, nowMs).ok());
  (void)takeTerminal(device, blocked);
}

void test_factory_reset_uncertainty_blocks_blind_persistence() {
  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    resetOperationTrace(bus);
    faultRelativeCall(bus, 1, TransferCode::TIMEOUT,
                      TransferDisposition::INDETERMINATE, true);
    const OperationId id = startJob(
        device, OperationRequest::factoryReset(), nowMs, nowMs + 5000U, 260);
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_TRUE(result.value.configuration.persistenceIndeterminate);
    TEST_ASSERT_TRUE(device.configurationSnapshot().persistenceIndeterminate);
  }

  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    resetOperationTrace(bus);
    const OperationId id = startJob(
        device, OperationRequest::factoryReset(), nowMs, nowMs + 5000U, 261);
    pollChecked(device, bus, nowMs, 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, operationCalls(bus));
    TEST_ASSERT_TRUE(device.cancel(id, nowMs).ok());
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_TRUE(result.value.configuration.persistenceIndeterminate);
    TEST_ASSERT_TRUE(device.configurationSnapshot().persistenceIndeterminate);
  }
}

void test_identity_changes_and_wrong_variants_require_attach() {
  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    bus.serialWords[2] ^= 0x0001U;
    resetOperationTrace(bus);
    const OperationId id = startJob(
        device, OperationRequest::make(OperationKind::REINIT), nowMs,
        nowMs + 20000U, 190);
    ++nowMs;
    bus.callbackCompletedMs = nowMs;
    const PollResult started = device.poll(nowMs, 1);
    TEST_ASSERT_EQUAL_UINT8(1U, started.callbacksUsed);
    driveUntilTerminal(device, bus, nowMs);
    const OperationResult result = takeTerminal(device, id);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_TRUE(result.reconciliationRequired);
    TEST_ASSERT_FALSE(device.isAttached());
  }

  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    bus.variantWord = 0x0440U;
    const OperationResult result = completeJob(
        device, bus, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
        100, 191);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                      static_cast<uint8_t>(result.status.code));
    TEST_ASSERT_TRUE(result.reconciliationRequired);
    TEST_ASSERT_FALSE(device.isAttached());
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SensorVariant::SCD40),
                      static_cast<uint8_t>(device.identity().variant));
  }

  {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 10;
    attachDevice(device, bus, nowMs);
    completeJob(device, bus, OperationRequest::make(OperationKind::POWER_DOWN),
                nowMs, 100, 192);
    bus.variantWord = 0x0440U;
    const OperationResult result = completeJob(
        device, bus, OperationRequest::make(OperationKind::WAKE_UP), nowMs,
        1000, 193);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                      static_cast<uint8_t>(result.status.code));
    TEST_ASSERT_TRUE(result.reconciliationRequired);
    TEST_ASSERT_FALSE(device.isAttached());
  }
}

void test_passive_offline_health_never_gates_a_later_attempt() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  for (uint32_t i = 0; i < 3; ++i) {
    resetOperationTrace(bus);
    faultRelativeCall(bus, 1, TransferCode::TIMEOUT, TransferDisposition::NO_EFFECT);
    const OperationId id =
        startJob(device, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs,
                 nowMs + 100, 130 + i);
    driveUntilTerminal(device, bus, nowMs);
    TEST_ASSERT_TRUE(takeTerminal(device, id).outcome != OperationOutcome::SUCCEEDED);
    advanceToNextSafe(device, nowMs);
  }
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::OFFLINE),
                    static_cast<uint8_t>(device.healthSnapshot().state));

  resetOperationTrace(bus);
  const OperationResult recovered =
      completeJob(device, bus, OperationRequest::make(OperationKind::READ_IDENTITY), nowMs, 100,
                  140);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(recovered.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::READY),
                    static_cast<uint8_t>(device.healthSnapshot().state));
}

void test_self_test_and_frc_results_are_typed() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10;
  attachDevice(device, bus, nowMs);

  OperationResult selfTest =
      completeJob(device, bus, OperationRequest::make(OperationKind::SELF_TEST), nowMs, 12000,
                  110);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(selfTest.outcome));
  TEST_ASSERT_EQUAL_UINT32(0U, selfTest.value.value);

  bus.frcResult = static_cast<uint16_t>(cmd::FRC_OFFSET_BIAS - 7U);
  OperationResult frc = completeJob(device, bus, OperationRequest::forcedRecalibration(400), nowMs,
                                    2000, 111);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(frc.outcome));
  TEST_ASSERT_EQUAL_INT32(-7, frc.value.signedValue);

  bus.frcResult = cmd::FRC_FAILED;
  frc = completeJob(device, bus, OperationRequest::forcedRecalibration(400), nowMs, 2000, 112);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::FAILED),
                    static_cast<uint8_t>(frc.outcome));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::COMMAND_FAILED),
                    static_cast<uint8_t>(frc.status.code));
}

void test_each_runtime_and_maintenance_stage_fails_without_hidden_retry() {
  enum class Preparation : uint8_t {
    IDLE,
    PERIODIC,
    POWER_DOWN,
    DIRTY_CONFIGURATION
  };
  struct ProcedureCase {
    OperationRequest request;
    Preparation preparation;
  };
  const ProcedureCase procedures[] = {
      {OperationRequest::make(OperationKind::READ_IDENTITY), Preparation::IDLE},
      {OperationRequest::make(OperationKind::START_PERIODIC), Preparation::IDLE},
      {OperationRequest::make(OperationKind::START_LOW_POWER_PERIODIC), Preparation::IDLE},
      {OperationRequest::make(OperationKind::STOP_PERIODIC), Preparation::PERIODIC},
      {OperationRequest::make(OperationKind::READ_DATA_READY), Preparation::IDLE},
      {OperationRequest::make(OperationKind::FETCH_SAMPLE), Preparation::PERIODIC},
      {OperationRequest::make(OperationKind::SINGLE_SHOT), Preparation::IDLE},
      {OperationRequest::make(OperationKind::SINGLE_SHOT_RHT_ONLY), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_TEMPERATURE_OFFSET), Preparation::IDLE},
      {OperationRequest::setTemperatureOffsetMilliC(4000), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_SENSOR_ALTITUDE), Preparation::IDLE},
      {OperationRequest::setSensorAltitudeM(123), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_AMBIENT_PRESSURE), Preparation::IDLE},
      {OperationRequest::setAmbientPressurePa(100000), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_ASC_ENABLED), Preparation::IDLE},
      {OperationRequest::setAscEnabled(false), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_ASC_TARGET), Preparation::IDLE},
      {OperationRequest::setAscTargetPpm(500), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_ASC_INITIAL_PERIOD), Preparation::IDLE},
      {OperationRequest::setAscInitialPeriodHours(48), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_ASC_STANDARD_PERIOD), Preparation::IDLE},
      {OperationRequest::setAscStandardPeriodHours(160), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_CONFIGURATION), Preparation::IDLE},
      {OperationRequest::make(OperationKind::POWER_DOWN), Preparation::IDLE},
      {OperationRequest::make(OperationKind::WAKE_UP), Preparation::POWER_DOWN},
      {OperationRequest::make(OperationKind::REINIT), Preparation::IDLE},
      {OperationRequest::make(OperationKind::SELF_TEST), Preparation::IDLE},
      {OperationRequest::forcedRecalibration(400), Preparation::IDLE},
      {OperationRequest::persistSettings(), Preparation::DIRTY_CONFIGURATION},
      {OperationRequest::factoryReset(), Preparation::IDLE},
      {OperationRequest::diagnosticReadWords(0x1234U, 1U), Preparation::IDLE},
      {OperationRequest::diagnosticWriteCommand(0x1234U), Preparation::IDLE},
      {OperationRequest::diagnosticWriteWord(0x1234U, 0xBEEFU), Preparation::IDLE},
  };

  uint32_t requestId = 300;
  for (const ProcedureCase& procedure : procedures) {
    ModelTransport baselineBus;
    Device baselineDevice;
    bindDevice(baselineDevice, baselineBus);
    uint32_t baselineNow = 10;
    attachDevice(baselineDevice, baselineBus, baselineNow, requestId++);
    if (procedure.preparation == Preparation::PERIODIC) {
      completeJob(baselineDevice, baselineBus,
                  OperationRequest::make(OperationKind::START_PERIODIC),
                  baselineNow, 100, requestId++);
    } else if (procedure.preparation == Preparation::POWER_DOWN) {
      completeJob(baselineDevice, baselineBus,
                  OperationRequest::make(OperationKind::POWER_DOWN),
                  baselineNow, 100, requestId++);
    } else if (procedure.preparation == Preparation::DIRTY_CONFIGURATION) {
      completeJob(baselineDevice, baselineBus,
                  OperationRequest::setTemperatureOffsetMilliC(4000),
                  baselineNow, 1000, requestId++);
    }
    const OperationResult baseline =
        completeJob(baselineDevice, baselineBus, procedure.request, baselineNow,
                    20000, requestId++);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                      static_cast<uint8_t>(baseline.outcome));
    const size_t transferCount = operationCalls(baselineBus);
    TEST_ASSERT_TRUE(transferCount > 0U);
    TEST_ASSERT_TRUE(
        transferCount <= Device::limits(procedure.request.kind).maxCallbacks);
    TEST_ASSERT_TRUE(
        (baseline.completedMs - baseline.startedMs) <=
        Device::limits(procedure.request.kind).maxWaitMs);

    for (size_t failedStage = 1; failedStage <= transferCount; ++failedStage) {
      ModelTransport bus;
      Device device;
      bindDevice(device, bus);
      uint32_t nowMs = 10;
      attachDevice(device, bus, nowMs, requestId++);
      if (procedure.preparation == Preparation::PERIODIC) {
        completeJob(device, bus,
                    OperationRequest::make(OperationKind::START_PERIODIC),
                    nowMs, 100, requestId++);
      } else if (procedure.preparation == Preparation::POWER_DOWN) {
        completeJob(device, bus,
                    OperationRequest::make(OperationKind::POWER_DOWN), nowMs,
                    100, requestId++);
      } else if (procedure.preparation == Preparation::DIRTY_CONFIGURATION) {
        completeJob(device, bus,
                    OperationRequest::setTemperatureOffsetMilliC(4000), nowMs,
                    1000, requestId++);
      }
      resetOperationTrace(bus);
      faultRelativeCall(bus, failedStage, TransferCode::TIMEOUT,
                        TransferDisposition::NO_EFFECT);
      const OperationId id =
          startJob(device, procedure.request, nowMs, nowMs + 20000,
                   requestId++);
      driveUntilTerminal(device, bus, nowMs);
      const OperationResult result = takeTerminal(device, id);
      TEST_ASSERT_TRUE(result.outcome != OperationOutcome::SUCCEEDED);
      TEST_ASSERT_EQUAL_UINT32(failedStage, operationCalls(bus));
      if (procedure.request.kind == OperationKind::READ_CONFIGURATION &&
          result.completedFieldMask != 0U) {
        TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::PARTIAL),
                          static_cast<uint8_t>(result.outcome));
      }
    }
  }
}

void test_cancellation_between_each_transfer_phase_is_terminal() {
  enum class Preparation : uint8_t { IDLE, POWER_DOWN, DIRTY };
  struct Case {
    OperationRequest request;
    Preparation preparation;
  };
  const Case cases[] = {
      {OperationRequest::make(OperationKind::ATTACH), Preparation::IDLE},
      {OperationRequest::setTemperatureOffsetMilliC(4000), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_CONFIGURATION), Preparation::IDLE},
      {OperationRequest::make(OperationKind::SINGLE_SHOT), Preparation::IDLE},
      {OperationRequest::make(OperationKind::WAKE_UP), Preparation::POWER_DOWN},
      {OperationRequest::make(OperationKind::REINIT), Preparation::IDLE},
      {OperationRequest::make(OperationKind::SELF_TEST), Preparation::IDLE},
      {OperationRequest::forcedRecalibration(400), Preparation::IDLE},
      {OperationRequest::persistSettings(), Preparation::DIRTY},
      {OperationRequest::factoryReset(), Preparation::IDLE},
      {OperationRequest::diagnosticReadWords(0x1234U, 1U), Preparation::IDLE},
  };

  uint32_t requestId = 5000U;
  for (const Case& item : cases) {
    const uint8_t maxCallbacks = Device::limits(item.request.kind).maxCallbacks;
    for (uint8_t cancelAfter = 1U; cancelAfter <= maxCallbacks; ++cancelAfter) {
      ModelTransport bus;
      Device device;
      bindDevice(device, bus);
      uint32_t nowMs = 10;
      if (item.request.kind != OperationKind::ATTACH) {
        attachDevice(device, bus, nowMs, requestId++);
      }
      if (item.preparation == Preparation::POWER_DOWN) {
        completeJob(device, bus, OperationRequest::make(OperationKind::POWER_DOWN),
                    nowMs, 100U, requestId++);
      } else if (item.preparation == Preparation::DIRTY) {
        completeJob(device, bus,
                    OperationRequest::setTemperatureOffsetMilliC(3000), nowMs,
                    1000U, requestId++);
      }

      resetOperationTrace(bus);
      const OperationId id = startJob(device, item.request, nowMs,
                                      nowMs + 20000U, requestId++);
      PollResult poll = {};
      for (uint16_t step = 0U; step < 128U; ++step) {
        poll = pollChecked(device, bus, nowMs, 1U);
        if (poll.state == OperationState::RESULT_PENDING ||
            operationCalls(bus) >= cancelAfter) {
          break;
        }
        if (poll.nextDueMs != 0U && !timeReached(nowMs, poll.nextDueMs)) {
          nowMs = poll.nextDueMs;
        } else {
          ++nowMs;
        }
      }
      if (poll.state == OperationState::RESULT_PENDING) {
        (void)takeTerminal(device, id);
        continue;
      }

      const size_t beforeCancel = bus.calls;
      TEST_ASSERT_TRUE(device.cancel(id, nowMs).ok());
      TEST_ASSERT_EQUAL_UINT32(beforeCancel, bus.calls);
      const OperationResult result = takeTerminal(device, id);
      TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                        static_cast<uint8_t>(result.outcome));
      TEST_ASSERT_EQUAL_UINT8(cancelAfter, result.callbacksUsed);
      const size_t afterTake = bus.calls;
      (void)device.poll(nowMs, 4U);
      TEST_ASSERT_EQUAL_UINT32(afterTake, bus.calls);
      if (item.request.kind == OperationKind::SET_TEMPERATURE_OFFSET ||
          item.request.kind == OperationKind::PERSIST_SETTINGS ||
          item.request.kind == OperationKind::FACTORY_RESET) {
        TEST_ASSERT_EQUAL_HEX16(device.configurationSnapshot().dirtyMask,
                                result.value.configuration.dirtyMask);
        TEST_ASSERT_EQUAL(
            device.configurationSnapshot().persistenceIndeterminate,
            result.value.configuration.persistenceIndeterminate);
      }
    }
  }
}

void test_deadline_crossing_at_each_transfer_phase_is_terminal() {
  enum class Preparation : uint8_t { IDLE, POWER_DOWN, DIRTY };
  struct Case {
    OperationRequest request;
    Preparation preparation;
  };
  const Case cases[] = {
      {OperationRequest::make(OperationKind::ATTACH), Preparation::IDLE},
      {OperationRequest::setTemperatureOffsetMilliC(4000), Preparation::IDLE},
      {OperationRequest::make(OperationKind::READ_CONFIGURATION), Preparation::IDLE},
      {OperationRequest::make(OperationKind::SINGLE_SHOT), Preparation::IDLE},
      {OperationRequest::make(OperationKind::WAKE_UP), Preparation::POWER_DOWN},
      {OperationRequest::make(OperationKind::REINIT), Preparation::IDLE},
      {OperationRequest::make(OperationKind::SELF_TEST), Preparation::IDLE},
      {OperationRequest::forcedRecalibration(400), Preparation::IDLE},
      {OperationRequest::persistSettings(), Preparation::DIRTY},
      {OperationRequest::factoryReset(), Preparation::IDLE},
      {OperationRequest::diagnosticReadWords(0x1234U, 1U), Preparation::IDLE},
  };

  uint32_t requestId = 6000U;
  for (const Case& item : cases) {
    const uint8_t maxCallbacks = Device::limits(item.request.kind).maxCallbacks;
    for (uint8_t deadlineStage = 1U; deadlineStage <= maxCallbacks;
         ++deadlineStage) {
      ModelTransport bus;
      Device device;
      bindDevice(device, bus);
      uint32_t nowMs = 10;
      if (item.request.kind != OperationKind::ATTACH) {
        attachDevice(device, bus, nowMs, requestId++);
      }
      if (item.preparation == Preparation::POWER_DOWN) {
        completeJob(device, bus, OperationRequest::make(OperationKind::POWER_DOWN),
                    nowMs, 100U, requestId++);
      } else if (item.preparation == Preparation::DIRTY) {
        completeJob(device, bus,
                    OperationRequest::setTemperatureOffsetMilliC(3000), nowMs,
                    1000U, requestId++);
      }

      resetOperationTrace(bus);
      const uint32_t deadlineMs = nowMs + 20000U;
      faultRelativeCall(bus, deadlineStage, TransferCode::OK,
                        TransferDisposition::COMPLETE, true, true, deadlineMs);
      const OperationId id = startJob(device, item.request, nowMs, deadlineMs,
                                      requestId++);
      driveUntilTerminal(device, bus, nowMs);
      const OperationResult result = takeTerminal(device, id);
      TEST_ASSERT_TRUE(result.outcome != OperationOutcome::SUCCEEDED);
      TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::TIMEOUT),
                        static_cast<uint8_t>(result.status.code));
      TEST_ASSERT_EQUAL_UINT8(deadlineStage, result.callbacksUsed);
      TEST_ASSERT_EQUAL_UINT32(deadlineStage, operationCalls(bus));
      if (item.request.kind == OperationKind::READ_CONFIGURATION &&
          result.completedFieldMask != 0U) {
        TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::PARTIAL),
                          static_cast<uint8_t>(result.outcome));
      }
    }
  }
}

void test_scheduled_wait_topologies_handle_clock_wrap() {
  const OperationRequest requests[] = {
      OperationRequest::make(OperationKind::READ_IDENTITY),
      OperationRequest::setTemperatureOffsetMilliC(4000),
      OperationRequest::make(OperationKind::SINGLE_SHOT),
      OperationRequest::make(OperationKind::REINIT),
      OperationRequest::make(OperationKind::SELF_TEST),
      OperationRequest::factoryReset(),
      OperationRequest::diagnosticReadWords(0x1234U, 1U),
  };

  uint32_t requestId = 7000U;
  for (const OperationRequest& request : requests) {
    ModelTransport bus;
    Device device;
    bindDevice(device, bus);
    uint32_t nowMs = 0xFFFFFDCBU;
    attachDevice(device, bus, nowMs, requestId++);
    TEST_ASSERT_TRUE(nowMs >= 0xFFFFF000U);
    const uint32_t startedMs = nowMs;
    const OperationResult result = completeJob(device, bus, request, nowMs,
                                               30000U, requestId++);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                      static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_TRUE(nowMs < startedMs);
  }
}

void test_end_is_zero_io_and_cancels_active_work() {
  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  OperationId id = {};
  TEST_ASSERT_TRUE(device.start(OperationRequest::make(OperationKind::ATTACH),
                                OperationOptions{120, 10, 5000}, id)
                       .inProgress());
  const PollResult waiting = device.poll(20, 0);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationState::ACTIVE),
                    static_cast<uint8_t>(waiting.state));
  const size_t before = bus.calls;
  device.end();
  TEST_ASSERT_EQUAL_UINT32(before, bus.calls);
  TEST_ASSERT_FALSE(device.isBound());
  TEST_ASSERT_FALSE(device.isAttached());

  OperationResult result = {};
  TEST_ASSERT_TRUE(device.takeResult(id, result).ok());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                    static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_UINT32(20U, result.completedMs);
}

void test_rebind_uses_only_new_transport_context() {
  ModelTransport busA;
  Device device;
  bindDevice(device, busA);
  device.end();

  ModelTransport busB;
  TEST_ASSERT_TRUE(device.begin(makeConfig(busB)).ok());
  uint32_t nowMs = 10;
  attachDevice(device, busB, nowMs);
  TEST_ASSERT_EQUAL_UINT32(0U, busA.calls);
  TEST_ASSERT_TRUE(busB.calls > 0U);
}

void test_helper_boundaries_and_extreme_float_inputs() {
  const uint8_t crcVector[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, crc8(crcVector, sizeof(crcVector)));
  const uint8_t scd40Variant[2] = {0x04U, 0x40U};
  const uint8_t scd41Variant[2] = {0x14U, 0x40U};
  const uint8_t scd43Variant[2] = {0x54U, 0x41U};
  const uint8_t serialWord0[2] = {0xF8U, 0x96U};
  const uint8_t serialWord1[2] = {0x9FU, 0x07U};
  const uint8_t serialWord2[2] = {0x3BU, 0xBEU};
  TEST_ASSERT_EQUAL_HEX8(0x3FU, crc8(scd40Variant, sizeof(scd40Variant)));
  TEST_ASSERT_EQUAL_HEX8(0x51U, crc8(scd41Variant, sizeof(scd41Variant)));
  TEST_ASSERT_EQUAL_HEX8(0xE9U, crc8(scd43Variant, sizeof(scd43Variant)));
  TEST_ASSERT_EQUAL_HEX8(0x31U, crc8(serialWord0, sizeof(serialWord0)));
  TEST_ASSERT_EQUAL_HEX8(0xC2U, crc8(serialWord1, sizeof(serialWord1)));
  TEST_ASSERT_EQUAL_HEX8(0x89U, crc8(serialWord2, sizeof(serialWord2)));
  TEST_ASSERT_FALSE(Device::isDataReady(0));
  TEST_ASSERT_TRUE(Device::isDataReady(0x0001));
  TEST_ASSERT_TRUE(Device::isDataReady(0x0400));
  TEST_ASSERT_FALSE(Device::isDataReady(0x0800));
  TEST_ASSERT_FALSE(Device::isDataReady(0x8000));

  TEST_ASSERT_EQUAL_INT32(-45000, Device::convertTemperatureMilliC(0));
  TEST_ASSERT_EQUAL_INT32(25003, Device::convertTemperatureMilliC(0x6667U));
  TEST_ASSERT_EQUAL_INT32(130000, Device::convertTemperatureMilliC(65535));
  TEST_ASSERT_EQUAL_UINT32(0U, Device::convertHumidityMilliPercent(0));
  TEST_ASSERT_EQUAL_UINT32(37002U,
                           Device::convertHumidityMilliPercent(0x5EB9U));
  TEST_ASSERT_EQUAL_UINT32(100000U,
                           Device::convertHumidityMilliPercent(65535));
  TEST_ASSERT_FLOAT_WITHIN(0.001F, -45.0F, Device::convertTemperatureC(0));
  TEST_ASSERT_FLOAT_WITHIN(0.001F, 100.0F, Device::convertHumidityPct(65535));

  uint16_t encoded = 0xAAAA;
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(Device::encodeTemperatureOffsetC(NAN, encoded).code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(Device::encodeTemperatureOffsetC(INFINITY, encoded).code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(Device::encodeTemperatureOffsetC(FLT_MAX, encoded).code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(Device::encodeTemperatureOffsetC(-1.0F, encoded).code));
  TEST_ASSERT_TRUE(Device::encodeTemperatureOffsetMilliC(0, encoded).ok());
  TEST_ASSERT_EQUAL_HEX16(0x0000, encoded);
  TEST_ASSERT_TRUE(Device::encodeTemperatureOffsetMilliC(4000, encoded).ok());
  TEST_ASSERT_EQUAL_HEX16(0x05DA, encoded);
  TEST_ASSERT_TRUE(Device::encodeTemperatureOffsetMilliC(20000, encoded).ok());
  TEST_ASSERT_EQUAL_HEX16(0x1D42, encoded);
  TEST_ASSERT_TRUE(Device::encodeTemperatureOffsetMilliC(175000, encoded).ok());
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, encoded);
  TEST_ASSERT_TRUE(Device::encodeTemperatureOffsetC(175.0F, encoded).ok());
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, encoded);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(
                        Device::encodeTemperatureOffsetMilliC(175001, encoded).code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(
                        Device::encodeTemperatureOffsetC(175.001F, encoded).code));

  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(Device::encodeAmbientPressurePa(69999, encoded).code));
  TEST_ASSERT_TRUE(Device::encodeAmbientPressurePa(70000, encoded).ok());
  TEST_ASSERT_EQUAL_UINT16(700U, encoded);
  TEST_ASSERT_TRUE(Device::encodeAmbientPressurePa(70099, encoded).ok());
  TEST_ASSERT_EQUAL_UINT16(700U, encoded);
  TEST_ASSERT_TRUE(Device::encodeAmbientPressurePa(70100, encoded).ok());
  TEST_ASSERT_EQUAL_UINT16(701U, encoded);
  TEST_ASSERT_TRUE(Device::encodeAmbientPressurePa(120000, encoded).ok());
  TEST_ASSERT_EQUAL_UINT16(1200U, encoded);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(Device::encodeAmbientPressurePa(120001, encoded).code));

  ModelTransport bus;
  Device device;
  bindDevice(device, bus);
  uint32_t nowMs = 10U;
  attachDevice(device, bus, nowMs, 9000U);
  completeJob(device, bus, OperationRequest::setAscTargetPpm(0U), nowMs,
              100U, 9001U);
  TEST_ASSERT_EQUAL_UINT16(0U, bus.ascTarget);
  completeJob(device, bus, OperationRequest::setAscTargetPpm(65535U), nowMs,
              100U, 9002U);
  TEST_ASSERT_EQUAL_UINT16(65535U, bus.ascTarget);
  const OperationResult frc = completeJob(
      device, bus, OperationRequest::forcedRecalibration(0U), nowMs, 1000U,
      9003U);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(frc.outcome));
  const OperationResult frcMax = completeJob(
      device, bus, OperationRequest::forcedRecalibration(65535U), nowMs,
      1000U, 9004U);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                    static_cast<uint8_t>(frcMax.outcome));
}

void test_cli_diagnostic_workflows_are_bounded_and_deterministic() {
  scd41_cli::DiagnosticWorkflow workflow;
  TEST_ASSERT_FALSE(workflow.begin(
      scd41_cli::WorkflowKind::STRESS, 0U, OperatingMode::IDLE));
  TEST_ASSERT_FALSE(workflow.begin(
      scd41_cli::WorkflowKind::STRESS,
      scd41_cli::MAX_STRESS_CYCLES + 1U, OperatingMode::IDLE));
  TEST_ASSERT_FALSE(workflow.begin(
      scd41_cli::WorkflowKind::SELFCHECK, 1U,
      OperatingMode::PERIODIC));

  TEST_ASSERT_TRUE(workflow.begin(
      scd41_cli::WorkflowKind::STRESS_MIX, 2U,
      OperatingMode::PERIODIC));
  TEST_ASSERT_EQUAL_UINT32(6U, workflow.totalSteps());
  const OperationKind periodicSequence[] = {
      OperationKind::READ_DATA_READY, OperationKind::FETCH_SAMPLE,
      OperationKind::READ_AMBIENT_PRESSURE, OperationKind::READ_DATA_READY,
      OperationKind::FETCH_SAMPLE, OperationKind::READ_AMBIENT_PRESSURE};
  for (uint32_t i = 0U; i < 6U; ++i) {
    OperationRequest request;
    TEST_ASSERT_TRUE(workflow.nextRequest(request));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(periodicSequence[i]),
                      static_cast<uint8_t>(request.kind));
    const OperationId id{100U + i, 200U + i};
    TEST_ASSERT_TRUE(workflow.markStarted(id, request.kind));
    OperationResult result;
    result.id = id;
    result.kind = request.kind;
    result.outcome = request.kind == OperationKind::FETCH_SAMPLE
                         ? OperationOutcome::NO_DATA
                         : OperationOutcome::SUCCEEDED;
    TEST_ASSERT_TRUE(workflow.acceptResult(result));
  }
  TEST_ASSERT_TRUE(workflow.finished());
  TEST_ASSERT_EQUAL_UINT32(4U, workflow.passed());
  TEST_ASSERT_EQUAL_UINT32(2U, workflow.warnings());
  TEST_ASSERT_EQUAL_UINT32(0U, workflow.failed());
  TEST_ASSERT_EQUAL_UINT32(2U, workflow.completedCycles());

  workflow.clear();
  TEST_ASSERT_TRUE(workflow.begin(
      scd41_cli::WorkflowKind::SELFCHECK, 1U, OperatingMode::IDLE));
  TEST_ASSERT_EQUAL_UINT32(4U, workflow.totalSteps());
  const OperationKind selfcheckSequence[] = {
      OperationKind::READ_IDENTITY, OperationKind::READ_SENSOR_VARIANT,
      OperationKind::READ_CONFIGURATION, OperationKind::SELF_TEST};
  for (uint32_t i = 0U; i < 4U; ++i) {
    OperationRequest request;
    TEST_ASSERT_TRUE(workflow.nextRequest(request));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(selfcheckSequence[i]),
                      static_cast<uint8_t>(request.kind));
    const OperationId id{300U + i, 400U + i};
    TEST_ASSERT_TRUE(workflow.markStarted(id, request.kind));
    OperationResult result;
    result.id = id;
    result.kind = request.kind;
    result.outcome = OperationOutcome::SUCCEEDED;
    TEST_ASSERT_TRUE(workflow.acceptResult(result));
  }
  TEST_ASSERT_TRUE(workflow.finished());
  TEST_ASSERT_EQUAL_UINT32(4U, workflow.passed());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_status_and_public_type_contracts);
  RUN_TEST(test_public_enum_name_helpers_are_exhaustive);
  RUN_TEST(test_public_health_compatibility_accessors_match_transfer_channel);
  RUN_TEST(test_begin_is_zero_io_and_validates_before_rebinding);
  RUN_TEST(test_config_validation_boundaries_are_zero_io);
  RUN_TEST(test_start_is_zero_io_and_result_backpressure_is_exact);
  RUN_TEST(test_attach_budget_waiting_and_expected_wake_nack);
  RUN_TEST(test_poll_budget_zero_never_touches_transport);
  RUN_TEST(test_exactly_once_and_stale_result_do_not_modify_output);
  RUN_TEST(test_deadline_before_first_transfer_and_exact_boundary);
  RUN_TEST(test_deadline_after_callback_reports_timeout_without_retry);
  RUN_TEST(test_deadline_and_waits_handle_u32_wrap);
  RUN_TEST(test_owner_clock_cannot_move_backwards);
  RUN_TEST(test_cancel_before_io_is_exact_and_prevents_stale_completion);
  RUN_TEST(test_cancel_after_effectful_write_requires_reconciliation_and_no_retry);
  RUN_TEST(test_attach_failure_at_each_transfer_is_terminal_and_bounded);
  RUN_TEST(test_attach_converges_from_known_modes_and_hotplug);
  RUN_TEST(test_hotplug_attach_starts_a_new_cache_epoch);
  RUN_TEST(test_read_identity_response_fault_and_crc_are_atomic);
  RUN_TEST(test_dedicated_variant_command_contract_and_strict_failures);
  RUN_TEST(test_periodic_fetch_phase_faults_preserve_last_sample);
  RUN_TEST(test_not_ready_fetch_is_terminal_no_data_without_hidden_retry);
  RUN_TEST(test_fixed_point_sample_take_and_peek_contract);
  RUN_TEST(test_configuration_read_marks_fields_only_after_complete_crc);
  RUN_TEST(test_invalid_returned_settings_are_not_published_or_cached);
  RUN_TEST(test_setting_write_readback_dirty_and_no_unchanged_rewrite);
  RUN_TEST(test_ambiguous_setting_write_is_not_retried_and_cache_is_invalid);
  RUN_TEST(test_setting_verification_fault_does_not_reclassify_the_mutation);
  RUN_TEST(test_setting_write_crossing_deadline_retains_dirty_evidence);
  RUN_TEST(test_runtime_pressure_does_not_create_eeprom_work);
  RUN_TEST(test_periodic_admission_rejects_idle_only_work_without_io);
  RUN_TEST(test_typed_stop_while_idle_is_zero_io_precondition_failure);
  RUN_TEST(test_maintenance_confirmation_limits_and_no_retry);
  RUN_TEST(test_transport_contract_failures_are_observable_and_passive);
  RUN_TEST(test_transport_contradictions_and_spacing_are_conservative);
  RUN_TEST(test_health_channels_track_terminal_protocol_and_transport_truth);
  RUN_TEST(test_diagnostic_commands_are_explicit_and_invalidate_managed_state);
  RUN_TEST(test_reset_verify_failure_invalidates_runtime_cache);
  RUN_TEST(test_wake_and_reattach_failures_never_retain_stale_mode);
  RUN_TEST(test_sample_provenance_survives_rebind_and_resets_on_mode_change);
  RUN_TEST(test_cancelled_long_operation_blocks_rebind_until_sensor_safe);
  RUN_TEST(test_factory_reset_uncertainty_blocks_blind_persistence);
  RUN_TEST(test_identity_changes_and_wrong_variants_require_attach);
  RUN_TEST(test_passive_offline_health_never_gates_a_later_attempt);
  RUN_TEST(test_self_test_and_frc_results_are_typed);
  RUN_TEST(test_each_runtime_and_maintenance_stage_fails_without_hidden_retry);
  RUN_TEST(test_cancellation_between_each_transfer_phase_is_terminal);
  RUN_TEST(test_deadline_crossing_at_each_transfer_phase_is_terminal);
  RUN_TEST(test_scheduled_wait_topologies_handle_clock_wrap);
  RUN_TEST(test_end_is_zero_io_and_cancels_active_work);
  RUN_TEST(test_rebind_uses_only_new_transport_context);
  RUN_TEST(test_helper_boundaries_and_extreme_float_inputs);
  RUN_TEST(test_cli_diagnostic_workflows_are_bounded_and_deterministic);
  return UNITY_END();
}
