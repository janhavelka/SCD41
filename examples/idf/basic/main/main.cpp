/// @file main.cpp
/// @brief Native ESP-IDF owner-safe SCD41 CLI example.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/select.h>
#include <unistd.h>

#include "IdfI2cTransport.h"
#include "SCD41/SCD41.h"

#ifndef SCD41_IDF_I2C_SDA
#define SCD41_IDF_I2C_SDA 8
#endif
#ifndef SCD41_IDF_I2C_SCL
#define SCD41_IDF_I2C_SCL 9
#endif
#ifndef SCD41_IDF_I2C_FREQ_HZ
#define SCD41_IDF_I2C_FREQ_HZ 400000
#endif

namespace {

constexpr size_t CLI_LINE_CAPACITY = 128U;
constexpr size_t HELP_COMMAND_WIDTH = 32U;
constexpr uint8_t SCD41_ADDRESS = SCD41::cmd::I2C_ADDRESS;
constexpr gpio_num_t I2C_SDA = static_cast<gpio_num_t>(SCD41_IDF_I2C_SDA);
constexpr gpio_num_t I2C_SCL = static_cast<gpio_num_t>(SCD41_IDF_I2C_SCL);
constexpr uint32_t I2C_FREQUENCY_HZ = SCD41_IDF_I2C_FREQ_HZ;
constexpr char LOG_COLOR_RESET[] = "\033[0m";
constexpr char LOG_COLOR_RED[] = "\033[31m";
constexpr char LOG_COLOR_GREEN[] = "\033[32m";
constexpr char LOG_COLOR_YELLOW[] = "\033[33m";
constexpr char LOG_COLOR_CYAN[] = "\033[36m";

#define LOG_PRINT(color, tag, fmt, ...) \
  do { std::printf("%s[%s]%s " fmt "\n", color, tag, LOG_COLOR_RESET, ##__VA_ARGS__); } while (0)
#define LOGI(fmt, ...) LOG_PRINT(LOG_COLOR_CYAN, "I", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_PRINT(LOG_COLOR_YELLOW, "W", fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG_PRINT(LOG_COLOR_RED, "E", fmt, ##__VA_ARGS__)

struct Line {
  static constexpr size_t npos = static_cast<size_t>(-1);
  char text[CLI_LINE_CAPACITY]{};
  size_t length = 0;

  void clear() { length = 0; text[0] = '\0'; }
  bool assign(const char* source, size_t count) {
    if (source == nullptr || count >= CLI_LINE_CAPACITY) { clear(); return false; }
    if (count > 0U) std::memcpy(text, source, count);
    length = count;
    text[length] = '\0';
    return true;
  }
  bool empty() const { return length == 0U; }
  const char* c_str() const { return text; }
  char operator[](size_t index) const { return index < length ? text[index] : '\0'; }
  size_t find(char needle) const {
    for (size_t i = 0; i < length; ++i) if (text[i] == needle) return i;
    return npos;
  }
};

bool operator==(const Line& left, const char* right) {
  return std::strcmp(left.c_str(), right == nullptr ? "" : right) == 0;
}
bool operator!=(const Line& left, const char* right) { return !(left == right); }

void trimCopy(const Line& input, Line& output) {
  size_t first = 0;
  while (first < input.length && (input[first] == ' ' || input[first] == '\t')) ++first;
  size_t last = input.length;
  while (last > first && (input[last - 1U] == ' ' || input[last - 1U] == '\t')) --last;
  (void)output.assign(input.c_str() + first, last - first);
}

bool splitHeadTail(const Line& input, Line& head, Line& tail) {
  Line trimmed;
  trimCopy(input, trimmed);
  const size_t split = trimmed.find(' ');
  if (split == Line::npos) {
    head = trimmed;
    tail.clear();
  } else {
    Line rawTail;
    (void)head.assign(trimmed.c_str(), split);
    (void)rawTail.assign(trimmed.c_str() + split + 1U,
                         trimmed.length - split - 1U);
    trimCopy(rawTail, tail);
  }
  return !head.empty();
}

bool parseU32(const Line& token, uint32_t& value) {
  if (token.empty() || token[0] == '-') return false;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0') return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseU16(const Line& token, uint16_t& value) {
  uint32_t parsed = 0;
  if (!parseU32(token, parsed) || parsed > 0xFFFFU) return false;
  value = static_cast<uint16_t>(parsed);
  return true;
}

bool parseI32(const Line& token, int32_t& value) {
  if (token.empty()) return false;
  char* end = nullptr;
  const long parsed = std::strtol(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0') return false;
  value = static_cast<int32_t>(parsed);
  return true;
}

bool parseBool(const Line& token, bool& value) {
  if (token == "1" || token == "on" || token == "true") { value = true; return true; }
  if (token == "0" || token == "off" || token == "false") { value = false; return true; }
  return false;
}

bool readLine(Line& output) {
  static char buffer[CLI_LINE_CAPACITY]{};
  static size_t length = 0;
  static bool overflow = false;
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(STDIN_FILENO, &readSet);
  timeval timeout{};
  const int ready = select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &timeout);
  if (ready <= 0 || !FD_ISSET(STDIN_FILENO, &readSet)) return false;

  char value = '\0';
  while (read(STDIN_FILENO, &value, 1) == 1) {
    if (value == '\b' || value == 0x7F) {
      if (!overflow && length > 0U) --length;
      continue;
    }
    if (value == '\r' || value == '\n') {
      if (overflow) {
        overflow = false;
        length = 0;
        LOGW("Command too long; discarded");
        return false;
      }
      Line raw;
      (void)raw.assign(buffer, length);
      trimCopy(raw, output);
      length = 0;
      return !output.empty();
    }
    if (!overflow) {
      if (length + 1U < CLI_LINE_CAPACITY) buffer[length++] = value;
      else overflow = true;
    }
  }
  return false;
}

const char* errName(SCD41::Err value) {
  switch (value) {
    case SCD41::Err::OK: return "OK";
    case SCD41::Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case SCD41::Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case SCD41::Err::INVALID_PARAM: return "INVALID_PARAM";
    case SCD41::Err::BUSY: return "BUSY";
    case SCD41::Err::IN_PROGRESS: return "IN_PROGRESS";
    case SCD41::Err::RESULT_NOT_READY: return "RESULT_NOT_READY";
    case SCD41::Err::STALE_RESULT: return "STALE_RESULT";
    case SCD41::Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case SCD41::Err::MEASUREMENT_NOT_READY: return "MEASUREMENT_NOT_READY";
    case SCD41::Err::CRC_MISMATCH: return "CRC_MISMATCH";
    case SCD41::Err::COMMAND_FAILED: return "COMMAND_FAILED";
    case SCD41::Err::UNSUPPORTED: return "UNSUPPORTED";
    case SCD41::Err::TIMEOUT: return "TIMEOUT";
    case SCD41::Err::CANCELLED: return "CANCELLED";
    case SCD41::Err::PARTIAL: return "PARTIAL";
    case SCD41::Err::INDETERMINATE: return "INDETERMINATE";
    case SCD41::Err::CONFIRMATION_REQUIRED: return "CONFIRMATION_REQUIRED";
    case SCD41::Err::RECONCILIATION_REQUIRED: return "RECONCILIATION_REQUIRED";
    case SCD41::Err::I2C_ERROR: return "I2C_ERROR";
    case SCD41::Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case SCD41::Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case SCD41::Err::I2C_NACK_READ: return "I2C_NACK_READ";
    case SCD41::Err::I2C_NACK: return "I2C_NACK";
    case SCD41::Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case SCD41::Err::I2C_BUS: return "I2C_BUS";
    case SCD41::Err::I2C_SHORT_TRANSFER: return "I2C_SHORT_TRANSFER";
    case SCD41::Err::OFFLINE: return "OFFLINE";
  }
  return "UNKNOWN";
}

const char* stateName(SCD41::DriverState value) {
  switch (value) {
    case SCD41::DriverState::UNINIT: return "UNINIT";
    case SCD41::DriverState::READY: return "READY";
    case SCD41::DriverState::DEGRADED: return "DEGRADED";
    case SCD41::DriverState::OFFLINE: return "OFFLINE";
  }
  return "UNKNOWN";
}

const char* modeName(SCD41::OperatingMode value) {
  switch (value) {
    case SCD41::OperatingMode::UNKNOWN: return "UNKNOWN";
    case SCD41::OperatingMode::IDLE: return "IDLE";
    case SCD41::OperatingMode::PERIODIC: return "PERIODIC";
    case SCD41::OperatingMode::LOW_POWER_PERIODIC: return "LOW_POWER_PERIODIC";
    case SCD41::OperatingMode::POWER_DOWN: return "POWER_DOWN";
  }
  return "UNKNOWN";
}

const char* operationName(SCD41::OperationKind value) {
  switch (value) {
    case SCD41::OperationKind::NONE: return "NONE";
    case SCD41::OperationKind::ATTACH: return "ATTACH";
    case SCD41::OperationKind::READ_IDENTITY: return "READ_IDENTITY";
    case SCD41::OperationKind::START_PERIODIC: return "START_PERIODIC";
    case SCD41::OperationKind::START_LOW_POWER_PERIODIC: return "START_LOW_POWER_PERIODIC";
    case SCD41::OperationKind::STOP_PERIODIC: return "STOP_PERIODIC";
    case SCD41::OperationKind::READ_DATA_READY: return "READ_DATA_READY";
    case SCD41::OperationKind::FETCH_SAMPLE: return "FETCH_SAMPLE";
    case SCD41::OperationKind::SINGLE_SHOT: return "SINGLE_SHOT";
    case SCD41::OperationKind::SINGLE_SHOT_RHT_ONLY: return "SINGLE_SHOT_RHT_ONLY";
    case SCD41::OperationKind::READ_TEMPERATURE_OFFSET: return "READ_TEMPERATURE_OFFSET";
    case SCD41::OperationKind::SET_TEMPERATURE_OFFSET: return "SET_TEMPERATURE_OFFSET";
    case SCD41::OperationKind::READ_SENSOR_ALTITUDE: return "READ_SENSOR_ALTITUDE";
    case SCD41::OperationKind::SET_SENSOR_ALTITUDE: return "SET_SENSOR_ALTITUDE";
    case SCD41::OperationKind::READ_AMBIENT_PRESSURE: return "READ_AMBIENT_PRESSURE";
    case SCD41::OperationKind::SET_AMBIENT_PRESSURE: return "SET_AMBIENT_PRESSURE";
    case SCD41::OperationKind::READ_ASC_ENABLED: return "READ_ASC_ENABLED";
    case SCD41::OperationKind::SET_ASC_ENABLED: return "SET_ASC_ENABLED";
    case SCD41::OperationKind::READ_ASC_TARGET: return "READ_ASC_TARGET";
    case SCD41::OperationKind::SET_ASC_TARGET: return "SET_ASC_TARGET";
    case SCD41::OperationKind::READ_ASC_INITIAL_PERIOD: return "READ_ASC_INITIAL_PERIOD";
    case SCD41::OperationKind::SET_ASC_INITIAL_PERIOD: return "SET_ASC_INITIAL_PERIOD";
    case SCD41::OperationKind::READ_ASC_STANDARD_PERIOD: return "READ_ASC_STANDARD_PERIOD";
    case SCD41::OperationKind::SET_ASC_STANDARD_PERIOD: return "SET_ASC_STANDARD_PERIOD";
    case SCD41::OperationKind::READ_CONFIGURATION: return "READ_CONFIGURATION";
    case SCD41::OperationKind::POWER_DOWN: return "POWER_DOWN";
    case SCD41::OperationKind::WAKE_UP: return "WAKE_UP";
    case SCD41::OperationKind::REINIT: return "REINIT";
    case SCD41::OperationKind::SELF_TEST: return "SELF_TEST";
    case SCD41::OperationKind::FORCED_RECALIBRATION: return "FORCED_RECALIBRATION";
    case SCD41::OperationKind::PERSIST_SETTINGS: return "PERSIST_SETTINGS";
    case SCD41::OperationKind::FACTORY_RESET: return "FACTORY_RESET";
    case SCD41::OperationKind::DIAGNOSTIC_READ_WORDS: return "DIAGNOSTIC_READ_WORDS";
    case SCD41::OperationKind::DIAGNOSTIC_WRITE_COMMAND: return "DIAGNOSTIC_WRITE_COMMAND";
    case SCD41::OperationKind::DIAGNOSTIC_WRITE_WORD: return "DIAGNOSTIC_WRITE_WORD";
  }
  return "UNKNOWN";
}

const char* outcomeName(SCD41::OperationOutcome value) {
  switch (value) {
    case SCD41::OperationOutcome::SUCCEEDED: return "SUCCEEDED";
    case SCD41::OperationOutcome::NO_DATA: return "NO_DATA";
    case SCD41::OperationOutcome::FAILED: return "FAILED";
    case SCD41::OperationOutcome::CANCELLED: return "CANCELLED";
    case SCD41::OperationOutcome::TIMED_OUT: return "TIMED_OUT";
    case SCD41::OperationOutcome::PARTIAL: return "PARTIAL";
    case SCD41::OperationOutcome::INDETERMINATE: return "INDETERMINATE";
  }
  return "UNKNOWN";
}

const char* effectName(SCD41::EffectState value) {
  switch (value) {
    case SCD41::EffectState::NONE: return "NONE";
    case SCD41::EffectState::NOT_ATTEMPTED: return "NOT_ATTEMPTED";
    case SCD41::EffectState::ATTEMPTED: return "ATTEMPTED";
    case SCD41::EffectState::ACKNOWLEDGED: return "ACKNOWLEDGED";
    case SCD41::EffectState::VERIFIED: return "VERIFIED";
    case SCD41::EffectState::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* variantName(SCD41::SensorVariant value) {
  switch (value) {
    case SCD41::SensorVariant::UNKNOWN: return "UNKNOWN";
    case SCD41::SensorVariant::SCD40: return "SCD40";
    case SCD41::SensorVariant::SCD41: return "SCD41";
    case SCD41::SensorVariant::SCD42: return "SCD42";
    case SCD41::SensorVariant::SCD43: return "SCD43";
  }
  return "UNKNOWN";
}

void printHelpHeader(const char* title) { std::printf("%s=== %s ===%s\n", LOG_COLOR_CYAN, title, LOG_COLOR_RESET); }
void printHelpSection(const char* title) { std::printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET); }
void printHelpItem(const char* command, const char* description) {
  std::printf("  %s%-*s%s - %s\n", LOG_COLOR_CYAN,
              static_cast<int>(HELP_COMMAND_WIDTH), command, LOG_COLOR_RESET,
              description);
}
void printPrompt() { std::printf("> "); std::fflush(stdout); }

SCD41::SCD41 device;
SCD41::Config config;
SCD41::OperationResult lastResult;
bool lastResultValid = false;
uint32_t nextRequestId = 1U;
IdfI2cContext i2cContext;
i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t sensorHandle = nullptr;

uint32_t idfNowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000LL); }

uint32_t operationBudgetMs(SCD41::OperationKind kind) {
  const SCD41::OperationLimits limits = SCD41::SCD41::limits(kind);
  return limits.maxWaitMs +
         static_cast<uint32_t>(limits.maxCallbacks) * config.transferTimeoutMs +
         1000U;
}

void printStatus(const SCD41::Status& status) {
  std::printf("status=%s detail=%ld msg=%s\n", errName(status.code),
              static_cast<long>(status.detail), status.msg);
}

void printSample(const SCD41::FixedSample& sample) {
  std::printf("sample seq=%lu epoch=%lu mode=%s co2=%u temp_mC=%ld rh_mPct=%lu flags=0x%04X at=%lu\n",
              static_cast<unsigned long>(sample.sequence),
              static_cast<unsigned long>(sample.sensorEpoch), modeName(sample.mode),
              static_cast<unsigned>(sample.co2Ppm),
              static_cast<long>(sample.temperatureMilliC),
              static_cast<unsigned long>(sample.humidityMilliPercent),
              static_cast<unsigned>(sample.flags),
              static_cast<unsigned long>(sample.capturedAtMs));
}

void printConfiguration(const SCD41::ConfigurationSnapshot& value) {
  std::printf("config offset_mC=%ld altitude_m=%u pressure_Pa=%lu asc=%s target_ppm=%u initial_h=%u standard_h=%u verified=0x%04X dirty=0x%04X persistence_indeterminate=%s\n",
              static_cast<long>(value.temperatureOffsetMilliC),
              static_cast<unsigned>(value.sensorAltitudeM),
              static_cast<unsigned long>(value.ambientPressurePa),
              value.ascEnabled ? "on" : "off",
              static_cast<unsigned>(value.ascTargetPpm),
              static_cast<unsigned>(value.ascInitialPeriodHours),
              static_cast<unsigned>(value.ascStandardPeriodHours),
              static_cast<unsigned>(value.verifiedMask),
              static_cast<unsigned>(value.dirtyMask),
              value.persistenceIndeterminate ? "yes" : "no");
}

void printResult(const SCD41::OperationResult& result) {
  std::printf("result request=%lu generation=%lu op=%s outcome=%s effect=%s status=%s callbacks=%u reconcile=%s\n",
              static_cast<unsigned long>(result.id.requestId),
              static_cast<unsigned long>(result.id.generation),
              operationName(result.kind), outcomeName(result.outcome),
              effectName(result.effect), errName(result.status.code),
              static_cast<unsigned>(result.callbacksUsed),
              result.reconciliationRequired ? "yes" : "no");
  if (result.kind == SCD41::OperationKind::ATTACH ||
      result.kind == SCD41::OperationKind::READ_IDENTITY ||
      result.kind == SCD41::OperationKind::WAKE_UP ||
      result.kind == SCD41::OperationKind::REINIT ||
      result.kind == SCD41::OperationKind::FACTORY_RESET) {
    std::printf("identity valid=%s serial=0x%012llX variant=%s epoch=%lu\n",
                result.value.identity.valid ? "yes" : "no",
                static_cast<unsigned long long>(result.value.identity.serialNumber),
                variantName(result.value.identity.variant),
                static_cast<unsigned long>(result.value.identity.sensorEpoch));
  } else if (result.kind == SCD41::OperationKind::FETCH_SAMPLE ||
             result.kind == SCD41::OperationKind::SINGLE_SHOT ||
             result.kind == SCD41::OperationKind::SINGLE_SHOT_RHT_ONLY) {
    if ((result.value.sample.flags & SCD41::SAMPLE_FRESH) != 0U) printSample(result.value.sample);
  } else if (result.kind == SCD41::OperationKind::READ_TEMPERATURE_OFFSET ||
             result.kind == SCD41::OperationKind::SET_TEMPERATURE_OFFSET) {
    std::printf("value signed=%ld\n", static_cast<long>(result.value.signedValue));
  } else if (result.kind == SCD41::OperationKind::READ_ASC_ENABLED ||
             result.kind == SCD41::OperationKind::SET_ASC_ENABLED) {
    std::printf("value bool=%s\n", result.value.boolValue ? "true" : "false");
  } else if (result.kind == SCD41::OperationKind::READ_SENSOR_ALTITUDE ||
             result.kind == SCD41::OperationKind::SET_SENSOR_ALTITUDE ||
             result.kind == SCD41::OperationKind::READ_AMBIENT_PRESSURE ||
             result.kind == SCD41::OperationKind::SET_AMBIENT_PRESSURE ||
             result.kind == SCD41::OperationKind::READ_ASC_TARGET ||
             result.kind == SCD41::OperationKind::SET_ASC_TARGET ||
             result.kind == SCD41::OperationKind::READ_ASC_INITIAL_PERIOD ||
             result.kind == SCD41::OperationKind::SET_ASC_INITIAL_PERIOD ||
             result.kind == SCD41::OperationKind::READ_ASC_STANDARD_PERIOD ||
             result.kind == SCD41::OperationKind::SET_ASC_STANDARD_PERIOD) {
    std::printf("value unsigned=%lu\n", static_cast<unsigned long>(result.value.value));
  } else if (result.kind == SCD41::OperationKind::SELF_TEST) {
    std::printf("selftest raw=0x%04X\n", static_cast<unsigned>(result.value.rawWords[0]));
  } else if (result.kind == SCD41::OperationKind::FORCED_RECALIBRATION) {
    std::printf("frc correction_ppm=%ld raw=0x%04X\n",
                static_cast<long>(result.value.signedValue),
                static_cast<unsigned>(result.value.rawWords[0]));
  } else if (result.kind == SCD41::OperationKind::READ_CONFIGURATION) {
    printConfiguration(result.value.configuration);
  } else if (result.kind == SCD41::OperationKind::READ_DATA_READY) {
    std::printf("data_ready=%s raw=0x%04X\n",
                result.value.dataReady.ready ? "yes" : "no",
                static_cast<unsigned>(result.value.dataReady.raw));
  } else if (result.kind == SCD41::OperationKind::DIAGNOSTIC_READ_WORDS) {
    for (uint8_t i = 0; i < result.value.wordCount; ++i) {
      std::printf("word[%u]=0x%04X\n", static_cast<unsigned>(i),
                  static_cast<unsigned>(result.value.rawWords[i]));
    }
  }
}

void takeAndPrint(const SCD41::OperationId& id) {
  SCD41::OperationResult result;
  const SCD41::Status status = device.takeResult(id, result);
  if (!status.ok()) { printStatus(status); return; }
  lastResult = result;
  lastResultValid = true;
  printResult(result);
}

void pollDriver() {
  const SCD41::PollResult poll = device.poll(idfNowMs(), 1U);
  if (poll.state == SCD41::OperationState::RESULT_PENDING) takeAndPrint(poll.id);
  else if (!poll.status.ok() && !poll.status.inProgress()) printStatus(poll.status);
}

bool bindDriver() {
  config = SCD41::Config{};
  config.transfer = idfI2cTransfer;
  config.transferUser = &i2cContext;
  config.transferTimeoutMs = 50U;
  const SCD41::Status status = device.begin(config);
  printStatus(status);
  return status.ok();
}

void startOperation(const SCD41::OperationRequest& request) {
  const uint32_t now = idfNowMs();
  SCD41::OperationOptions options;
  options.requestId = nextRequestId++;
  if (nextRequestId == 0U) nextRequestId = 1U;
  options.nowMs = now;
  options.deadlineMs = now + operationBudgetMs(request.kind);
  SCD41::OperationId id;
  const SCD41::Status status = device.start(request, options, id);
  printStatus(status);
  if (status.inProgress()) {
    std::printf("started request=%lu generation=%lu op=%s deadline=%lu\n",
                static_cast<unsigned long>(id.requestId),
                static_cast<unsigned long>(id.generation), operationName(request.kind),
                static_cast<unsigned long>(options.deadlineMs));
  }
}

void printRuntime() {
  const SCD41::RuntimeSnapshot runtime = device.runtimeSnapshot();
  const SCD41::HealthSnapshot health = device.healthSnapshot();
  std::printf("runtime bound=%s attached=%s state=%s mode=%s operation=%s next_due=%lu reconcile=%s sample=%s\n",
              runtime.bound ? "yes" : "no", runtime.attached ? "yes" : "no",
              stateName(runtime.driverState), modeName(runtime.operatingMode),
              operationName(runtime.operationKind),
              static_cast<unsigned long>(runtime.nextDueMs),
              runtime.reconciliationRequired ? "yes" : "no",
              runtime.sampleAvailable ? "yes" : "no");
  std::printf("health transfer_ok=%lu transfer_fail=%lu expected_nack=%lu protocol_fail=%lu operation_ok=%lu operation_fail=%lu cancelled=%lu\n",
              static_cast<unsigned long>(health.totalTransferSuccess),
              static_cast<unsigned long>(health.totalTransferFailures),
              static_cast<unsigned long>(health.expectedNacks),
              static_cast<unsigned long>(health.totalProtocolFailures),
              static_cast<unsigned long>(health.totalOperationSuccess),
              static_cast<unsigned long>(health.totalOperationFailures),
              static_cast<unsigned long>(health.totalOperationCancelled));
}

void printHelp() {
  printHelpHeader("SCD41 Owner-Safe CLI v1");
  printHelpSection("Lifecycle");
  printHelpItem("help/?", "Show this help");
  printHelpItem("version", "Show library version");
  printHelpItem("scan", "Scan the example-owned I2C bus while idle");
  printHelpItem("begin", "Zero-I2C bind, then start attach");
  printHelpItem("end", "Cancel active work and unbind without I2C");
  printHelpItem("status", "Show cache-only runtime and health");
  printHelpItem("result", "Show the example's last consumed result");
  printHelpItem("cancel", "Cancel the active operation without I2C");
  printHelpSection("Measurement");
  printHelpItem("identity", "Read serial number and variant");
  printHelpItem("periodic on|lp|off", "Change periodic mode");
  printHelpItem("dataready", "Read data-ready status");
  printHelpItem("read", "Fetch one periodic sample");
  printHelpItem("single full|rht", "Run one bounded single-shot job");
  printHelpItem("sample", "Show the latest cached fixed-point sample");
  printHelpItem("settings", "Read all live configuration fields");
  printHelpItem("sleep", "Enter sensor power-down mode");
  printHelpItem("wake", "Wake and verify the sensor");
  printHelpSection("Configuration");
  printHelpItem("toffset [mC]", "Read or set temperature offset");
  printHelpItem("altitude [m]", "Read or set sensor altitude");
  printHelpItem("pressure [Pa]", "Read or set ambient pressure");
  printHelpItem("asc_enabled [0|1]", "Read or set ASC enable");
  printHelpItem("asc_target [ppm]", "Read or set ASC target");
  printHelpItem("asc_initial [h]", "Read or set ASC initial period");
  printHelpItem("asc_standard [h]", "Read or set ASC standard period");
  printHelpSection("Maintenance");
  printHelpItem("reinit", "Reload persisted sensor settings");
  printHelpItem("selftest", "Run the 10-second sensor self-test");
  printHelpItem("frc confirm <reference_ppm>", "Run forced recalibration");
  printHelpItem("persist confirm", "Write current settings to EEPROM");
  printHelpItem("factory_reset confirm", "Reset persisted sensor state");
  printHelpSection("Diagnostics");
  printHelpItem("command read_words <cmd> <count>", "CRC-check 1..3 words; then reattach");
}

void scanBus() {
  if (bus == nullptr) { LOGW("I2C bus not initialized"); return; }
  int count = 0;
  for (uint8_t address = 1U; address < 127U; ++address) {
    if (i2c_master_probe(bus, address, 20) == ESP_OK) {
      std::printf("  Found device at 0x%02X%s\n", static_cast<unsigned>(address),
                  address == SCD41_ADDRESS ? "  <target>" : "");
      ++count;
    }
  }
  LOGI("Found %d device(s)", count);
}

void processCommand(const Line& line) {
  Line head;
  Line tail;
  if (!splitHeadTail(line, head, tail)) return;
  if (head == "help" || head == "?") printHelp();
  else if (head == "version") std::printf("SCD41 version=%s\n", SCD41::VERSION);
  else if (head == "scan") {
    if (device.operationState() != SCD41::OperationState::IDLE) printStatus(SCD41::Status::Error(SCD41::Err::BUSY, "Operation active"));
    else scanBus();
  } else if (head == "begin") {
    const SCD41::RuntimeSnapshot before = device.runtimeSnapshot();
    device.end();
    if (before.operationState != SCD41::OperationState::IDLE) takeAndPrint(before.operationId);
    if (bindDriver()) startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::ATTACH));
  } else if (head == "end") {
    const SCD41::RuntimeSnapshot before = device.runtimeSnapshot();
    device.end();
    if (before.operationState != SCD41::OperationState::IDLE) takeAndPrint(before.operationId);
  } else if (head == "status") printRuntime();
  else if (head == "result") { if (lastResultValid) printResult(lastResult); else std::printf("result=none\n"); }
  else if (head == "cancel") {
    const SCD41::RuntimeSnapshot runtime = device.runtimeSnapshot();
    const SCD41::Status status = device.cancel(runtime.operationId, idfNowMs());
    printStatus(status); if (status.ok()) takeAndPrint(runtime.operationId);
  } else if (head == "identity") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::READ_IDENTITY));
  else if (head == "periodic") {
    if (tail == "on") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::START_PERIODIC));
    else if (tail == "lp") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::START_LOW_POWER_PERIODIC));
    else if (tail == "off") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::STOP_PERIODIC));
    else LOGW("Usage: periodic on|lp|off");
  } else if (head == "dataready") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::READ_DATA_READY));
  else if (head == "read") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::FETCH_SAMPLE));
  else if (head == "single") {
    if (tail == "full") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::SINGLE_SHOT));
    else if (tail == "rht") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::SINGLE_SHOT_RHT_ONLY));
    else LOGW("Usage: single full|rht");
  } else if (head == "sample") {
    SCD41::FixedSample sample;
    const SCD41::Status status = device.peekLatestSample(sample);
    if (status.ok()) printSample(sample); else printStatus(status);
  } else if (head == "settings") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::READ_CONFIGURATION));
  else if (head == "sleep") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::POWER_DOWN));
  else if (head == "wake") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::WAKE_UP));
  else if (head == "toffset") {
    int32_t value = 0;
    if (tail.empty()) startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::READ_TEMPERATURE_OFFSET));
    else if (parseI32(tail, value)) startOperation(SCD41::OperationRequest::setTemperatureOffsetMilliC(value));
    else LOGW("Usage: toffset [mC]");
  } else if (head == "altitude" || head == "pressure" || head == "asc_target" || head == "asc_initial" || head == "asc_standard") {
    uint32_t value = 0;
    const bool supplied = !tail.empty();
    if (supplied && !parseU32(tail, value)) { LOGW("Expected an unsigned integer"); return; }
    if (supplied && head != "pressure" && value > 65535U) {
      LOGW("Value must fit in 16 bits");
      return;
    }
    if (head == "altitude") startOperation(supplied ? SCD41::OperationRequest::setSensorAltitudeM(static_cast<uint16_t>(value)) : SCD41::OperationRequest::make(SCD41::OperationKind::READ_SENSOR_ALTITUDE));
    else if (head == "pressure") startOperation(supplied ? SCD41::OperationRequest::setAmbientPressurePa(value) : SCD41::OperationRequest::make(SCD41::OperationKind::READ_AMBIENT_PRESSURE));
    else if (head == "asc_target") startOperation(supplied ? SCD41::OperationRequest::setAscTargetPpm(static_cast<uint16_t>(value)) : SCD41::OperationRequest::make(SCD41::OperationKind::READ_ASC_TARGET));
    else if (head == "asc_initial") startOperation(supplied ? SCD41::OperationRequest::setAscInitialPeriodHours(static_cast<uint16_t>(value)) : SCD41::OperationRequest::make(SCD41::OperationKind::READ_ASC_INITIAL_PERIOD));
    else startOperation(supplied ? SCD41::OperationRequest::setAscStandardPeriodHours(static_cast<uint16_t>(value)) : SCD41::OperationRequest::make(SCD41::OperationKind::READ_ASC_STANDARD_PERIOD));
  } else if (head == "asc_enabled") {
    bool enabled = false;
    if (tail.empty()) startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::READ_ASC_ENABLED));
    else if (parseBool(tail, enabled)) startOperation(SCD41::OperationRequest::setAscEnabled(enabled));
    else LOGW("Usage: asc_enabled [0|1]");
  } else if (head == "reinit") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::REINIT));
  else if (head == "selftest") startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::SELF_TEST));
  else if (head == "frc") {
    Line confirm; Line reference; uint16_t ppm = 0;
    if (!splitHeadTail(tail, confirm, reference) || confirm != "confirm" || !parseU16(reference, ppm)) LOGW("use 'frc confirm <reference_ppm>' to update calibration history");
    else startOperation(SCD41::OperationRequest::forcedRecalibration(ppm));
  } else if (head == "persist") {
    if (tail != "confirm") LOGW("use 'persist confirm' to write EEPROM");
    else startOperation(SCD41::OperationRequest::persistSettings());
  } else if (head == "factory_reset") {
    if (tail != "confirm") LOGW("use 'factory_reset confirm' to erase/reset settings");
    else startOperation(SCD41::OperationRequest::factoryReset());
  } else if (head == "command") {
    Line sub; Line args; Line commandText; Line countText; uint16_t command = 0; uint32_t count = 0;
    if (!splitHeadTail(tail, sub, args) || sub != "read_words" ||
        !splitHeadTail(args, commandText, countText) || !parseU16(commandText, command) ||
        !parseU32(countText, count) || count == 0U || count > 3U) LOGW("Usage: command read_words <cmd> <count>");
    else startOperation(SCD41::OperationRequest::diagnosticReadWords(command, static_cast<uint8_t>(count)));
  } else LOGW("Unknown command: %s", head.c_str());
}

esp_err_t createBus() {
  i2c_master_bus_config_t busConfig{};
  busConfig.i2c_port = I2C_NUM_0;
  busConfig.sda_io_num = I2C_SDA;
  busConfig.scl_io_num = I2C_SCL;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&busConfig, &bus);
}

esp_err_t addSensor() {
  i2c_device_config_t deviceConfig{};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = SCD41_ADDRESS;
  deviceConfig.scl_speed_hz = I2C_FREQUENCY_HZ;
  return i2c_master_bus_add_device(bus, &deviceConfig, &sensorHandle);
}

}  // namespace

extern "C" void app_main(void) {
  ESP_ERROR_CHECK(createBus());
  ESP_ERROR_CHECK(addSensor());
  i2cContext.device = sensorHandle;
  i2cContext.address = SCD41_ADDRESS;
  LOGI("SCD41 owner-safe bring-up CLI");
  if (bindDriver()) startOperation(SCD41::OperationRequest::make(SCD41::OperationKind::ATTACH));
  printHelp();
  printPrompt();
  for (;;) {
    pollDriver();
    Line line;
    if (readLine(line)) { processCommand(line); printPrompt(); }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
