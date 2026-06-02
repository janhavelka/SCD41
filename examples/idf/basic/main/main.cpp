/// @file main.cpp
/// @brief ESP-IDF native bringup CLI example for SCD41.
/// @note This is an EXAMPLE, not part of the library.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <driver/gpio.h>
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

const char* errToString(Err err) {
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

const char* stateToString(DriverState state) {
  switch (state) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::READY: return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

const char* modeToString(OperatingMode mode) {
  switch (mode) {
    case OperatingMode::IDLE: return "IDLE";
    case OperatingMode::PERIODIC: return "PERIODIC";
    case OperatingMode::LOW_POWER_PERIODIC: return "LOW_POWER_PERIODIC";
    case OperatingMode::POWER_DOWN: return "POWER_DOWN";
    default: return "UNKNOWN";
  }
}

const char* pendingToString(PendingCommand command) {
  switch (command) {
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

const char* variantToString(SensorVariant variant) {
  switch (variant) {
    case SensorVariant::SCD40: return "SCD40";
    case SensorVariant::SCD41: return "SCD41";
    case SensorVariant::SCD42: return "SCD42";
    case SensorVariant::SCD43: return "SCD43";
    case SensorVariant::UNKNOWN: return "UNKNOWN";
    default: return "UNKNOWN";
  }
}

}  // namespace app_driver

namespace {

constexpr char LOG_COLOR_RESET[] = "\033[0m";
constexpr char LOG_COLOR_RED[] = "\033[31m";
constexpr char LOG_COLOR_GREEN[] = "\033[32m";
constexpr char LOG_COLOR_YELLOW[] = "\033[33m";
constexpr char LOG_COLOR_BLUE[] = "\033[34m";
constexpr char LOG_COLOR_CYAN[] = "\033[36m";
constexpr char LOG_COLOR_GRAY[] = "\033[90m";
constexpr size_t HELP_COMMAND_WIDTH = 32U;
constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
constexpr gpio_num_t I2C_SDA = static_cast<gpio_num_t>(SCD41_IDF_I2C_SDA);
constexpr gpio_num_t I2C_SCL = static_cast<gpio_num_t>(SCD41_IDF_I2C_SCL);
constexpr uint32_t I2C_FREQ_HZ = SCD41_IDF_I2C_FREQ_HZ;
constexpr uint8_t SCD41_ADDR = SCD41::cmd::I2C_ADDRESS;
constexpr uint32_t STRESS_PROGRESS_UPDATES = 10U;
constexpr uint32_t DEFAULT_STRESS_COUNT = 100U;
constexpr uint32_t MAX_STRESS_COUNT = 100000U;
constexpr size_t CLI_LINE_CAPACITY = 128U;

#define LOG_COLOR_RESULT(ok) ((ok) ? LOG_COLOR_GREEN : LOG_COLOR_RED)
#define LOG_PRINT_WITH_TAG(tagColor, tag, fmt, ...) \
  do { \
    std::printf(tagColor "[" tag "]" LOG_COLOR_RESET " " fmt "\n", ##__VA_ARGS__); \
    std::fflush(stdout); \
  } while (0)
#define LOGE(fmt, ...) LOG_PRINT_WITH_TAG(LOG_COLOR_RED, "E", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_PRINT_WITH_TAG(LOG_COLOR_YELLOW, "W", fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_PRINT_WITH_TAG(LOG_COLOR_CYAN, "I", fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG_PRINT_WITH_TAG(LOG_COLOR_BLUE, "D", fmt, ##__VA_ARGS__)

app_driver::Device device;
app_driver::Config gConfig;
IdfI2cContext gI2cCtx;
i2c_master_bus_handle_t gBus = nullptr;
i2c_master_dev_handle_t gDev = nullptr;
bool gVerbose = false;
bool gPendingRead = false;
bool gWatchEnabled = false;
uint32_t gPendingStartMs = 0;
int gStressRemaining = 0;
app_driver::PendingCommand gLastPending = app_driver::PendingCommand::NONE;

void printStatus(const app_driver::Status& st);

struct StressStats {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  uint32_t successBefore = 0;
  uint32_t failBefore = 0;
  int target = 0;
  int attempts = 0;
  int success = 0;
  uint32_t errors = 0;
  uint32_t invalidCo2Samples = 0;
  bool hasFailure = false;
  bool hasValidCo2 = false;
  bool hasSample = false;
  uint16_t minCo2 = 0;
  uint16_t maxCo2 = 0;
  float minTemp = 0.0f;
  float maxTemp = 0.0f;
  float minRh = 0.0f;
  float maxRh = 0.0f;
  double sumCo2 = 0.0;
  double sumTemp = 0.0;
  double sumRh = 0.0;
  app_driver::Status firstError = app_driver::Status::Ok();
  app_driver::Status lastError = app_driver::Status::Ok();
};

struct SettingsCache {
  bool temperatureOffsetKnown = false;
  bool altitudeKnown = false;
  bool ambientPressureKnown = false;
  bool ascEnabledKnown = false;
  bool ascTargetKnown = false;
  bool ascInitialKnown = false;
  bool ascStandardKnown = false;
  int32_t temperatureOffsetC_x1000 = 0;
  uint16_t sensorAltitudeM = 0;
  uint32_t ambientPressurePa = 0;
  bool automaticSelfCalibrationEnabled = false;
  uint16_t automaticSelfCalibrationTargetPpm = 0;
  uint16_t automaticSelfCalibrationInitialPeriodHours = 0;
  uint16_t automaticSelfCalibrationStandardPeriodHours = 0;
};

struct HealthSnapshot {
  app_driver::DriverState state = app_driver::DriverState::UNINIT;
  bool online = false;
  uint8_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t totalSuccess = 0;
  uint32_t lastOkMs = 0;
  uint32_t lastErrorMs = 0;
  app_driver::Status lastError = app_driver::Status::Ok();

  void capture(const app_driver::Device& driver) {
    state = driver.state();
    online = driver.isOnline();
    consecutiveFailures = driver.consecutiveFailures();
    totalFailures = driver.totalFailures();
    totalSuccess = driver.totalSuccess();
    lastOkMs = driver.lastOkMs();
    lastErrorMs = driver.lastErrorMs();
    lastError = driver.lastError();
  }
};

StressStats gStressStats;
SettingsCache gSettingsCache;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

uint32_t nowUs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time());
}

void cooperativeYield(void*) {
  taskYIELD();
}

uint32_t idfNowMs() {
  return nowMs(nullptr);
}

const char* yesNo(bool value) {
  return value ? "yes" : "no";
}

const char* boolText(bool value) {
  return value ? "true" : "false";
}

const char* onOff(bool value) {
  return value ? "ON" : "OFF";
}

const char* boolColor(bool value) {
  return value ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

const char* stateColor(app_driver::DriverState state) {
  switch (state) {
    case app_driver::DriverState::READY: return LOG_COLOR_GREEN;
    case app_driver::DriverState::DEGRADED: return LOG_COLOR_YELLOW;
    case app_driver::DriverState::OFFLINE: return LOG_COLOR_RED;
    case app_driver::DriverState::UNINIT: return LOG_COLOR_GRAY;
    default: return LOG_COLOR_RESET;
  }
}

const char* statusColor(const app_driver::Status& st) {
  if (st.ok()) {
    return LOG_COLOR_GREEN;
  }
  if (st.inProgress()) {
    return LOG_COLOR_YELLOW;
  }
  return LOG_COLOR_RED;
}

const char* goodIfZeroColor(uint32_t value) {
  return (value == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

const char* goodIfNonZeroColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

const char* skipCountColor(uint32_t count) {
  return (count > 0U) ? LOG_COLOR_YELLOW : LOG_COLOR_GRAY;
}

const char* toggleColor(bool enabled) {
  return enabled ? LOG_COLOR_GREEN : LOG_COLOR_GRAY;
}

const char* failureColor(uint32_t failures) {
  if (failures == 0U) {
    return LOG_COLOR_GREEN;
  }
  if (failures < 3U) {
    return LOG_COLOR_YELLOW;
  }
  return LOG_COLOR_RED;
}

const char* successColor(uint32_t successes) {
  return (successes > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_GRAY;
}

const char* singleShotModeToString(app_driver::SingleShotMode mode) {
  switch (mode) {
    case app_driver::SingleShotMode::CO2_T_RH: return "full";
    case app_driver::SingleShotMode::T_RH_ONLY: return "rht";
    default: return "unknown";
  }
}

void flushOut() {
  std::fflush(stdout);
}

void printPrompt() {
  std::printf("> ");
  flushOut();
}

void printHelpHeader(const char* title) {
  std::printf("%s=== %s ===%s\n", LOG_COLOR_CYAN, title, LOG_COLOR_RESET);
}

void printHelpSection(const char* title) {
  std::printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
}

void printHelpItem(const char* command, const char* description) {
  std::printf("  %s%-*s%s - %s\n",
              LOG_COLOR_CYAN,
              static_cast<int>(HELP_COMMAND_WIDTH),
              command,
              LOG_COLOR_RESET,
              description);
}

void printToggleState(const char* label, bool enabled) {
  std::printf("  %s: %s%s%s\n", label, toggleColor(enabled), onOff(enabled), LOG_COLOR_RESET);
}

struct Line {
  static constexpr size_t npos = static_cast<size_t>(-1);

  char text[CLI_LINE_CAPACITY] = {};
  size_t len = 0;

  void clear() {
    len = 0;
    text[0] = '\0';
  }

  bool assign(const char* src, size_t srcLen) {
    if (src == nullptr) {
      clear();
      return false;
    }
    const bool fits = srcLen < CLI_LINE_CAPACITY;
    const size_t copyLen = fits ? srcLen : (CLI_LINE_CAPACITY - 1U);
    if (copyLen > 0U) {
      std::memcpy(text, src, copyLen);
    }
    len = copyLen;
    text[len] = '\0';
    return fits;
  }

  bool assign(const char* src) {
    return assign(src, (src == nullptr) ? 0U : std::strlen(src));
  }

  bool empty() const {
    return len == 0U;
  }

  size_t size() const {
    return len;
  }

  const char* c_str() const {
    return text;
  }

  char operator[](size_t index) const {
    return (index < len) ? text[index] : '\0';
  }

  size_t find(char needle) const {
    for (size_t i = 0; i < len; ++i) {
      if (text[i] == needle) {
        return i;
      }
    }
    return npos;
  }
};

bool operator==(const Line& lhs, const char* rhs) {
  return std::strcmp(lhs.c_str(), (rhs == nullptr) ? "" : rhs) == 0;
}

bool operator!=(const Line& lhs, const char* rhs) {
  return !(lhs == rhs);
}

void trimCopy(const Line& input, Line& output) {
  size_t first = 0;
  while (first < input.size() && (input[first] == ' ' || input[first] == '\t')) {
    ++first;
  }
  size_t last = input.size();
  while (last > first && (input[last - 1U] == ' ' || input[last - 1U] == '\t')) {
    --last;
  }
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
    Line rawHead;
    Line rawTail;
    (void)rawHead.assign(trimmed.c_str(), split);
    (void)rawTail.assign(trimmed.c_str() + split + 1U, trimmed.size() - split - 1U);
    trimCopy(rawHead, head);
    trimCopy(rawTail, tail);
  }
  return !head.empty();
}

bool hasLeadingMinus(const Line& token) {
  return !token.empty() && token[0] == '-';
}

bool parseU16(const Line& token, uint16_t& outValue) {
  if (token.empty() || hasLeadingMinus(token)) {
    return false;
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0' || value > 0xFFFFUL) {
    return false;
  }
  outValue = static_cast<uint16_t>(value);
  return true;
}

bool parseU32(const Line& token, uint32_t& outValue) {
  if (token.empty() || hasLeadingMinus(token)) {
    return false;
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0') {
    return false;
  }
  outValue = static_cast<uint32_t>(value);
  return true;
}

bool parseFloat(const Line& token, float& outValue) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  outValue = static_cast<float>(std::strtod(token.c_str(), &end));
  return !(end == token.c_str() || *end != '\0');
}

bool parseBool01(const Line& token, bool& outValue) {
  if (token == "1" || token == "on" || token == "true" || token == "enable") {
    outValue = true;
    return true;
  }
  if (token == "0" || token == "off" || token == "false" || token == "disable") {
    outValue = false;
    return true;
  }
  return false;
}

bool readLine(Line& outLine) {
  static char buffer[CLI_LINE_CAPACITY] = {};
  static size_t length = 0;
  static bool overflowed = false;

  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(STDIN_FILENO, &readSet);
  timeval timeout = {};
  const int ready = select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &timeout);
  if (ready <= 0 || !FD_ISSET(STDIN_FILENO, &readSet)) {
    return false;
  }

  char c = '\0';
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == '\b' || c == 0x7F) {
      if (!overflowed && length > 0U) {
        --length;
      }
      continue;
    }

    if (c == '\r' || c == '\n') {
      if (overflowed) {
        length = 0;
        overflowed = false;
        outLine.clear();
        LOGW("Command too long (max %u characters); discarded",
             static_cast<unsigned>(CLI_LINE_CAPACITY - 1U));
        return false;
      }
      buffer[length] = '\0';
      Line raw;
      (void)raw.assign(buffer, length);
      trimCopy(raw, outLine);
      length = 0;
      return !outLine.empty();
    }

    if (overflowed) {
      continue;
    }

    if (length < (sizeof(buffer) - 1U)) {
      buffer[length++] = c;
    } else {
      length = 0;
      overflowed = true;
    }

    timeval moreTimeout = {};
    FD_ZERO(&readSet);
    FD_SET(STDIN_FILENO, &readSet);
    const int more = select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &moreTimeout);
    if (more <= 0 || !FD_ISSET(STDIN_FILENO, &readSet)) {
      break;
    }
  }
  return false;
}

void clearSettingsCache() {
  gSettingsCache = SettingsCache{};
}

void noteSettingsCache(const app_driver::SettingsSnapshot& snap) {
  if (snap.liveConfigValid) {
    gSettingsCache.temperatureOffsetKnown = true;
    gSettingsCache.altitudeKnown = true;
    gSettingsCache.ascEnabledKnown = true;
    gSettingsCache.ascTargetKnown = true;
    gSettingsCache.ascInitialKnown = true;
    gSettingsCache.ascStandardKnown = true;
  }
  if (snap.liveConfigValid || snap.ambientPressurePa != 0U) {
    gSettingsCache.ambientPressureKnown = true;
  }
  if (gSettingsCache.temperatureOffsetKnown) {
    gSettingsCache.temperatureOffsetC_x1000 = snap.temperatureOffsetC_x1000;
  }
  if (gSettingsCache.altitudeKnown) {
    gSettingsCache.sensorAltitudeM = snap.sensorAltitudeM;
  }
  if (gSettingsCache.ambientPressureKnown) {
    gSettingsCache.ambientPressurePa = snap.ambientPressurePa;
  }
  if (gSettingsCache.ascEnabledKnown) {
    gSettingsCache.automaticSelfCalibrationEnabled = snap.automaticSelfCalibrationEnabled;
  }
  if (gSettingsCache.ascTargetKnown) {
    gSettingsCache.automaticSelfCalibrationTargetPpm =
        snap.automaticSelfCalibrationTargetPpm;
  }
  if (gSettingsCache.ascInitialKnown) {
    gSettingsCache.automaticSelfCalibrationInitialPeriodHours =
        snap.automaticSelfCalibrationInitialPeriodHours;
  }
  if (gSettingsCache.ascStandardKnown) {
    gSettingsCache.automaticSelfCalibrationStandardPeriodHours =
        snap.automaticSelfCalibrationStandardPeriodHours;
  }
}

bool hasCachedSettings() {
  return gSettingsCache.temperatureOffsetKnown || gSettingsCache.altitudeKnown ||
         gSettingsCache.ambientPressureKnown || gSettingsCache.ascEnabledKnown ||
         gSettingsCache.ascTargetKnown || gSettingsCache.ascInitialKnown ||
         gSettingsCache.ascStandardKnown;
}

bool healthMatches(const HealthSnapshot& before, const HealthSnapshot& after) {
  return before.state == after.state && before.online == after.online &&
         before.consecutiveFailures == after.consecutiveFailures &&
         before.totalFailures == after.totalFailures &&
         before.totalSuccess == after.totalSuccess && before.lastOkMs == after.lastOkMs &&
         before.lastErrorMs == after.lastErrorMs &&
         before.lastError.code == after.lastError.code &&
         before.lastError.detail == after.lastError.detail;
}

void printUnknownField(const char* label) {
  std::printf("  %-20s %sunknown%s\n", label, LOG_COLOR_GRAY, LOG_COLOR_RESET);
}

void printKnownBoolField(const char* label, bool known, bool value) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  std::printf("  %-20s %s%s%s\n", label, boolColor(value), yesNo(value), LOG_COLOR_RESET);
}

void printKnownFloatField(const char* label, bool known, float value, const char* unit) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  std::printf("  %-20s %.3f %s\n", label, static_cast<double>(value), unit);
}

void printKnownU16Field(const char* label, bool known, uint16_t value, const char* unit) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  std::printf("  %-20s %u %s\n", label, static_cast<unsigned>(value), unit);
}

void printKnownU32Field(const char* label, bool known, uint32_t value, const char* unit) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  std::printf("  %-20s %lu %s\n", label, static_cast<unsigned long>(value), unit);
}

uint32_t stressProgressStep(uint32_t total) {
  if (total == 0U) {
    return 0U;
  }
  const uint32_t step = total / STRESS_PROGRESS_UPDATES;
  return (step == 0U) ? 1U : step;
}

void printStressProgress(uint32_t completed, uint32_t total, uint32_t okCount, uint32_t failCount) {
  if (completed == 0U || total == 0U) {
    return;
  }
  const uint32_t step = stressProgressStep(total);
  if (step == 0U || (completed != total && (completed % step) != 0U)) {
    return;
  }
  const float pct = (100.0f * static_cast<float>(completed)) / static_cast<float>(total);
  std::printf("  Progress: %lu/%lu (%s%.0f%%%s, ok=%s%lu%s, fail=%s%lu%s)\n",
              static_cast<unsigned long>(completed),
              static_cast<unsigned long>(total),
              successRateColor(pct),
              static_cast<double>(pct),
              LOG_COLOR_RESET,
              goodIfNonZeroColor(okCount),
              static_cast<unsigned long>(okCount),
              LOG_COLOR_RESET,
              goodIfZeroColor(failCount),
              static_cast<unsigned long>(failCount),
              LOG_COLOR_RESET);
}

void resetStressStats(int target) {
  gStressStats = StressStats{};
  gStressStats.active = true;
  gStressStats.startMs = idfNowMs();
  gStressStats.successBefore = device.totalSuccess();
  gStressStats.failBefore = device.totalFailures();
  gStressStats.target = target;
}

void noteStressError(const app_driver::Status& st) {
  ++gStressStats.errors;
  if (!gStressStats.hasFailure) {
    gStressStats.firstError = st;
    gStressStats.hasFailure = true;
  }
  gStressStats.lastError = st;
}

void updateStressStats(const app_driver::Measurement& sample) {
  if (!gStressStats.hasSample) {
    gStressStats.minTemp = gStressStats.maxTemp = sample.temperatureC;
    gStressStats.minRh = gStressStats.maxRh = sample.humidityPct;
    gStressStats.hasSample = true;
  } else {
    if (sample.temperatureC < gStressStats.minTemp) gStressStats.minTemp = sample.temperatureC;
    if (sample.temperatureC > gStressStats.maxTemp) gStressStats.maxTemp = sample.temperatureC;
    if (sample.humidityPct < gStressStats.minRh) gStressStats.minRh = sample.humidityPct;
    if (sample.humidityPct > gStressStats.maxRh) gStressStats.maxRh = sample.humidityPct;
  }

  gStressStats.sumTemp += sample.temperatureC;
  gStressStats.sumRh += sample.humidityPct;
  if (sample.co2Valid) {
    if (!gStressStats.hasValidCo2) {
      gStressStats.minCo2 = gStressStats.maxCo2 = sample.co2Ppm;
      gStressStats.hasValidCo2 = true;
    } else {
      if (sample.co2Ppm < gStressStats.minCo2) gStressStats.minCo2 = sample.co2Ppm;
      if (sample.co2Ppm > gStressStats.maxCo2) gStressStats.maxCo2 = sample.co2Ppm;
    }
    gStressStats.sumCo2 += static_cast<double>(sample.co2Ppm);
  } else {
    ++gStressStats.invalidCo2Samples;
  }
  ++gStressStats.success;
}

void finishStressStats() {
  gStressStats.active = false;
  gStressStats.endMs = idfNowMs();
  const uint32_t successDelta = device.totalSuccess() - gStressStats.successBefore;
  const uint32_t failDelta = device.totalFailures() - gStressStats.failBefore;
  const uint32_t durationMs = gStressStats.endMs - gStressStats.startMs;
  const float successPct =
      (gStressStats.attempts > 0)
          ? (100.0f * static_cast<float>(gStressStats.success) /
             static_cast<float>(gStressStats.attempts))
          : 0.0f;

  std::printf("=== Stress Summary ===\n");
  std::printf("  Target:   %d\n", gStressStats.target);
  std::printf("  Attempts: %d\n", gStressStats.attempts);
  std::printf("  Success:  %s%d%s\n",
              goodIfNonZeroColor(static_cast<uint32_t>(gStressStats.success)),
              gStressStats.success,
              LOG_COLOR_RESET);
  std::printf("  Errors:   %s%lu%s\n",
              goodIfZeroColor(gStressStats.errors),
              static_cast<unsigned long>(gStressStats.errors),
              LOG_COLOR_RESET);
  std::printf("  Success rate: %s%.2f%%%s\n",
              successRateColor(successPct),
              static_cast<double>(successPct),
              LOG_COLOR_RESET);
  std::printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0U) {
    std::printf("  Rate:     %.2f samples/s\n",
                static_cast<double>(1000.0f * static_cast<float>(gStressStats.attempts) /
                                    static_cast<float>(durationMs)));
  }
  std::printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
              goodIfNonZeroColor(successDelta),
              static_cast<unsigned long>(successDelta),
              LOG_COLOR_RESET,
              goodIfZeroColor(failDelta),
              static_cast<unsigned long>(failDelta),
              LOG_COLOR_RESET);
  if (gStressStats.success > 0 && gStressStats.hasSample) {
    const float avgTemp =
        static_cast<float>(gStressStats.sumTemp / static_cast<double>(gStressStats.success));
    const float avgRh =
        static_cast<float>(gStressStats.sumRh / static_cast<double>(gStressStats.success));
    std::printf("  Temp C:   min=%.3f avg=%.3f max=%.3f\n",
                static_cast<double>(gStressStats.minTemp),
                static_cast<double>(avgTemp),
                static_cast<double>(gStressStats.maxTemp));
    std::printf("  RH %%:     min=%.3f avg=%.3f max=%.3f\n",
                static_cast<double>(gStressStats.minRh),
                static_cast<double>(avgRh),
                static_cast<double>(gStressStats.maxRh));
    if (gStressStats.hasValidCo2) {
      const uint32_t validCo2Count = static_cast<uint32_t>(gStressStats.success) -
                                     gStressStats.invalidCo2Samples;
      const float avgCo2 = static_cast<float>(gStressStats.sumCo2 /
                                              static_cast<double>(validCo2Count));
      std::printf("  CO2 ppm:  min=%u avg=%.1f max=%u invalid=%lu\n",
                  static_cast<unsigned>(gStressStats.minCo2),
                  static_cast<double>(avgCo2),
                  static_cast<unsigned>(gStressStats.maxCo2),
                  static_cast<unsigned long>(gStressStats.invalidCo2Samples));
    } else {
      std::printf("  CO2 ppm:  no valid CO2 samples (invalid=%lu)\n",
                  static_cast<unsigned long>(gStressStats.invalidCo2Samples));
    }
  }
  if (gStressStats.hasFailure) {
    std::printf("  First failure:\n");
    printStatus(gStressStats.firstError);
    if (gStressStats.errors > 1U) {
      std::printf("  Last failure:\n");
      printStatus(gStressStats.lastError);
    }
  }
}

void cancelQueuedWork() {
  gPendingRead = false;
  gPendingStartMs = 0;
  gWatchEnabled = false;
  gStressRemaining = 0;
  gStressStats.active = false;
}

void printStatus(const app_driver::Status& st) {
  std::printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
              statusColor(st),
              app_driver::errToString(st.code),
              LOG_COLOR_RESET,
              static_cast<unsigned>(st.code),
              static_cast<long>(st.detail));
  if (st.msg != nullptr && st.msg[0] != '\0') {
    std::printf("  Message: %s%s%s\n", LOG_COLOR_YELLOW, st.msg, LOG_COLOR_RESET);
  }
}

void printVersionInfo() {
  std::printf("=== Version ===\n");
  std::printf("  Example build: %s %s\n", __DATE__, __TIME__);
  std::printf("  Library:       SCD41 %s\n", SCD41::VERSION);
  std::printf("  Full:          %s\n", SCD41::VERSION_FULL);
  std::printf("  Built:         %s\n", SCD41::BUILD_TIMESTAMP);
  std::printf("  Commit:        %s\n", SCD41::GIT_COMMIT);
  std::printf("  Status:        %s\n", SCD41::GIT_STATUS);
}

void printMeasurement(const app_driver::Measurement& sample) {
  std::printf("Sample: CO2=%u ppm valid=%s%s%s T=%.3f C RH=%.3f %%\n",
              static_cast<unsigned>(sample.co2Ppm),
              boolColor(sample.co2Valid),
              yesNo(sample.co2Valid),
              LOG_COLOR_RESET,
              static_cast<double>(sample.temperatureC),
              static_cast<double>(sample.humidityPct));
}

void printRawSample(const app_driver::RawSample& sample) {
  std::printf("raw_co2=%u raw_temp=0x%04X raw_rh=0x%04X\n",
              static_cast<unsigned>(sample.rawCo2),
              static_cast<unsigned>(sample.rawTemperature),
              static_cast<unsigned>(sample.rawHumidity));
}

void printCompensatedSample(const app_driver::CompensatedSample& sample) {
  std::printf("Comp: co2=%u ppm valid=%s%s%s temp=%ld mC rh=%lu m%%\n",
              static_cast<unsigned>(sample.co2Ppm),
              boolColor(sample.co2Valid),
              yesNo(sample.co2Valid),
              LOG_COLOR_RESET,
              static_cast<long>(sample.tempC_x1000),
              static_cast<unsigned long>(sample.humidityPct_x1000));
}

void printDriverHealth() {
  const uint32_t now = idfNowMs();
  const uint32_t totalOk = device.totalSuccess();
  const uint32_t totalFail = device.totalFailures();
  const uint32_t total = totalOk + totalFail;
  const float successRate = (total > 0U)
                                ? (100.0f * static_cast<float>(totalOk) /
                                   static_cast<float>(total))
                                : 0.0f;
  const app_driver::Status lastErr = device.lastError();
  const uint32_t lastOkMs = device.lastOkMs();
  const uint32_t lastErrorMs = device.lastErrorMs();

  std::printf("=== Driver Health ===\n");
  std::printf("  State: %s%s%s\n",
              stateColor(device.state()),
              app_driver::stateToString(device.state()),
              LOG_COLOR_RESET);
  std::printf("  Online: %s%s%s\n",
              boolColor(device.isOnline()),
              yesNo(device.isOnline()),
              LOG_COLOR_RESET);
  std::printf("  Consecutive failures: %s%u%s\n",
              goodIfZeroColor(device.consecutiveFailures()),
              static_cast<unsigned>(device.consecutiveFailures()),
              LOG_COLOR_RESET);
  std::printf("  Total success: %s%lu%s\n",
              goodIfNonZeroColor(totalOk),
              static_cast<unsigned long>(totalOk),
              LOG_COLOR_RESET);
  std::printf("  Total failures: %s%lu%s\n",
              goodIfZeroColor(totalFail),
              static_cast<unsigned long>(totalFail),
              LOG_COLOR_RESET);
  std::printf("  Success rate: %s%.1f%%%s\n",
              successRateColor(successRate),
              static_cast<double>(successRate),
              LOG_COLOR_RESET);
  if (lastOkMs > 0U) {
    std::printf("  Last OK: %lu ms ago (at %lu ms)\n",
                static_cast<unsigned long>(now - lastOkMs),
                static_cast<unsigned long>(lastOkMs));
  } else {
    std::printf("  Last OK: never\n");
  }
  if (lastErrorMs > 0U) {
    std::printf("  Last error: %lu ms ago (at %lu ms)\n",
                static_cast<unsigned long>(now - lastErrorMs),
                static_cast<unsigned long>(lastErrorMs));
  } else {
    std::printf("  Last error: never\n");
  }
  if (!lastErr.ok()) {
    std::printf("  Error code: %s%s%s\n",
                LOG_COLOR_RED,
                app_driver::errToString(lastErr.code),
                LOG_COLOR_RESET);
    std::printf("  Error detail: %ld\n", static_cast<long>(lastErr.detail));
    if (lastErr.msg != nullptr && lastErr.msg[0] != '\0') {
      std::printf("  Error msg: %s\n", lastErr.msg);
    }
  }
}

void printHealthView(const app_driver::Device& driver) {
  HealthSnapshot snap;
  snap.capture(driver);
  const uint32_t total = snap.totalSuccess + snap.totalFailures;
  const float pct = (total > 0U)
                        ? (100.0f * static_cast<float>(snap.totalSuccess) /
                           static_cast<float>(total))
                        : 0.0f;

  std::printf(
      "Health: state=%s%s%s online=%s%s%s consec=%s%u%s ok=%s%lu%s fail=%s%lu%s rate=%s%.1f%%%s lastOk=%lu lastErr=%lu err=%s%s%s\n",
      failureColor(static_cast<uint32_t>(snap.consecutiveFailures)),
      app_driver::stateToString(snap.state),
      LOG_COLOR_RESET,
      boolColor(snap.online),
      boolText(snap.online),
      LOG_COLOR_RESET,
      failureColor(static_cast<uint32_t>(snap.consecutiveFailures)),
      static_cast<unsigned>(snap.consecutiveFailures),
      LOG_COLOR_RESET,
      successColor(snap.totalSuccess),
      static_cast<unsigned long>(snap.totalSuccess),
      LOG_COLOR_RESET,
      failureColor(snap.totalFailures),
      static_cast<unsigned long>(snap.totalFailures),
      LOG_COLOR_RESET,
      successRateColor(pct),
      static_cast<double>(pct),
      LOG_COLOR_RESET,
      static_cast<unsigned long>(snap.lastOkMs),
      static_cast<unsigned long>(snap.lastErrorMs),
      snap.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
      snap.lastError.ok() ? "OK" : app_driver::errToString(snap.lastError.code),
      LOG_COLOR_RESET);
}

void printHealthDiff(const HealthSnapshot& before, const HealthSnapshot& after) {
  bool changed = false;

  if (before.state != after.state) {
    std::printf("  State: %s%s%s -> %s%s%s\n",
                failureColor(static_cast<uint32_t>(before.consecutiveFailures)),
                app_driver::stateToString(before.state),
                LOG_COLOR_RESET,
                failureColor(static_cast<uint32_t>(after.consecutiveFailures)),
                app_driver::stateToString(after.state),
                LOG_COLOR_RESET);
    changed = true;
  }
  if (before.online != after.online) {
    std::printf("  Online: %s%s%s -> %s%s%s\n",
                boolColor(before.online),
                boolText(before.online),
                LOG_COLOR_RESET,
                boolColor(after.online),
                boolText(after.online),
                LOG_COLOR_RESET);
    changed = true;
  }
  if (before.consecutiveFailures != after.consecutiveFailures) {
    std::printf("  ConsecFail: %s%u -> %u%s\n",
                failureColor(static_cast<uint32_t>(after.consecutiveFailures)),
                static_cast<unsigned>(before.consecutiveFailures),
                static_cast<unsigned>(after.consecutiveFailures),
                LOG_COLOR_RESET);
    changed = true;
  }
  if (before.totalSuccess != after.totalSuccess) {
    std::printf("  TotalOK: %lu -> %s%lu (+%lu)%s\n",
                static_cast<unsigned long>(before.totalSuccess),
                LOG_COLOR_GREEN,
                static_cast<unsigned long>(after.totalSuccess),
                static_cast<unsigned long>(after.totalSuccess - before.totalSuccess),
                LOG_COLOR_RESET);
    changed = true;
  }
  if (before.totalFailures != after.totalFailures) {
    std::printf("  TotalFail: %lu -> %s%lu (+%lu)%s\n",
                static_cast<unsigned long>(before.totalFailures),
                LOG_COLOR_RED,
                static_cast<unsigned long>(after.totalFailures),
                static_cast<unsigned long>(after.totalFailures - before.totalFailures),
                LOG_COLOR_RESET);
    changed = true;
  }
  if (before.lastError.code != after.lastError.code ||
      before.lastError.detail != after.lastError.detail) {
    std::printf("  LastErr: %s%s%s -> %s%s%s\n",
                before.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                before.lastError.ok() ? "OK" : app_driver::errToString(before.lastError.code),
                LOG_COLOR_RESET,
                after.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                after.lastError.ok() ? "OK" : app_driver::errToString(after.lastError.code),
                LOG_COLOR_RESET);
    changed = true;
  }
  if (before.lastOkMs != after.lastOkMs) {
    std::printf("  LastOKms: %lu -> %lu\n",
                static_cast<unsigned long>(before.lastOkMs),
                static_cast<unsigned long>(after.lastOkMs));
    changed = true;
  }
  if (before.lastErrorMs != after.lastErrorMs) {
    std::printf("  LastErrMs: %lu -> %lu\n",
                static_cast<unsigned long>(before.lastErrorMs),
                static_cast<unsigned long>(after.lastErrorMs));
    changed = true;
  }
  if (!changed) {
    std::printf("  (no health changes)\n");
  }
}

void printSelfTestResultView() {
  std::printf("=== Self-Test Result ===\n");
  std::printf("  ready: %s%s%s\n",
              boolColor(device.selfTestReady()),
              yesNo(device.selfTestReady()),
              LOG_COLOR_RESET);
  std::printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));

  uint16_t raw = 0;
  const app_driver::Status st = device.getSelfTestResult(raw);
  if (st.ok()) {
    std::printf("  raw: 0x%04X\n", static_cast<unsigned>(raw));
    std::printf("  pass: %s%s%s\n", boolColor(raw == 0U), yesNo(raw == 0U), LOG_COLOR_RESET);
    return;
  }
  printStatus(st);

  const app_driver::Status rawSt = device.getSelfTestRawResult(raw);
  if (rawSt.ok()) {
    std::printf("  raw: 0x%04X\n", static_cast<unsigned>(raw));
    std::printf("  pass: %s%s%s\n", boolColor(raw == 0U), yesNo(raw == 0U), LOG_COLOR_RESET);
  } else if (rawSt.code != st.code || rawSt.detail != st.detail) {
    printStatus(rawSt);
  }
}

void printFrcResultView() {
  std::printf("=== Forced Recalibration Result ===\n");
  std::printf("  ready: %s%s%s\n",
              boolColor(device.forcedRecalibrationReady()),
              yesNo(device.forcedRecalibrationReady()),
              LOG_COLOR_RESET);
  std::printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));

  int16_t correctionPpm = 0;
  const app_driver::Status st = device.getForcedRecalibrationCorrectionPpm(correctionPpm);
  if (st.ok()) {
    std::printf("  correction_ppm: %d\n", static_cast<int>(correctionPpm));
  } else {
    printStatus(st);
  }

  uint16_t raw = 0;
  const app_driver::Status rawSt = device.getForcedRecalibrationRawResult(raw);
  if (rawSt.ok()) {
    std::printf("  raw: 0x%04X\n", static_cast<unsigned>(raw));
  } else if (rawSt.code != st.code || rawSt.detail != st.detail) {
    printStatus(rawSt);
  }
}

void printPendingWorkView(const char* title = "=== Pending Work ===") {
  const uint32_t now = idfNowMs();
  const uint32_t pendingLatencyMs =
      (gPendingRead && gPendingStartMs > 0U) ? (now - gPendingStartMs) : 0U;

  std::printf("%s\n", title);
  std::printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
  std::printf("  command_ready_ms: %lu\n", static_cast<unsigned long>(device.commandReadyMs()));
  std::printf("  measurement_pending: %s%s%s\n",
              boolColor(device.measurementPending()),
              yesNo(device.measurementPending()),
              LOG_COLOR_RESET);
  std::printf("  measurement_ready: %s%s%s\n",
              boolColor(device.measurementReady()),
              yesNo(device.measurementReady()),
              LOG_COLOR_RESET);
  std::printf("  measurement_ready_ms: %lu\n",
              static_cast<unsigned long>(device.measurementReadyMs()));
  std::printf("  pending_latency_ms: %lu\n", static_cast<unsigned long>(pendingLatencyMs));
  printToggleState("Watch", gWatchEnabled);
  printToggleState("Stress", gStressStats.active);
}

void printCommandCompletionView(app_driver::PendingCommand completed) {
  std::printf("=== Command Complete ===\n");
  std::printf("  command: %s\n", app_driver::pendingToString(completed));
  std::printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));
  std::printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
  std::printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
  std::printf("  measurement_ready: %s\n", yesNo(device.measurementReady()));
}

void printStatusView() {
  const uint32_t now = idfNowMs();

  std::printf("=== Status ===\n");
  std::printf("  initialized: %s%s%s\n",
              boolColor(device.isInitialized()),
              yesNo(device.isInitialized()),
              LOG_COLOR_RESET);
  std::printf("  state: %s%s%s\n",
              stateColor(device.state()),
              app_driver::stateToString(device.state()),
              LOG_COLOR_RESET);
  std::printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));

  if (!device.isInitialized()) {
    printPendingWorkView();
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  const uint32_t pendingLatencyMs =
      (gPendingRead && gPendingStartMs > 0U) ? (now - gPendingStartMs) : 0U;

  std::printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  std::printf("  pending: %s\n", app_driver::pendingToString(snap.pendingCommand));
  std::printf("  busy: %s%s%s\n", boolColor(snap.busy), yesNo(snap.busy), LOG_COLOR_RESET);
  std::printf("  command_ready_ms: %lu\n", static_cast<unsigned long>(snap.commandReadyMs));
  std::printf("  measurement_pending: %s%s%s\n",
              boolColor(snap.measurementPending),
              yesNo(snap.measurementPending),
              LOG_COLOR_RESET);
  std::printf("  measurement_ready: %s%s%s\n",
              boolColor(snap.measurementReady),
              yesNo(snap.measurementReady),
              LOG_COLOR_RESET);
  std::printf("  has_sample: %s%s%s\n",
              boolColor(snap.hasSample),
              yesNo(snap.hasSample),
              LOG_COLOR_RESET);
  std::printf("  measurement_ready_ms: %lu\n",
              static_cast<unsigned long>(snap.measurementReadyMs));
  std::printf("  pending_latency_ms: %lu\n", static_cast<unsigned long>(pendingLatencyMs));
  std::printf("  sample_timestamp_ms: %lu\n",
              static_cast<unsigned long>(snap.sampleTimestampMs));
  std::printf("  sample_age_ms: %lu\n", static_cast<unsigned long>(device.sampleAgeMs(now)));
  std::printf("  missed_samples_estimate: %lu\n",
              static_cast<unsigned long>(snap.missedSamples));
  std::printf("  last_sample_co2_valid: %s%s%s\n",
              boolColor(snap.lastSampleCo2Valid),
              yesNo(snap.lastSampleCo2Valid),
              LOG_COLOR_RESET);
  std::printf("  selftest_ready: %s%s%s\n",
              boolColor(device.selfTestReady()),
              yesNo(device.selfTestReady()),
              LOG_COLOR_RESET);
  std::printf("  frc_ready: %s%s%s\n",
              boolColor(device.forcedRecalibrationReady()),
              yesNo(device.forcedRecalibrationReady()),
              LOG_COLOR_RESET);
  printToggleState("Watch", gWatchEnabled);
  printToggleState("Stress", gStressStats.active);

  if (snap.pendingCommand != app_driver::PendingCommand::NONE) {
    std::printf("  data_ready: %sbusy (%s)%s\n",
                LOG_COLOR_GRAY,
                app_driver::pendingToString(snap.pendingCommand),
                LOG_COLOR_RESET);
    return;
  }
  if (snap.operatingMode == app_driver::OperatingMode::POWER_DOWN) {
    std::printf("  data_ready: %spowered down%s\n", LOG_COLOR_GRAY, LOG_COLOR_RESET);
    return;
  }

  app_driver::DataReadyStatus ready = {};
  const app_driver::Status readySt = device.readDataReadyStatus(ready);
  if (!readySt.ok()) {
    printStatus(readySt);
    return;
  }
  std::printf("  data_ready: %s%s%s raw=0x%04X\n",
              ready.ready ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
              yesNo(ready.ready),
              LOG_COLOR_RESET,
              static_cast<unsigned>(ready.raw));
}

void printSampleView() {
  const uint32_t now = idfNowMs();
  std::printf("=== Sample ===\n");

  std::printf("  measurement_ready: %s%s%s\n",
              boolColor(device.measurementReady()),
              yesNo(device.measurementReady()),
              LOG_COLOR_RESET);
  std::printf("  measurement_pending: %s%s%s\n",
              boolColor(device.measurementPending()),
              yesNo(device.measurementPending()),
              LOG_COLOR_RESET);
  std::printf("  measurement_ready_ms: %lu\n",
              static_cast<unsigned long>(device.measurementReadyMs()));

  if (!device.isInitialized()) {
    printStatus(app_driver::Status::Error(app_driver::Err::NOT_INITIALIZED, "begin() not called"));
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status snapSt = device.getSettings(snap);
  if (!snapSt.ok()) {
    printStatus(snapSt);
    return;
  }

  std::printf("  sample_timestamp_ms: %lu\n",
              static_cast<unsigned long>(snap.sampleTimestampMs));
  std::printf("  sample_age_ms: %lu\n", static_cast<unsigned long>(device.sampleAgeMs(now)));
  std::printf("  missed_samples_estimate: %lu\n",
              static_cast<unsigned long>(snap.missedSamples));
  std::printf("  last_sample_co2_valid: %s%s%s\n",
              boolColor(snap.lastSampleCo2Valid),
              yesNo(snap.lastSampleCo2Valid),
              LOG_COLOR_RESET);

  app_driver::Measurement sample;
  const app_driver::Status st = device.getLastMeasurement(sample);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  printMeasurement(sample);

  app_driver::RawSample raw = {};
  const app_driver::Status rawSt = device.getRawSample(raw);
  if (rawSt.ok()) {
    printRawSample(raw);
  } else {
    printStatus(rawSt);
  }

  app_driver::CompensatedSample comp = {};
  const app_driver::Status compSt = device.getCompensatedSample(comp);
  if (compSt.ok()) {
    printCompensatedSample(comp);
  } else if (compSt.code != rawSt.code || compSt.detail != rawSt.detail) {
    printStatus(compSt);
  }
}

void printIdentity() {
  app_driver::Identity identity;
  const app_driver::Status st = device.getIdentity(identity);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  std::printf("variant=%s serial=0x%012llX\n",
              app_driver::variantToString(identity.variant),
              static_cast<unsigned long long>(identity.serialNumber));
}

void printConfigView() {
  const app_driver::Config& cfg = device.isInitialized() ? device.getConfig() : gConfig;

  std::printf("=== Config ===\n");
  std::printf("  initialized: %s\n", yesNo(device.isInitialized()));
  std::printf("  address: 0x%02X\n", static_cast<unsigned>(cfg.i2cAddress));
  std::printf("  timeout_ms: %lu\n", static_cast<unsigned long>(cfg.i2cTimeoutMs));
  std::printf("  command_delay_ms: %u\n", static_cast<unsigned>(cfg.commandDelayMs));
  std::printf("  power_up_delay_ms: %u\n", static_cast<unsigned>(cfg.powerUpDelayMs));
  std::printf("  periodic_fetch_margin_ms: %lu\n",
              static_cast<unsigned long>(cfg.periodicFetchMarginMs));
  std::printf("  data_ready_retry_ms: %lu\n",
              static_cast<unsigned long>(cfg.dataReadyRetryMs));
  std::printf("  recover_backoff_ms: %lu\n",
              static_cast<unsigned long>(cfg.recoverBackoffMs));
  std::printf("  offline_threshold: %u\n", static_cast<unsigned>(cfg.offlineThreshold));
  std::printf("  strict_variant_check: %s\n", yesNo(cfg.strictVariantCheck));
  std::printf("  single_shot_mode: %s\n", singleShotModeToString(cfg.singleShotMode));
  std::printf("  hooks: nowMs=%s nowUs=%s yield=%s\n",
              yesNo(cfg.nowMs != nullptr),
              yesNo(cfg.nowUs != nullptr),
              yesNo(cfg.cooperativeYield != nullptr));
  std::printf("  controls: busReset=%s powerCycle=%s\n",
              yesNo(cfg.busReset != nullptr),
              yesNo(cfg.powerCycle != nullptr));
  std::printf("  idf_bus: port=%d sda=%d scl=%d freq=%lu\n",
              static_cast<int>(I2C_PORT),
              static_cast<int>(I2C_SDA),
              static_cast<int>(I2C_SCL),
              static_cast<unsigned long>(I2C_FREQ_HZ));
}

void printDriverView() {
  std::printf("=== Driver ===\n");
  printDriverHealth();

  if (!device.isInitialized()) {
    std::printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));
    std::printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
    std::printf("  busy: %s\n", yesNo(device.isBusy()));
    std::printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
    std::printf("  measurement_ready: %s\n", yesNo(device.measurementReady()));
    std::printf("  watch: %s\n", yesNo(gWatchEnabled));
    std::printf("  stress_active: %s\n", yesNo(gStressStats.active));
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    std::printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));
    std::printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
    std::printf("  busy: %s\n", yesNo(device.isBusy()));
    std::printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
    std::printf("  measurement_ready: %s\n", yesNo(device.measurementReady()));
    return;
  }

  const uint32_t pendingLatencyMs =
      (gPendingRead && gPendingStartMs > 0U) ? (idfNowMs() - gPendingStartMs) : 0U;
  std::printf("  mode: %s\n", app_driver::modeToString(snap.operatingMode));
  std::printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  std::printf("  pending: %s\n", app_driver::pendingToString(snap.pendingCommand));
  std::printf("  busy: %s\n", yesNo(snap.busy));
  std::printf("  command_ready_ms: %lu\n", static_cast<unsigned long>(snap.commandReadyMs));
  std::printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
  std::printf("  measurement_ready: %s\n", yesNo(snap.measurementReady));
  std::printf("  measurement_ready_ms: %lu\n",
              static_cast<unsigned long>(device.measurementReadyMs()));
  std::printf("  pending_latency_ms: %lu\n", static_cast<unsigned long>(pendingLatencyMs));
  std::printf("  sample_age_ms: %lu\n", static_cast<unsigned long>(device.sampleAgeMs(idfNowMs())));
  std::printf("  missed_samples_estimate: %lu\n",
              static_cast<unsigned long>(snap.missedSamples));
  std::printf("  last_sample_co2_valid: %s\n", yesNo(snap.lastSampleCo2Valid));
  std::printf("  serial_valid: %s\n", yesNo(snap.serialNumberValid));
  std::printf("  serial: 0x%012llX\n", static_cast<unsigned long long>(snap.serialNumber));
  std::printf("  variant: %s\n", app_driver::variantToString(snap.sensorVariant));
  std::printf("  selftest_ready: %s\n", yesNo(device.selfTestReady()));
  std::printf("  frc_ready: %s\n", yesNo(device.forcedRecalibrationReady()));
  std::printf("  watch: %s\n", yesNo(gWatchEnabled));
  std::printf("  stress_active: %s\n", yesNo(gStressStats.active));
  if (gStressStats.active) {
    std::printf("  stress_attempts: %d/%d\n", gStressStats.attempts, gStressStats.target);
    std::printf("  stress_success: %d\n", gStressStats.success);
    std::printf("  stress_errors: %lu\n", static_cast<unsigned long>(gStressStats.errors));
  }
}

void printSettingsView() {
  if (!device.isInitialized()) {
    std::printf("settings unavailable before begin()\n");
    printConfigView();
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.readSettings(snap);
  std::printf("=== Settings ===\n");
  if (!st.ok()) {
    printStatus(st);
    const app_driver::Status fallbackSt = device.getSettings(snap);
    if (!fallbackSt.ok()) {
      printStatus(fallbackSt);
      return;
    }
  } else {
    noteSettingsCache(snap);
  }
  std::printf("  mode: %s\n", app_driver::modeToString(snap.operatingMode));
  std::printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  std::printf("  pending: %s\n", app_driver::pendingToString(snap.pendingCommand));
  std::printf("  busy: %s\n", yesNo(snap.busy));
  std::printf("  serial: 0x%012llX (%s)\n",
              static_cast<unsigned long long>(snap.serialNumber),
              app_driver::variantToString(snap.sensorVariant));
  if (st.ok() && snap.liveConfigValid) {
    std::printf("  live_device_config: %sfull%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
  } else if (hasCachedSettings()) {
    std::printf("  live_device_config: %spartial / last-known%s\n",
                LOG_COLOR_YELLOW,
                LOG_COLOR_RESET);
  } else {
    std::printf("  live_device_config: %sunavailable%s\n", LOG_COLOR_RED, LOG_COLOR_RESET);
  }

  printKnownFloatField("temperature_offset:",
                       gSettingsCache.temperatureOffsetKnown,
                       static_cast<float>(gSettingsCache.temperatureOffsetC_x1000) / 1000.0f,
                       "C");
  printKnownU16Field("altitude:", gSettingsCache.altitudeKnown, gSettingsCache.sensorAltitudeM, "m");
  printKnownU32Field("ambient_pressure:",
                     gSettingsCache.ambientPressureKnown,
                     gSettingsCache.ambientPressurePa,
                     "Pa");
  printKnownBoolField("asc_enabled:",
                      gSettingsCache.ascEnabledKnown,
                      gSettingsCache.automaticSelfCalibrationEnabled);
  printKnownU16Field("asc_target:",
                     gSettingsCache.ascTargetKnown,
                     gSettingsCache.automaticSelfCalibrationTargetPpm,
                     "ppm");
  printKnownU16Field("asc_initial_period:",
                     gSettingsCache.ascInitialKnown,
                     gSettingsCache.automaticSelfCalibrationInitialPeriodHours,
                     "h");
  printKnownU16Field("asc_standard_period:",
                     gSettingsCache.ascStandardKnown,
                     gSettingsCache.automaticSelfCalibrationStandardPeriodHours,
                     "h");
}

app_driver::Status scheduleMeasurement() {
  const app_driver::Status st = device.requestMeasurement();
  if (st.inProgress()) {
    gPendingRead = true;
    gPendingStartMs = idfNowMs();
  } else if (!st.ok()) {
    gPendingRead = false;
    gPendingStartMs = 0;
  }
  return st;
}

bool scheduleStressAttempt() {
  if (!gStressStats.active) {
    return false;
  }
  if (gStressRemaining <= 0) {
    finishStressStats();
    return false;
  }

  --gStressRemaining;
  ++gStressStats.attempts;
  const app_driver::Status st = scheduleMeasurement();
  if (!st.ok() && !st.inProgress()) {
    noteStressError(st);
    printStatus(st);
    printStressProgress(static_cast<uint32_t>(gStressStats.attempts),
                        static_cast<uint32_t>(gStressStats.target),
                        static_cast<uint32_t>(gStressStats.success),
                        gStressStats.errors);
    finishStressStats();
    return false;
  }
  return true;
}

void scheduleFollowupMeasurement() {
  if (gStressStats.active) {
    if (gStressRemaining <= 0) {
      finishStressStats();
      return;
    }
    (void)scheduleStressAttempt();
    return;
  }

  if (!gWatchEnabled) {
    return;
  }

  const app_driver::Status st = scheduleMeasurement();
  if (!st.ok() && !st.inProgress()) {
    printStatus(st);
    gWatchEnabled = false;
  }
}

void handleMeasurementReady() {
  if (!gPendingRead || !device.measurementReady()) {
    return;
  }

  app_driver::Measurement sample;
  const app_driver::Status st = device.getMeasurement(sample);
  const uint32_t latencyMs = (gPendingStartMs > 0U) ? (idfNowMs() - gPendingStartMs) : 0U;
  gPendingRead = false;
  gPendingStartMs = 0;
  if (!st.ok()) {
    printStatus(st);
    if (gStressStats.active) {
      noteStressError(st);
      printStressProgress(static_cast<uint32_t>(gStressStats.attempts),
                          static_cast<uint32_t>(gStressStats.target),
                          static_cast<uint32_t>(gStressStats.success),
                          gStressStats.errors);
      if (gStressRemaining <= 0) {
        finishStressStats();
      } else {
        (void)scheduleStressAttempt();
      }
      return;
    }
    gWatchEnabled = false;
    return;
  }

  if (gVerbose && latencyMs > 0U) {
    std::printf("  latency_ms=%lu\n", static_cast<unsigned long>(latencyMs));
  }
  printMeasurement(sample);
  if (gStressStats.active) {
    updateStressStats(sample);
    printStressProgress(static_cast<uint32_t>(gStressStats.attempts),
                        static_cast<uint32_t>(gStressStats.target),
                        static_cast<uint32_t>(gStressStats.success),
                        gStressStats.errors);
  }
  scheduleFollowupMeasurement();
}

void handlePendingTransitions() {
  const app_driver::PendingCommand current = device.pendingCommand();
  if (gLastPending == current) {
    return;
  }

  if (gLastPending != app_driver::PendingCommand::NONE &&
      current == app_driver::PendingCommand::NONE) {
    switch (gLastPending) {
      case app_driver::PendingCommand::SELF_TEST:
        printSelfTestResultView();
        break;

      case app_driver::PendingCommand::FORCED_RECALIBRATION:
        printFrcResultView();
        break;

      case app_driver::PendingCommand::SINGLE_SHOT:
      case app_driver::PendingCommand::SINGLE_SHOT_RHT_ONLY:
        break;

      default:
        printCommandCompletionView(gLastPending);
        break;
    }
  }

  gLastPending = current;
}

int scanBus(uint8_t preferredAddress = SCD41_ADDR) {
  if (gBus == nullptr) {
    LOGW("I2C bus not initialized");
    return 0;
  }

  LOGI("Scanning I2C bus...");
  int count = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    const esp_err_t err =
        i2c_master_probe(gBus, static_cast<uint16_t>(address), static_cast<int>(gConfig.i2cTimeoutMs));
    if (err == ESP_OK) {
      const char* marker = (address == preferredAddress) ? "  <target>" : "";
      std::printf("  Found device at 0x%02X%s\n", static_cast<unsigned>(address), marker);
      ++count;
    }
  }

  if (count == 0) {
    LOGW("No I2C devices found");
  } else {
    LOGI("Found %d device(s)", count);
  }
  return count;
}

void runDiagnostics() {
  struct DiagStats {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } stats;

  enum class Outcome : uint8_t { PASS, FAIL, SKIP };
  auto report = [&](const char* name, Outcome outcome, const char* note) {
    const bool passed = (outcome == Outcome::PASS);
    const bool skipped = (outcome == Outcome::SKIP);
    const char* color = skipped ? LOG_COLOR_YELLOW : LOG_COLOR_RESULT(passed);
    const char* tag = skipped ? "SKIP" : (passed ? "PASS" : "FAIL");
    std::printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note != nullptr && note[0] != '\0') {
      std::printf(" - %s", note);
    }
    std::printf("\n");
    if (skipped) {
      ++stats.skip;
    } else if (passed) {
      ++stats.pass;
    } else {
      ++stats.fail;
    }
  };
  auto reportCheck = [&](const char* name, bool passed, const char* note) {
    report(name, passed ? Outcome::PASS : Outcome::FAIL, note);
  };
  auto reportSkip = [&](const char* name, const char* note) {
    report(name, Outcome::SKIP, note);
  };

  std::printf("=== SCD41 diagnostics ===\n");
  HealthSnapshot before;
  before.capture(device);
  const app_driver::Status probeSt = device.probe();
  HealthSnapshot afterProbe;
  afterProbe.capture(device);

  reportCheck("probe responds", probeSt.ok(), probeSt.ok() ? "" : app_driver::errToString(probeSt.code));
  reportCheck("probe no-health-side-effects", healthMatches(before, afterProbe), "");

  if (!device.isInitialized()) {
    reportSkip("driver diagnostics", "begin() not called");
    std::printf("Diagnostics: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(stats.pass),
                static_cast<unsigned long>(stats.pass),
                LOG_COLOR_RESET,
                goodIfZeroColor(stats.fail),
                static_cast<unsigned long>(stats.fail),
                LOG_COLOR_RESET,
                skipCountColor(stats.skip),
                static_cast<unsigned long>(stats.skip),
                LOG_COLOR_RESET);
    return;
  }

  app_driver::Identity identity;
  app_driver::Status st = device.getIdentity(identity);
  reportCheck("get identity", st.ok(), st.ok() ? "" : app_driver::errToString(st.code));
  reportCheck("serial nonzero", st.ok() && identity.serialNumber != 0ULL, "");
  reportCheck("variant recognized",
              st.ok() && identity.variant != app_driver::SensorVariant::UNKNOWN,
              "");

  app_driver::SettingsSnapshot snapshot;
  st = device.getSettings(snapshot);
  reportCheck("getSettings snapshot", st.ok(), st.ok() ? "" : app_driver::errToString(st.code));
  if (st.ok()) {
    const bool busyMatches =
        snapshot.busy == (snapshot.pendingCommand != app_driver::PendingCommand::NONE);
    reportCheck("snapshot busy flag consistent", busyMatches, "");
  }

  if (device.pendingCommand() != app_driver::PendingCommand::NONE ||
      device.operatingMode() == app_driver::OperatingMode::POWER_DOWN) {
    reportSkip("readSettings live snapshot", "driver busy or powered down");
    reportSkip("read data-ready status", "driver busy or powered down");
  } else {
    st = device.readSettings(snapshot);
    reportCheck("readSettings live snapshot",
                st.ok(),
                st.ok() ? "" : app_driver::errToString(st.code));
    if (st.ok()) {
      noteSettingsCache(snapshot);
      if (snapshot.operatingMode == app_driver::OperatingMode::IDLE) {
        reportCheck("idle live settings available", snapshot.liveConfigValid, "");
      } else {
        reportCheck("periodic snapshot returned", true, "idle-only settings stay partial");
      }
    }

    app_driver::DataReadyStatus ready = {};
    st = device.readDataReadyStatus(ready);
    reportCheck("read data-ready status", st.ok(), st.ok() ? "" : app_driver::errToString(st.code));
  }

  std::printf("Diagnostics: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
              goodIfNonZeroColor(stats.pass),
              static_cast<unsigned long>(stats.pass),
              LOG_COLOR_RESET,
              goodIfZeroColor(stats.fail),
              static_cast<unsigned long>(stats.fail),
              LOG_COLOR_RESET,
              skipCountColor(stats.skip),
              static_cast<unsigned long>(stats.skip),
              LOG_COLOR_RESET);
}

void runDemo() {
  std::printf("=== Demo ===\n");
  if (!device.isInitialized()) {
    printStatus(app_driver::Status::Error(app_driver::Err::NOT_INITIALIZED, "begin() not called"));
    return;
  }

  if (device.pendingCommand() != app_driver::PendingCommand::NONE || device.measurementPending()) {
    std::printf("  Driver already has pending work\n");
    printPendingWorkView();
    return;
  }

  if (device.measurementReady()) {
    app_driver::Measurement sample;
    const app_driver::Status st = device.getMeasurement(sample);
    printStatus(st);
    if (st.ok()) {
      printMeasurement(sample);
    }
    return;
  }

  const app_driver::Status st = scheduleMeasurement();
  printStatus(st);
  if (st.inProgress()) {
    printPendingWorkView();
  }
}

void printHelp() {
  std::printf("\n");
  printHelpHeader("SCD41 CLI Help");
  std::printf("Version: %s\n", SCD41::VERSION_FULL);

  printHelpSection("Common");
  printHelpItem("help / ?", "Show this help");
  printHelpItem("version / ver", "Print firmware and library version info");
  printHelpItem("info", "Print version info and current identity");
  printHelpItem("scan", "Scan I2C bus");
  printHelpItem("begin", "Run begin() with the current example config");
  printHelpItem("end", "End the current driver session");
  printHelpItem("probe", "Probe device without health tracking");
  printHelpItem("recover", "Attempt manual driver recovery");
  printHelpItem("diag", "Run safe diagnostics and health invariants");
  printHelpItem("demo", "Run a safe one-sample managed measurement workflow");
  printHelpItem("drv", "Print driver state and health");
  printHelpItem("drv1 / state", "Print compact health view");
  printHelpItem("cfg", "Show current example config");
  printHelpItem("settings", "Show driver snapshot and live device settings");
  printHelpItem("status", "Show concise chip/runtime status and data-ready state");
  printHelpItem("verbose [0|1]", "Toggle or set verbose polling logs");

  printHelpSection("Measurement");
  printHelpItem("read", "Read now if ready, otherwise schedule one managed measurement");
  printHelpItem("fetch", "Directly fetch a ready measurement now");
  printHelpItem("sample / last", "Show the last cached converted/raw/fixed-point sample");
  printHelpItem("raw", "Print the last raw sample");
  printHelpItem("comp", "Print the last compensated sample");
  printHelpItem("dataready", "Read get_data_ready_status");
  printHelpItem("watch [0|1]", "Continuously schedule measurements");
  printHelpItem("stress [N=100, 1..100000]", "Async measurement stress test with summary");
  printHelpItem("single [full|rht]", "Show or set idle single-shot mode");
  printHelpItem("single_start [full|rht]", "Start a one-shot full or RHT-only command");
  printHelpItem("convert <rawT> <rawRH> [co2]", "Convert raw values using library helpers");

  printHelpSection("Mode And Power");
  printHelpItem("mode", "Show current operating and single-shot mode");
  printHelpItem("periodic [on|lp|off]", "Start standard or low-power periodic mode, or stop");
  printHelpItem("sleep", "Power down the sensor");
  printHelpItem("wake", "Wake from power-down");

  printHelpSection("Identity And Compensation");
  printHelpItem("serial", "Read and print serial number and variant");
  printHelpItem("variant", "Read and print current variant");
  printHelpItem("toffset [degC, 0..20]", "Show or set temperature offset");
  printHelpItem("altitude [m, 0..3000]", "Show or set sensor altitude");
  printHelpItem("pressure [Pa, 70000..120000]", "Show or set ambient pressure compensation");
  printHelpItem("asc_enabled [0|1]", "Show or set automatic self-calibration enable");
  printHelpItem("asc_target [ppm, 1..40000]", "Show or set ASC target");
  printHelpItem("asc_initial [hours, 0 or 4-step]", "Show or set ASC initial period");
  printHelpItem("asc_standard [hours, >0 4-step]", "Show or set ASC standard period");

  printHelpSection("Maintenance");
  printHelpItem("persist confirm", "Write EEPROM-backed settings to EEPROM");
  printHelpItem("reinit", "Reload persisted settings into RAM");
  printHelpItem("factory_reset confirm", "Erase stored settings and calibration history");
  printHelpItem("selftest", "Run the 10 s self-test");
  printHelpItem("selftest_result", "Print the current self-test result state");
  printHelpItem("frc confirm <reference_ppm>", "Start forced recalibration");
  printHelpItem("frc_result", "Print the current forced recalibration result state");

  printHelpSection("Raw Commands");
  printHelpItem("command write <cmd>", "Issue an immediate non-stateful raw 16-bit command");
  printHelpItem("command write_data <cmd> <data>", "Issue an immediate command with one CRC-packed data word");
  printHelpItem("command read <cmd> <len, 1..9>", "UNSAFE raw byte read without CRC validation");
  printHelpItem("command read_word <cmd>", "Issue a short read command and decode one CRC-checked word");
  printHelpItem("command read_words <cmd> <count, 1..3>", "Issue a short read command and decode CRC-checked words");
  std::printf("  %sNote:%s raw commands are immediate diagnostics and do not reconcile cached driver state\n",
              LOG_COLOR_GRAY,
              LOG_COLOR_RESET);
}

void processConvert(const Line& tail) {
  Line first;
  Line rest;
  if (!splitHeadTail(tail, first, rest)) {
    LOGW("Usage: convert <rawT> <rawRH> [co2]");
    return;
  }

  Line second;
  Line third;
  if (!splitHeadTail(rest, second, third)) {
    LOGW("Usage: convert <rawT> <rawRH> [co2]");
    return;
  }

  uint16_t rawT = 0;
  uint16_t rawRh = 0;
  uint16_t rawCo2 = 0;
  if (!parseU16(first, rawT) || !parseU16(second, rawRh)) {
    LOGW("Expected rawT/rawRH as 16-bit values");
    return;
  }
  if (!third.empty() && !parseU16(third, rawCo2)) {
    LOGW("Expected optional CO2 raw as a 16-bit value");
    return;
  }

  std::printf("co2=%u ppm temp=%.3f C rh=%.3f %%\n",
              static_cast<unsigned>(rawCo2),
              static_cast<double>(app_driver::Device::convertTemperatureC(rawT)),
              static_cast<double>(app_driver::Device::convertHumidityPct(rawRh)));
}

void processCommand(const Line& cmdLine) {
  Line head;
  Line tail;
  if (!splitHeadTail(cmdLine, head, tail)) {
    return;
  }

  if (head == "help" || head == "?") {
    printHelp();
    return;
  }

  if (head == "version" || head == "ver") {
    printVersionInfo();
    return;
  }

  if (head == "info") {
    printVersionInfo();
    printIdentity();
    return;
  }

  if (head == "scan") {
    scanBus(SCD41_ADDR);
    return;
  }

  if (head == "begin") {
    clearSettingsCache();
    const app_driver::Status st = device.begin(gConfig);
    gLastPending = device.pendingCommand();
    printStatus(st);
    if (st.ok()) {
      printDriverHealth();
    }
    return;
  }

  if (head == "end") {
    cancelQueuedWork();
    clearSettingsCache();
    device.end();
    gLastPending = device.pendingCommand();
    std::printf("driver ended\n");
    return;
  }

  if (head == "probe") {
    HealthSnapshot before;
    before.capture(device);
    const app_driver::Status st = device.probe();
    HealthSnapshot after;
    after.capture(device);
    printStatus(st);
    std::printf("  Health changes:\n");
    printHealthDiff(before, after);
    return;
  }

  if (head == "recover") {
    HealthSnapshot before;
    before.capture(device);
    const app_driver::Status st = device.recover();
    HealthSnapshot after;
    after.capture(device);
    printStatus(st);
    std::printf("  Health changes:\n");
    printHealthDiff(before, after);
    printDriverHealth();
    return;
  }

  if (head == "diag") {
    runDiagnostics();
    return;
  }

  if (head == "demo") {
    runDemo();
    return;
  }

  if (head == "drv") {
    printDriverView();
    return;
  }

  if (head == "drv1" || head == "state") {
    printHealthView(device);
    return;
  }

  if (head == "cfg") {
    printConfigView();
    return;
  }

  if (head == "settings") {
    printSettingsView();
    return;
  }

  if (head == "status") {
    printStatusView();
    return;
  }

  if (head == "read") {
    app_driver::Measurement sample;
    const app_driver::Status st = device.readMeasurement(sample);
    if (st.ok()) {
      const uint32_t latencyMs =
          (gPendingRead && gPendingStartMs > 0U) ? (idfNowMs() - gPendingStartMs) : 0U;
      gPendingRead = false;
      gPendingStartMs = 0;
      if (gVerbose && latencyMs > 0U) {
        std::printf("  latency_ms=%lu\n", static_cast<unsigned long>(latencyMs));
      }
      printMeasurement(sample);
      if (gStressStats.active) {
        updateStressStats(sample);
        printStressProgress(static_cast<uint32_t>(gStressStats.attempts),
                            static_cast<uint32_t>(gStressStats.target),
                            static_cast<uint32_t>(gStressStats.success),
                            gStressStats.errors);
      }
      scheduleFollowupMeasurement();
      return;
    }
    if (st.code == app_driver::Err::MEASUREMENT_NOT_READY &&
        device.pendingCommand() == app_driver::PendingCommand::NONE &&
        !device.measurementPending()) {
      const app_driver::Status scheduleSt = scheduleMeasurement();
      printStatus(scheduleSt);
      if (scheduleSt.inProgress()) {
        printPendingWorkView();
      }
      return;
    }
    printStatus(st);
    return;
  }

  if (head == "sample" || head == "last") {
    printSampleView();
    return;
  }

  if (head == "fetch") {
    app_driver::Measurement sample;
    const app_driver::Status st = device.readMeasurement(sample);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    const uint32_t latencyMs =
        (gPendingRead && gPendingStartMs > 0U) ? (idfNowMs() - gPendingStartMs) : 0U;
    gPendingRead = false;
    gPendingStartMs = 0;
    if (gVerbose && latencyMs > 0U) {
      std::printf("  latency_ms=%lu\n", static_cast<unsigned long>(latencyMs));
    }
    printMeasurement(sample);
    if (gStressStats.active) {
      updateStressStats(sample);
      printStressProgress(static_cast<uint32_t>(gStressStats.attempts),
                          static_cast<uint32_t>(gStressStats.target),
                          static_cast<uint32_t>(gStressStats.success),
                          gStressStats.errors);
    }
    scheduleFollowupMeasurement();
    return;
  }

  if (head == "raw") {
    app_driver::RawSample sample;
    const app_driver::Status st = device.getRawSample(sample);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    printRawSample(sample);
    return;
  }

  if (head == "comp") {
    app_driver::CompensatedSample sample;
    const app_driver::Status st = device.getCompensatedSample(sample);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    printCompensatedSample(sample);
    return;
  }

  if (head == "dataready") {
    app_driver::DataReadyStatus ready;
    const app_driver::Status st = device.readDataReadyStatus(ready);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    std::printf("data_ready=%s%s%s raw=0x%04X\n",
                ready.ready ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                yesNo(ready.ready),
                LOG_COLOR_RESET,
                static_cast<unsigned>(ready.raw));
    return;
  }

  if (head == "watch") {
    if (tail.empty()) {
      printToggleState("Watch", gWatchEnabled);
      return;
    }

    bool enabled = false;
    if (!parseBool01(tail, enabled)) {
      LOGW("Expected watch 0|1");
      return;
    }

    if (enabled && gStressStats.active) {
      LOGW("watch is disabled while stress is active");
      return;
    }

    gWatchEnabled = enabled;
    printToggleState("Watch", gWatchEnabled);
    if (gWatchEnabled && device.measurementReady()) {
      gPendingRead = true;
      gPendingStartMs = 0;
      return;
    }
    if (gWatchEnabled && !gPendingRead && !device.measurementReady()) {
      const app_driver::Status st = scheduleMeasurement();
      if (!st.ok() && !st.inProgress()) {
        printStatus(st);
        gWatchEnabled = false;
      }
    }
    return;
  }

  if (head == "verbose") {
    bool enabled = !gVerbose;
    if (!tail.empty() && !parseBool01(tail, enabled)) {
      LOGW("Expected verbose 0|1");
      return;
    }
    gVerbose = enabled;
    printToggleState("Verbose", gVerbose);
    return;
  }

  if (head == "stress") {
    if (tail.empty()) {
      std::printf("stress_active=%s target=%d attempts=%d success=%d errors=%lu remaining=%d\n",
                  yesNo(gStressStats.active),
                  gStressStats.target,
                  gStressStats.attempts,
                  gStressStats.success,
                  static_cast<unsigned long>(gStressStats.errors),
                  gStressRemaining);
      return;
    }

    uint32_t count = DEFAULT_STRESS_COUNT;
    if (!parseU32(tail, count)) {
      LOGW("Expected stress [count]");
      return;
    }
    if (count == 0U || count > MAX_STRESS_COUNT) {
      std::printf("  Expected stress [1..%lu]\n", static_cast<unsigned long>(MAX_STRESS_COUNT));
      return;
    }

    cancelQueuedWork();
    gWatchEnabled = false;
    resetStressStats(static_cast<int>(count));
    gStressRemaining = static_cast<int>(count);
    std::printf("stress_target=%lu\n", static_cast<unsigned long>(count));
    if (device.measurementReady()) {
      --gStressRemaining;
      ++gStressStats.attempts;
      gPendingRead = true;
      gPendingStartMs = 0;
      return;
    }
    (void)scheduleStressAttempt();
    return;
  }

  if (head == "single_start") {
    app_driver::Status st = app_driver::Status::Error(app_driver::Err::INVALID_PARAM,
                                                      "Invalid single-shot command");
    if (tail.empty() || tail == "full") {
      st = device.startSingleShotMeasurement();
    } else if (tail == "rht") {
      st = device.startSingleShotRhtOnlyMeasurement();
    } else {
      LOGW("Usage: single_start [full|rht]");
      return;
    }
    if (st.inProgress()) {
      gPendingRead = true;
      gPendingStartMs = idfNowMs();
    }
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "single") {
    if (tail.empty()) {
      app_driver::SingleShotMode mode = app_driver::SingleShotMode::CO2_T_RH;
      const app_driver::Status st = device.getSingleShotMode(mode);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("single=%s\n", singleShotModeToString(mode));
      return;
    }

    if (tail == "full") {
      printStatus(device.setSingleShotMode(app_driver::SingleShotMode::CO2_T_RH));
      return;
    }
    if (tail == "rht") {
      printStatus(device.setSingleShotMode(app_driver::SingleShotMode::T_RH_ONLY));
      return;
    }
    LOGW("Usage: single [full|rht]");
    return;
  }

  if (head == "mode") {
    app_driver::SingleShotMode single = app_driver::SingleShotMode::CO2_T_RH;
    app_driver::Status st = device.getSingleShotMode(single);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    std::printf("mode=%s single=%s\n",
                app_driver::modeToString(device.operatingMode()),
                singleShotModeToString(single));
    return;
  }

  if (head == "periodic") {
    if (tail.empty() || tail == "on") {
      printStatus(device.startPeriodicMeasurement());
      return;
    }
    if (tail == "lp") {
      printStatus(device.startLowPowerPeriodicMeasurement());
      return;
    }
    if (tail == "off") {
      const app_driver::Status st = device.stopPeriodicMeasurement();
      printStatus(st);
      if (st.inProgress()) {
        printPendingWorkView();
      }
      return;
    }
    LOGW("Usage: periodic [on|lp|off]");
    return;
  }

  if (head == "sleep") {
    const app_driver::Status st = device.powerDown();
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "wake") {
    const app_driver::Status st = device.wakeUp();
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "serial") {
    printIdentity();
    return;
  }

  if (head == "variant") {
    app_driver::SensorVariant variant = app_driver::SensorVariant::UNKNOWN;
    const app_driver::Status st = device.readSensorVariant(variant);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    std::printf("variant=%s\n", app_driver::variantToString(variant));
    return;
  }

  if (head == "toffset") {
    if (tail.empty()) {
      float offsetC = 0.0f;
      const app_driver::Status st = device.getTemperatureOffsetC(offsetC);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("toffset=%.3f C\n", static_cast<double>(offsetC));
      return;
    }

    float offsetC = 0.0f;
    if (!parseFloat(tail, offsetC)) {
      LOGW("Expected toffset <degC>");
      return;
    }
    printStatus(device.setTemperatureOffsetC(offsetC));
    return;
  }

  if (head == "altitude") {
    if (tail.empty()) {
      uint16_t altitudeM = 0;
      const app_driver::Status st = device.getSensorAltitudeM(altitudeM);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("altitude=%u m\n", static_cast<unsigned>(altitudeM));
      return;
    }

    uint32_t altitudeM = 0;
    if (!parseU32(tail, altitudeM) || altitudeM > SCD41::cmd::ALTITUDE_MAX_M) {
      LOGW("Expected altitude <m> in 0..3000");
      return;
    }
    printStatus(device.setSensorAltitudeM(static_cast<uint16_t>(altitudeM)));
    return;
  }

  if (head == "pressure") {
    if (tail.empty()) {
      uint32_t pressurePa = 0;
      const app_driver::Status st = device.getAmbientPressurePa(pressurePa);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("pressure=%lu Pa\n", static_cast<unsigned long>(pressurePa));
      return;
    }

    uint32_t pressurePa = 0;
    if (!parseU32(tail, pressurePa)) {
      LOGW("Expected pressure <Pa>");
      return;
    }
    printStatus(device.setAmbientPressurePa(pressurePa));
    return;
  }

  if (head == "asc_enabled") {
    if (tail.empty()) {
      bool enabled = false;
      const app_driver::Status st = device.getAutomaticSelfCalibrationEnabled(enabled);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("asc_enabled=%s\n", yesNo(enabled));
      return;
    }

    bool enabled = false;
    if (!parseBool01(tail, enabled)) {
      LOGW("Expected asc_enabled 0|1");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationEnabled(enabled));
    return;
  }

  if (head == "asc_target") {
    if (tail.empty()) {
      uint16_t ppm = 0;
      const app_driver::Status st = device.getAutomaticSelfCalibrationTargetPpm(ppm);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("asc_target=%u ppm\n", static_cast<unsigned>(ppm));
      return;
    }

    uint16_t ppm = 0;
    if (!parseU16(tail, ppm)) {
      LOGW("Expected asc_target <ppm>");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationTargetPpm(ppm));
    return;
  }

  if (head == "asc_initial") {
    if (tail.empty()) {
      uint16_t hours = 0;
      const app_driver::Status st = device.getAutomaticSelfCalibrationInitialPeriodHours(hours);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("asc_initial=%u h\n", static_cast<unsigned>(hours));
      return;
    }

    uint16_t hours = 0;
    if (!parseU16(tail, hours)) {
      LOGW("Expected asc_initial <hours>");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationInitialPeriodHours(hours));
    return;
  }

  if (head == "asc_standard") {
    if (tail.empty()) {
      uint16_t hours = 0;
      const app_driver::Status st = device.getAutomaticSelfCalibrationStandardPeriodHours(hours);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      std::printf("asc_standard=%u h\n", static_cast<unsigned>(hours));
      return;
    }

    uint16_t hours = 0;
    if (!parseU16(tail, hours)) {
      LOGW("Expected asc_standard <hours>");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationStandardPeriodHours(hours));
    return;
  }

  if (head == "persist") {
    if (tail != "confirm") {
      LOGW("use 'persist confirm' to write EEPROM");
      return;
    }
    const app_driver::Status st = device.startPersistSettings();
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "reinit") {
    const app_driver::Status st = device.startReinit();
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "factory_reset") {
    if (tail != "confirm") {
      LOGW("use 'factory_reset confirm' to erase/reset settings");
      return;
    }
    const app_driver::Status st = device.startFactoryReset();
    printStatus(st);
    if (st.inProgress()) {
      clearSettingsCache();
      printPendingWorkView();
    }
    return;
  }

  if (head == "selftest") {
    const app_driver::Status st = device.startSelfTest();
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "selftest_result") {
    printSelfTestResultView();
    return;
  }

  if (head == "frc") {
    Line confirmToken;
    Line referenceToken;
    if (!splitHeadTail(tail, confirmToken, referenceToken) || confirmToken != "confirm") {
      LOGW("use 'frc confirm <reference_ppm>' to update calibration history");
      return;
    }
    uint16_t referencePpm = 0;
    if (!parseU16(referenceToken, referencePpm)) {
      LOGW("Usage: frc confirm <reference_ppm>");
      return;
    }
    const app_driver::Status st = device.startForcedRecalibration(referencePpm);
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "frc_result") {
    printFrcResultView();
    return;
  }

  if (head == "command") {
    Line sub;
    Line rest;
    if (!splitHeadTail(tail, sub, rest)) {
      LOGW("Usage: command write|write_data|read|read_word|read_words ...");
      return;
    }

    if (sub == "write") {
      uint16_t command = 0;
      if (!parseU16(rest, command)) {
        LOGW("Usage: command write <cmd>");
        return;
      }
      printStatus(device.writeCommand(command));
      return;
    }

    if (sub == "write_data") {
      Line cmdToken;
      Line dataToken;
      if (!splitHeadTail(rest, cmdToken, dataToken)) {
        LOGW("Usage: command write_data <cmd> <data>");
        return;
      }
      uint16_t command = 0;
      uint16_t data = 0;
      if (!parseU16(cmdToken, command) || !parseU16(dataToken, data)) {
        LOGW("Invalid command or data");
        return;
      }
      printStatus(device.writeCommandWithData(command, data));
      return;
    }

    if (sub == "read") {
      Line cmdToken;
      Line lenToken;
      if (!splitHeadTail(rest, cmdToken, lenToken)) {
        LOGW("Usage: command read <cmd> <len>");
        return;
      }
      uint16_t command = 0;
      uint32_t len = 0;
      if (!parseU16(cmdToken, command) || !parseU32(lenToken, len) || len == 0U ||
          len > SCD41::cmd::MEASUREMENT_RESPONSE_LEN) {
        LOGW("Length must be 1..9");
        return;
      }
      uint8_t buf[SCD41::cmd::MEASUREMENT_RESPONSE_LEN] = {};
      const app_driver::Status st =
          device.readCommandUnsafe(command, buf, static_cast<size_t>(len));
      printStatus(st);
      if (!st.ok()) {
        return;
      }
      std::printf("data=");
      for (uint32_t i = 0; i < len; ++i) {
        std::printf("%s%02X", (i == 0U) ? "" : " ", static_cast<unsigned>(buf[i]));
      }
      std::printf("\n");
      return;
    }

    if (sub == "read_word") {
      uint16_t command = 0;
      if (!parseU16(rest, command)) {
        LOGW("Usage: command read_word <cmd>");
        return;
      }
      uint16_t value = 0;
      const app_driver::Status st = device.readWordCommand(command, value);
      printStatus(st);
      if (st.ok()) {
        std::printf("word=0x%04X (%u)\n", static_cast<unsigned>(value), static_cast<unsigned>(value));
      }
      return;
    }

    if (sub == "read_words") {
      Line cmdToken;
      Line countToken;
      if (!splitHeadTail(rest, cmdToken, countToken)) {
        LOGW("Usage: command read_words <cmd> <count>");
        return;
      }
      uint16_t command = 0;
      uint32_t count = 0;
      if (!parseU16(cmdToken, command) || !parseU32(countToken, count) || count == 0U ||
          count > 3U) {
        LOGW("Count must be 1..3");
        return;
      }
      uint16_t words[3] = {};
      const app_driver::Status st =
          device.readWordsCommand(command, words, static_cast<size_t>(count));
      printStatus(st);
      if (!st.ok()) {
        return;
      }
      for (uint32_t i = 0; i < count; ++i) {
        std::printf("word[%lu]=0x%04X (%u)\n",
                    static_cast<unsigned long>(i),
                    static_cast<unsigned>(words[i]),
                    static_cast<unsigned>(words[i]));
      }
      return;
    }

    LOGW("Usage: command write|write_data|read|read_word|read_words ...");
    return;
  }

  if (head == "convert") {
    processConvert(tail);
    return;
  }

  LOGW("Unknown command: '%s'. Type 'help'.", head.c_str());
}

esp_err_t createBus(i2c_master_bus_handle_t* bus) {
  i2c_master_bus_config_t busConfig = {};
  busConfig.i2c_port = I2C_PORT;
  busConfig.sda_io_num = I2C_SDA;
  busConfig.scl_io_num = I2C_SCL;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&busConfig, bus);
}

esp_err_t addDevice(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t* dev) {
  i2c_device_config_t devConfig = {};
  devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  devConfig.device_address = SCD41_ADDR;
  devConfig.scl_speed_hz = I2C_FREQ_HZ;
  return i2c_master_bus_add_device(bus, &devConfig, dev);
}

void configureDriver() {
  gI2cCtx.device = gDev;
  gI2cCtx.address = SCD41_ADDR;

  gConfig.i2cWrite = idfI2cWrite;
  gConfig.i2cWriteRead = idfI2cWriteRead;
  gConfig.i2cUser = &gI2cCtx;
  gConfig.i2cAddress = SCD41_ADDR;
  gConfig.nowMs = nowMs;
  gConfig.nowUs = nowUs;
  gConfig.cooperativeYield = cooperativeYield;
  gConfig.i2cTimeoutMs = 50;
  gConfig.transportCapabilities = app_driver::TransportCapability::TIMEOUT;
  gConfig.strictVariantCheck = true;
}

void setupCli() {
  LOGI("=== SCD41 Bringup CLI ===");
  printVersionInfo();

  LOGI("I2C initialized (SDA=%d, SCL=%d)", static_cast<int>(I2C_SDA), static_cast<int>(I2C_SCL));
  scanBus(SCD41_ADDR);

  const app_driver::Status st = device.begin(gConfig);
  gLastPending = device.pendingCommand();
  if (!st.ok()) {
    LOGE("Device initialization failed; CLI remains available for probe/recover");
    printStatus(st);
  } else {
    LOGI("Device initialized successfully");
    printDriverHealth();
  }

  std::printf("\nType 'help' for commands\n");
  printPrompt();
}

}  // namespace

extern "C" void app_main(void) {
  ESP_ERROR_CHECK(createBus(&gBus));
  ESP_ERROR_CHECK(addDevice(gBus, &gDev));
  configureDriver();
  setupCli();

  while (true) {
    const app_driver::Status tickSt = device.tick(idfNowMs());
    if (!tickSt.ok() && tickSt.code != app_driver::Err::MEASUREMENT_NOT_READY) {
      LOGW("Async tick completed with non-OK status");
      printStatus(tickSt);
    }
    handlePendingTransitions();
    handleMeasurementReady();

    if (gVerbose && gPendingRead) {
      static uint32_t lastLogMs = 0;
      const uint32_t now = idfNowMs();
      if ((now - lastLogMs) >= 1000U) {
        lastLogMs = now;
        std::printf("pending=%s ready=%s watch=%s stress=%d\n",
                    app_driver::pendingToString(device.pendingCommand()),
                    yesNo(device.measurementReady()),
                    yesNo(gWatchEnabled),
                    gStressRemaining);
      }
    }

    Line line;
    if (readLine(line)) {
      processCommand(line);
      printPrompt();
    }

    flushOut();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
