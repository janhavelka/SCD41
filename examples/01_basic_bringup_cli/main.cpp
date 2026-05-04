/// @file main.cpp
/// @brief Basic bringup CLI example for SCD41
/// @note This is an EXAMPLE, not part of the library

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "common/BoardConfig.h"
#include "common/BusDiag.h"
#include "common/CliShell.h"
#include "common/CommandHandler.h"
#include "common/DriverCompat.h"
#include "common/HealthDiag.h"
#include "common/HealthView.h"
#include "common/I2cTransport.h"
#include "common/CliStyle.h"
#include "common/Log.h"

namespace {

app_driver::Device device;
app_driver::Config gConfig;
bool gVerbose = false;
bool gPendingRead = false;
bool gWatchEnabled = false;
uint32_t gPendingStartMs = 0;
int gStressRemaining = 0;
app_driver::PendingCommand gLastPending = app_driver::PendingCommand::NONE;
static constexpr uint32_t STRESS_PROGRESS_UPDATES = 10U;
static constexpr uint32_t DEFAULT_STRESS_COUNT = 100U;
static constexpr uint32_t MAX_STRESS_COUNT = 100000U;

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

StressStats gStressStats;
SettingsCache gSettingsCache;

const char* yesNo(bool value) {
  return value ? "yes" : "no";
}

const char* onOff(bool value) {
  return value ? "ON" : "OFF";
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

const char* singleShotModeToString(app_driver::SingleShotMode mode) {
  switch (mode) {
    case app_driver::SingleShotMode::CO2_T_RH: return "full";
    case app_driver::SingleShotMode::T_RH_ONLY: return "rht";
    default: return "unknown";
  }
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

bool healthMatches(const HealthSnapshot<app_driver::Device>& before,
                   const HealthSnapshot<app_driver::Device>& after) {
  return before.state == after.state && before.online == after.online &&
         before.consecutiveFailures == after.consecutiveFailures &&
         before.totalFailures == after.totalFailures &&
         before.totalSuccess == after.totalSuccess && before.lastOkMs == after.lastOkMs &&
         before.lastErrorMs == after.lastErrorMs &&
         before.lastError.code == after.lastError.code &&
         before.lastError.detail == after.lastError.detail;
}

void printUnknownField(const char* label) {
  Serial.printf("  %-20s %sunknown%s\n", label, LOG_COLOR_GRAY, LOG_COLOR_RESET);
}

void printKnownBoolField(const char* label, bool known, bool value) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  Serial.printf("  %-20s %s%s%s\n",
                label,
                diag::boolColor(value),
                yesNo(value),
                LOG_COLOR_RESET);
}

void printKnownFloatField(const char* label, bool known, float value, const char* unit) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  Serial.printf("  %-20s %.3f %s\n", label, static_cast<double>(value), unit);
}

void printKnownU16Field(const char* label, bool known, uint16_t value, const char* unit) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  Serial.printf("  %-20s %u %s\n", label, static_cast<unsigned>(value), unit);
}

void printKnownU32Field(const char* label, bool known, uint32_t value, const char* unit) {
  if (!known) {
    printUnknownField(label);
    return;
  }
  Serial.printf("  %-20s %lu %s\n", label, static_cast<unsigned long>(value), unit);
}

void printPrompt() {
  cli::printPrompt();
}

void printToggleState(const char* label, bool enabled) {
  Serial.printf("  %s: %s%s%s\n", label, toggleColor(enabled), onOff(enabled), LOG_COLOR_RESET);
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
  Serial.printf("  Progress: %lu/%lu (%s%.0f%%%s, ok=%s%lu%s, fail=%s%lu%s)\n",
                static_cast<unsigned long>(completed),
                static_cast<unsigned long>(total),
                successRateColor(pct),
                pct,
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
  gStressStats.startMs = millis();
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
  gStressStats.endMs = millis();
  const uint32_t successDelta = device.totalSuccess() - gStressStats.successBefore;
  const uint32_t failDelta = device.totalFailures() - gStressStats.failBefore;
  const uint32_t durationMs = gStressStats.endMs - gStressStats.startMs;
  const float successPct =
      (gStressStats.attempts > 0)
          ? (100.0f * static_cast<float>(gStressStats.success) /
             static_cast<float>(gStressStats.attempts))
          : 0.0f;

  Serial.println("=== Stress Summary ===");
  Serial.printf("  Target:   %d\n", gStressStats.target);
  Serial.printf("  Attempts: %d\n", gStressStats.attempts);
  Serial.printf("  Success:  %s%d%s\n",
                goodIfNonZeroColor(static_cast<uint32_t>(gStressStats.success)),
                gStressStats.success,
                LOG_COLOR_RESET);
  Serial.printf("  Errors:   %s%lu%s\n",
                goodIfZeroColor(gStressStats.errors),
                static_cast<unsigned long>(gStressStats.errors),
                LOG_COLOR_RESET);
  Serial.printf("  Success rate: %s%.2f%%%s\n",
                successRateColor(successPct),
                successPct,
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0U) {
    Serial.printf("  Rate:     %.2f samples/s\n",
                  1000.0f * static_cast<float>(gStressStats.attempts) /
                      static_cast<float>(durationMs));
  }
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
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
    Serial.printf("  Temp C:   min=%.3f avg=%.3f max=%.3f\n",
                  static_cast<double>(gStressStats.minTemp),
                  static_cast<double>(avgTemp),
                  static_cast<double>(gStressStats.maxTemp));
    Serial.printf("  RH %%:     min=%.3f avg=%.3f max=%.3f\n",
                  static_cast<double>(gStressStats.minRh),
                  static_cast<double>(avgRh),
                  static_cast<double>(gStressStats.maxRh));
    if (gStressStats.hasValidCo2) {
      const uint32_t validCo2Count = static_cast<uint32_t>(gStressStats.success) -
                                     gStressStats.invalidCo2Samples;
      const float avgCo2 = static_cast<float>(gStressStats.sumCo2 /
                                              static_cast<double>(validCo2Count));
      Serial.printf("  CO2 ppm:  min=%u avg=%.1f max=%u invalid=%lu\n",
                    static_cast<unsigned>(gStressStats.minCo2),
                    static_cast<double>(avgCo2),
                    static_cast<unsigned>(gStressStats.maxCo2),
                    static_cast<unsigned long>(gStressStats.invalidCo2Samples));
    } else {
      Serial.printf("  CO2 ppm:  no valid CO2 samples (invalid=%lu)\n",
                    static_cast<unsigned long>(gStressStats.invalidCo2Samples));
    }
  }
  if (gStressStats.hasFailure) {
    Serial.println("  First failure:");
    printStatus(gStressStats.firstError);
    if (gStressStats.errors > 1U) {
      Serial.println("  Last failure:");
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
  Serial.printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
                statusColor(st),
                app_driver::errToString(st.code),
                LOG_COLOR_RESET,
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail));
  if (st.msg != nullptr && st.msg[0] != '\0') {
    Serial.printf("  Message: %s%s%s\n", LOG_COLOR_YELLOW, st.msg, LOG_COLOR_RESET);
  }
}

void printVersionInfo() {
  Serial.println("=== Version ===");
  Serial.printf("  Example build: %s %s\n", __DATE__, __TIME__);
  Serial.printf("  Library:       %s %s\n", app_driver::driverName(), app_driver::version());
  Serial.printf("  Full:          %s\n", app_driver::versionFull());
  Serial.printf("  Built:         %s\n", app_driver::buildTimestamp());
  Serial.printf("  Commit:        %s\n", app_driver::gitCommit());
  Serial.printf("  Status:        %s\n", app_driver::gitStatus());
}

void printMeasurement(const app_driver::Measurement& sample) {
  Serial.printf("Sample: CO2=%u ppm valid=%s%s%s T=%.3f C RH=%.3f %%\n",
                static_cast<unsigned>(sample.co2Ppm),
                diag::boolColor(sample.co2Valid),
                yesNo(sample.co2Valid),
                LOG_COLOR_RESET,
                static_cast<double>(sample.temperatureC),
                static_cast<double>(sample.humidityPct));
}

void printRawSample(const app_driver::RawSample& sample) {
  Serial.printf("raw_co2=%u raw_temp=0x%04X raw_rh=0x%04X\n",
                static_cast<unsigned>(sample.rawCo2),
                static_cast<unsigned>(sample.rawTemperature),
                static_cast<unsigned>(sample.rawHumidity));
}

void printCompensatedSample(const app_driver::CompensatedSample& sample) {
  Serial.printf("Comp: co2=%u ppm valid=%s%s%s temp=%ld mC rh=%lu m%%\n",
                static_cast<unsigned>(sample.co2Ppm),
                diag::boolColor(sample.co2Valid),
                yesNo(sample.co2Valid),
                LOG_COLOR_RESET,
                static_cast<long>(sample.tempC_x1000),
                static_cast<unsigned long>(sample.humidityPct_x1000));
}

void printDriverHealth() {
  const uint32_t now = millis();
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

  Serial.println("=== Driver Health ===");
  Serial.printf("  State: %s%s%s\n",
                diag::stateColor(device.state()),
                app_driver::stateToString(device.state()),
                LOG_COLOR_RESET);
  Serial.printf("  Online: %s%s%s\n",
                diag::boolColor(device.isOnline()),
                yesNo(device.isOnline()),
                LOG_COLOR_RESET);
  Serial.printf("  Consecutive failures: %s%u%s\n",
                goodIfZeroColor(device.consecutiveFailures()),
                static_cast<unsigned>(device.consecutiveFailures()),
                LOG_COLOR_RESET);
  Serial.printf("  Total success: %s%lu%s\n",
                goodIfNonZeroColor(totalOk),
                static_cast<unsigned long>(totalOk),
                LOG_COLOR_RESET);
  Serial.printf("  Total failures: %s%lu%s\n",
                goodIfZeroColor(totalFail),
                static_cast<unsigned long>(totalFail),
                LOG_COLOR_RESET);
  Serial.printf("  Success rate: %s%.1f%%%s\n",
                successRateColor(successRate),
                successRate,
                LOG_COLOR_RESET);
  if (lastOkMs > 0U) {
    Serial.printf("  Last OK: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastOkMs),
                  static_cast<unsigned long>(lastOkMs));
  } else {
    Serial.println("  Last OK: never");
  }
  if (lastErrorMs > 0U) {
    Serial.printf("  Last error: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastErrorMs),
                  static_cast<unsigned long>(lastErrorMs));
  } else {
    Serial.println("  Last error: never");
  }
  if (!lastErr.ok()) {
    Serial.printf("  Error code: %s%s%s\n",
                  LOG_COLOR_RED,
                  app_driver::errToString(lastErr.code),
                  LOG_COLOR_RESET);
    Serial.printf("  Error detail: %ld\n", static_cast<long>(lastErr.detail));
    if (lastErr.msg != nullptr && lastErr.msg[0] != '\0') {
      Serial.printf("  Error msg: %s\n", lastErr.msg);
    }
  }
}

void printSelfTestResultView() {
  Serial.println("=== Self-Test Result ===");
  Serial.printf("  ready: %s%s%s\n",
                diag::boolColor(device.selfTestReady()),
                yesNo(device.selfTestReady()),
                LOG_COLOR_RESET);
  Serial.printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));

  uint16_t raw = 0;
  const app_driver::Status st = device.getSelfTestResult(raw);
  if (st.ok()) {
    Serial.printf("  raw: 0x%04X\n", static_cast<unsigned>(raw));
    Serial.printf("  pass: %s%s%s\n",
                  diag::boolColor(raw == 0U),
                  yesNo(raw == 0U),
                  LOG_COLOR_RESET);
    return;
  }
  printStatus(st);

  const app_driver::Status rawSt = device.getSelfTestRawResult(raw);
  if (rawSt.ok()) {
    Serial.printf("  raw: 0x%04X\n", static_cast<unsigned>(raw));
    Serial.printf("  pass: %s%s%s\n",
                  diag::boolColor(raw == 0U),
                  yesNo(raw == 0U),
                  LOG_COLOR_RESET);
  } else if (rawSt.code != st.code || rawSt.detail != st.detail) {
    printStatus(rawSt);
  }
}

void printFrcResultView() {
  Serial.println("=== Forced Recalibration Result ===");
  Serial.printf("  ready: %s%s%s\n",
                diag::boolColor(device.forcedRecalibrationReady()),
                yesNo(device.forcedRecalibrationReady()),
                LOG_COLOR_RESET);
  Serial.printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));

  int16_t correctionPpm = 0;
  const app_driver::Status st = device.getForcedRecalibrationCorrectionPpm(correctionPpm);
  if (st.ok()) {
    Serial.printf("  correction_ppm: %d\n", static_cast<int>(correctionPpm));
  } else {
    printStatus(st);
  }

  uint16_t raw = 0;
  const app_driver::Status rawSt = device.getForcedRecalibrationRawResult(raw);
  if (rawSt.ok()) {
    Serial.printf("  raw: 0x%04X\n", static_cast<unsigned>(raw));
  } else if (rawSt.code != st.code || rawSt.detail != st.detail) {
    printStatus(rawSt);
  }
}

void printPendingWorkView(const char* title = "=== Pending Work ===") {
  const uint32_t now = millis();
  const uint32_t pendingLatencyMs =
      (gPendingRead && gPendingStartMs > 0U) ? (now - gPendingStartMs) : 0U;

  Serial.println(title);
  Serial.printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
  Serial.printf("  command_ready_ms: %lu\n", static_cast<unsigned long>(device.commandReadyMs()));
  Serial.printf("  measurement_pending: %s%s%s\n",
                diag::boolColor(device.measurementPending()),
                yesNo(device.measurementPending()),
                LOG_COLOR_RESET);
  Serial.printf("  measurement_ready: %s%s%s\n",
                diag::boolColor(device.measurementReady()),
                yesNo(device.measurementReady()),
                LOG_COLOR_RESET);
  Serial.printf("  measurement_ready_ms: %lu\n",
                static_cast<unsigned long>(device.measurementReadyMs()));
  Serial.printf("  pending_latency_ms: %lu\n", static_cast<unsigned long>(pendingLatencyMs));
  printToggleState("Watch", gWatchEnabled);
  printToggleState("Stress", gStressStats.active);
}

void printCommandCompletionView(app_driver::PendingCommand completed) {
  Serial.println("=== Command Complete ===");
  Serial.printf("  command: %s\n", app_driver::pendingToString(completed));
  Serial.printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));
  Serial.printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
  Serial.printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
  Serial.printf("  measurement_ready: %s\n", yesNo(device.measurementReady()));
}

void printStatusView() {
  const uint32_t now = millis();

  Serial.println("=== Status ===");
  Serial.printf("  initialized: %s%s%s\n",
                diag::boolColor(device.isInitialized()),
                yesNo(device.isInitialized()),
                LOG_COLOR_RESET);
  Serial.printf("  state: %s%s%s\n",
                diag::stateColor(device.state()),
                app_driver::stateToString(device.state()),
                LOG_COLOR_RESET);
  Serial.printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));

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

  Serial.printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  Serial.printf("  pending: %s\n", app_driver::pendingToString(snap.pendingCommand));
  Serial.printf("  busy: %s%s%s\n",
                diag::boolColor(snap.busy),
                yesNo(snap.busy),
                LOG_COLOR_RESET);
  Serial.printf("  command_ready_ms: %lu\n", static_cast<unsigned long>(snap.commandReadyMs));
  Serial.printf("  measurement_pending: %s%s%s\n",
                diag::boolColor(snap.measurementPending),
                yesNo(snap.measurementPending),
                LOG_COLOR_RESET);
  Serial.printf("  measurement_ready: %s%s%s\n",
                diag::boolColor(snap.measurementReady),
                yesNo(snap.measurementReady),
                LOG_COLOR_RESET);
  Serial.printf("  measurement_ready_ms: %lu\n",
                static_cast<unsigned long>(snap.measurementReadyMs));
  Serial.printf("  pending_latency_ms: %lu\n", static_cast<unsigned long>(pendingLatencyMs));
  Serial.printf("  sample_timestamp_ms: %lu\n",
                static_cast<unsigned long>(snap.sampleTimestampMs));
  Serial.printf("  sample_age_ms: %lu\n",
                static_cast<unsigned long>(device.sampleAgeMs(now)));
  Serial.printf("  missed_samples_estimate: %lu\n",
                static_cast<unsigned long>(snap.missedSamples));
  Serial.printf("  last_sample_co2_valid: %s%s%s\n",
                diag::boolColor(snap.lastSampleCo2Valid),
                yesNo(snap.lastSampleCo2Valid),
                LOG_COLOR_RESET);
  Serial.printf("  selftest_ready: %s%s%s\n",
                diag::boolColor(device.selfTestReady()),
                yesNo(device.selfTestReady()),
                LOG_COLOR_RESET);
  Serial.printf("  frc_ready: %s%s%s\n",
                diag::boolColor(device.forcedRecalibrationReady()),
                yesNo(device.forcedRecalibrationReady()),
                LOG_COLOR_RESET);
  printToggleState("Watch", gWatchEnabled);
  printToggleState("Stress", gStressStats.active);

  if (snap.pendingCommand != app_driver::PendingCommand::NONE) {
    Serial.printf("  data_ready: %sbusy (%s)%s\n",
                  LOG_COLOR_GRAY,
                  app_driver::pendingToString(snap.pendingCommand),
                  LOG_COLOR_RESET);
    return;
  }
  if (snap.operatingMode == app_driver::OperatingMode::POWER_DOWN) {
    Serial.printf("  data_ready: %spowered down%s\n", LOG_COLOR_GRAY, LOG_COLOR_RESET);
    return;
  }

  app_driver::DataReadyStatus ready = {};
  const app_driver::Status readySt = device.readDataReadyStatus(ready);
  if (!readySt.ok()) {
    printStatus(readySt);
    return;
  }
  Serial.printf("  data_ready: %s%s%s raw=0x%04X\n",
                ready.ready ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                yesNo(ready.ready),
                LOG_COLOR_RESET,
                static_cast<unsigned>(ready.raw));
}

void printSampleView() {
  const uint32_t now = millis();
  Serial.println("=== Sample ===");

  Serial.printf("  measurement_ready: %s%s%s\n",
                diag::boolColor(device.measurementReady()),
                yesNo(device.measurementReady()),
                LOG_COLOR_RESET);
  Serial.printf("  measurement_pending: %s%s%s\n",
                diag::boolColor(device.measurementPending()),
                yesNo(device.measurementPending()),
                LOG_COLOR_RESET);
  Serial.printf("  measurement_ready_ms: %lu\n",
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

  Serial.printf("  sample_timestamp_ms: %lu\n",
                static_cast<unsigned long>(snap.sampleTimestampMs));
  Serial.printf("  sample_age_ms: %lu\n",
                static_cast<unsigned long>(device.sampleAgeMs(now)));
  Serial.printf("  missed_samples_estimate: %lu\n",
                static_cast<unsigned long>(snap.missedSamples));
  Serial.printf("  last_sample_co2_valid: %s%s%s\n",
                diag::boolColor(snap.lastSampleCo2Valid),
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

  Serial.printf("variant=%s serial=0x%012llX\n",
                app_driver::variantToString(identity.variant),
                static_cast<unsigned long long>(identity.serialNumber));
}

void printConfigView() {
  const app_driver::Config& cfg = device.isInitialized() ? device.getConfig() : gConfig;

  Serial.println("=== Config ===");
  Serial.printf("  initialized: %s\n", yesNo(device.isInitialized()));
  Serial.printf("  address: 0x%02X\n", cfg.i2cAddress);
  Serial.printf("  timeout_ms: %lu\n", static_cast<unsigned long>(cfg.i2cTimeoutMs));
  Serial.printf("  command_delay_ms: %u\n", static_cast<unsigned>(cfg.commandDelayMs));
  Serial.printf("  power_up_delay_ms: %u\n", static_cast<unsigned>(cfg.powerUpDelayMs));
  Serial.printf("  periodic_fetch_margin_ms: %lu\n",
                static_cast<unsigned long>(cfg.periodicFetchMarginMs));
  Serial.printf("  data_ready_retry_ms: %lu\n",
                static_cast<unsigned long>(cfg.dataReadyRetryMs));
  Serial.printf("  recover_backoff_ms: %lu\n",
                static_cast<unsigned long>(cfg.recoverBackoffMs));
  Serial.printf("  offline_threshold: %u\n", static_cast<unsigned>(cfg.offlineThreshold));
  Serial.printf("  strict_variant_check: %s\n", yesNo(cfg.strictVariantCheck));
  Serial.printf("  single_shot_mode: %s\n", singleShotModeToString(cfg.singleShotMode));
  Serial.printf("  hooks: nowMs=%s nowUs=%s yield=%s\n",
                yesNo(cfg.nowMs != nullptr),
                yesNo(cfg.nowUs != nullptr),
                yesNo(cfg.cooperativeYield != nullptr));
  Serial.printf("  controls: busReset=%s powerCycle=%s\n",
                yesNo(cfg.busReset != nullptr),
                yesNo(cfg.powerCycle != nullptr));
}

void printDriverView() {
  Serial.println("=== Driver ===");
  printDriverHealth();

  if (!device.isInitialized()) {
    Serial.printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));
    Serial.printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
    Serial.printf("  busy: %s\n", yesNo(device.isBusy()));
    Serial.printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
    Serial.printf("  measurement_ready: %s\n", yesNo(device.measurementReady()));
    Serial.printf("  watch: %s\n", yesNo(gWatchEnabled));
    Serial.printf("  stress_active: %s\n", yesNo(gStressStats.active));
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    Serial.printf("  mode: %s\n", app_driver::modeToString(device.operatingMode()));
    Serial.printf("  pending: %s\n", app_driver::pendingToString(device.pendingCommand()));
    Serial.printf("  busy: %s\n", yesNo(device.isBusy()));
    Serial.printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
    Serial.printf("  measurement_ready: %s\n", yesNo(device.measurementReady()));
    return;
  }

  const uint32_t pendingLatencyMs =
      (gPendingRead && gPendingStartMs > 0U) ? (millis() - gPendingStartMs) : 0U;
  Serial.printf("  mode: %s\n", app_driver::modeToString(snap.operatingMode));
  Serial.printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  Serial.printf("  pending: %s\n", app_driver::pendingToString(snap.pendingCommand));
  Serial.printf("  busy: %s\n", yesNo(snap.busy));
  Serial.printf("  command_ready_ms: %lu\n", static_cast<unsigned long>(snap.commandReadyMs));
  Serial.printf("  measurement_pending: %s\n", yesNo(device.measurementPending()));
  Serial.printf("  measurement_ready: %s\n", yesNo(snap.measurementReady));
  Serial.printf("  measurement_ready_ms: %lu\n",
                static_cast<unsigned long>(device.measurementReadyMs()));
  Serial.printf("  pending_latency_ms: %lu\n", static_cast<unsigned long>(pendingLatencyMs));
  Serial.printf("  sample_age_ms: %lu\n",
                static_cast<unsigned long>(device.sampleAgeMs(millis())));
  Serial.printf("  missed_samples_estimate: %lu\n",
                static_cast<unsigned long>(snap.missedSamples));
  Serial.printf("  last_sample_co2_valid: %s\n", yesNo(snap.lastSampleCo2Valid));
  Serial.printf("  serial_valid: %s\n", yesNo(snap.serialNumberValid));
  Serial.printf("  serial: 0x%012llX\n", static_cast<unsigned long long>(snap.serialNumber));
  Serial.printf("  variant: %s\n", app_driver::variantToString(snap.sensorVariant));
  Serial.printf("  selftest_ready: %s\n", yesNo(device.selfTestReady()));
  Serial.printf("  frc_ready: %s\n", yesNo(device.forcedRecalibrationReady()));
  Serial.printf("  watch: %s\n", yesNo(gWatchEnabled));
  Serial.printf("  stress_active: %s\n", yesNo(gStressStats.active));
  if (gStressStats.active) {
    Serial.printf("  stress_attempts: %d/%d\n", gStressStats.attempts, gStressStats.target);
    Serial.printf("  stress_success: %d\n", gStressStats.success);
    Serial.printf("  stress_errors: %lu\n", static_cast<unsigned long>(gStressStats.errors));
  }
}

void printSettingsView() {
  if (!device.isInitialized()) {
    Serial.println("settings unavailable before begin()");
    printConfigView();
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.readSettings(snap);
  Serial.println("=== Settings ===");
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
  Serial.printf("  mode: %s\n", app_driver::modeToString(snap.operatingMode));
  Serial.printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  Serial.printf("  pending: %s\n", app_driver::pendingToString(snap.pendingCommand));
  Serial.printf("  busy: %s\n", yesNo(snap.busy));
  Serial.printf("  serial: 0x%012llX (%s)\n",
                static_cast<unsigned long long>(snap.serialNumber),
                app_driver::variantToString(snap.sensorVariant));
  if (st.ok() && snap.liveConfigValid) {
    Serial.printf("  live_device_config: %sfull%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
  } else if (hasCachedSettings()) {
    Serial.printf("  live_device_config: %spartial / last-known%s\n",
                  LOG_COLOR_YELLOW,
                  LOG_COLOR_RESET);
  } else {
    Serial.printf("  live_device_config: %sunavailable%s\n", LOG_COLOR_RED, LOG_COLOR_RESET);
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
    gPendingStartMs = millis();
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
  const uint32_t latencyMs =
      (gPendingStartMs > 0U) ? (millis() - gPendingStartMs) : 0U;
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
    Serial.printf("  latency_ms=%lu\n", static_cast<unsigned long>(latencyMs));
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
      case app_driver::PendingCommand::SELF_TEST: {
        printSelfTestResultView();
      } break;

      case app_driver::PendingCommand::FORCED_RECALIBRATION: {
        printFrcResultView();
      } break;

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
    Serial.printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note != nullptr && note[0] != '\0') {
      Serial.printf(" - %s", note);
    }
    Serial.println();
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

  Serial.println("=== SCD41 diagnostics ===");
  HealthSnapshot<app_driver::Device> before;
  before.capture(device);
  const app_driver::Status probeSt = device.probe();
  HealthSnapshot<app_driver::Device> afterProbe;
  afterProbe.capture(device);

  reportCheck("probe responds",
              probeSt.ok(),
              probeSt.ok() ? "" : app_driver::errToString(probeSt.code));
  reportCheck("probe no-health-side-effects", healthMatches(before, afterProbe), "");

  if (!device.isInitialized()) {
    reportSkip("driver diagnostics", "begin() not called");
    Serial.printf("Diagnostics: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
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
  reportCheck("get identity",
              st.ok(),
              st.ok() ? "" : app_driver::errToString(st.code));
  reportCheck("serial nonzero", st.ok() && identity.serialNumber != 0ULL, "");
  reportCheck("variant recognized",
              st.ok() && identity.variant != app_driver::SensorVariant::UNKNOWN,
              "");

  app_driver::SettingsSnapshot snapshot;
  st = device.getSettings(snapshot);
  reportCheck("getSettings snapshot",
              st.ok(),
              st.ok() ? "" : app_driver::errToString(st.code));
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
    reportCheck("read data-ready status",
                st.ok(),
                st.ok() ? "" : app_driver::errToString(st.code));
  }

  Serial.printf("Diagnostics: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
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

void printHelp() {
  Serial.println();
  cli::printHelpHeader("SCD41 CLI Help");
  Serial.printf("Version: %s\n", app_driver::versionFull());

  cli::printHelpSection("Common");
  cli::printHelpItem("help / ?", "Show this help");
  cli::printHelpItem("version / ver", "Print firmware and library version info");
  cli::printHelpItem("info", "Print version info and current identity");
  cli::printHelpItem("scan", "Scan I2C bus");
  cli::printHelpItem("begin", "Run begin() with the current example config");
  cli::printHelpItem("end", "End the current driver session");
  cli::printHelpItem("probe", "Probe device without health tracking");
  cli::printHelpItem("recover", "Attempt manual driver recovery");
  cli::printHelpItem("diag", "Run safe diagnostics and health invariants");
  cli::printHelpItem("drv", "Print driver state and health");
  cli::printHelpItem("drv1 / state", "Print compact health view");
  cli::printHelpItem("cfg", "Show current example config");
  cli::printHelpItem("settings", "Show driver snapshot and live device settings");
  cli::printHelpItem("status", "Show concise chip/runtime status and data-ready state");
  cli::printHelpItem("verbose [0|1]", "Toggle or set verbose polling logs");

  cli::printHelpSection("Measurement");
  cli::printHelpItem("read", "Read now if ready, otherwise schedule one managed measurement");
  cli::printHelpItem("fetch", "Directly fetch a ready measurement now");
  cli::printHelpItem("sample / last", "Show the last cached converted/raw/fixed-point sample");
  cli::printHelpItem("raw", "Print the last raw sample");
  cli::printHelpItem("comp", "Print the last compensated sample");
  cli::printHelpItem("dataready", "Read get_data_ready_status");
  cli::printHelpItem("watch [0|1]", "Continuously schedule measurements");
  cli::printHelpItem("stress [N]", "Async measurement stress test with summary");
  cli::printHelpItem("single [full|rht]", "Show or set idle single-shot mode");
  cli::printHelpItem("single_start [full|rht]", "Start a one-shot full or RHT-only command");
  cli::printHelpItem("convert <rawT> <rawRH> [co2]", "Convert raw values using library helpers");

  cli::printHelpSection("Mode And Power");
  cli::printHelpItem("mode", "Show current operating and single-shot mode");
  cli::printHelpItem("periodic [on|lp|off]", "Start standard or low-power periodic mode, or stop");
  cli::printHelpItem("sleep", "Power down the sensor");
  cli::printHelpItem("wake", "Wake from power-down");

  cli::printHelpSection("Identity And Compensation");
  cli::printHelpItem("serial", "Read and print serial number and variant");
  cli::printHelpItem("variant", "Read and print current variant");
  cli::printHelpItem("toffset [degC]", "Show or set temperature offset");
  cli::printHelpItem("altitude [m]", "Show or set sensor altitude");
  cli::printHelpItem("pressure [Pa]", "Show or set ambient pressure compensation");
  cli::printHelpItem("asc_enabled [0|1]", "Show or set automatic self-calibration enable");
  cli::printHelpItem("asc_target [ppm]", "Show or set ASC target");
  cli::printHelpItem("asc_initial [hours]", "Show or set ASC initial period");
  cli::printHelpItem("asc_standard [hours]", "Show or set ASC standard period");

  cli::printHelpSection("Maintenance");
  cli::printHelpItem("persist", "Persist EEPROM-backed settings");
  cli::printHelpItem("reinit", "Reload persisted settings into RAM");
  cli::printHelpItem("factory_reset", "Perform factory reset");
  cli::printHelpItem("selftest", "Run the 10 s self-test");
  cli::printHelpItem("selftest_result", "Print the current self-test result state");
  cli::printHelpItem("frc <reference_ppm>", "Start forced recalibration");
  cli::printHelpItem("frc_result", "Print the current forced recalibration result state");

  cli::printHelpSection("Raw Commands");
  cli::printHelpItem("command write <cmd>", "Issue an immediate non-stateful raw 16-bit command");
  cli::printHelpItem("command write_data <cmd> <data>", "Issue an immediate command with one CRC-packed data word");
  cli::printHelpItem("command read <cmd> <len>", "Issue a short raw read command and print response bytes");
  cli::printHelpItem("command read_word <cmd>", "Issue a short read command and decode one CRC-checked word");
  cli::printHelpItem("command read_words <cmd> <count>", "Issue a short read command and decode CRC-checked words");
  Serial.printf("  %sNote:%s raw commands are immediate diagnostics and do not reconcile cached driver state\n",
                LOG_COLOR_GRAY,
                LOG_COLOR_RESET);
}

void processConvert(const String& tail) {
  String first;
  String rest;
  if (!cmd::splitHeadTail(tail, first, rest)) {
    LOGW("Usage: convert <rawT> <rawRH> [co2]");
    return;
  }

  String second;
  String third;
  if (!cmd::splitHeadTail(rest, second, third)) {
    LOGW("Usage: convert <rawT> <rawRH> [co2]");
    return;
  }

  uint16_t rawT = 0;
  uint16_t rawRh = 0;
  uint16_t rawCo2 = 0;
  if (!cmd::parseU16(first, rawT) || !cmd::parseU16(second, rawRh)) {
    LOGW("Expected rawT/rawRH as 16-bit values");
    return;
  }
  if (third.length() > 0U && !cmd::parseU16(third, rawCo2)) {
    LOGW("Expected optional CO2 raw as a 16-bit value");
    return;
  }

  Serial.printf("co2=%u ppm temp=%.3f C rh=%.3f %%\n",
                static_cast<unsigned>(rawCo2),
                static_cast<double>(app_driver::Device::convertTemperatureC(rawT)),
                static_cast<double>(app_driver::Device::convertHumidityPct(rawRh)));
}

void processCommand(const String& cmdLine) {
  String head;
  String tail;
  if (!cmd::splitHeadTail(cmdLine, head, tail)) {
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
    bus_diag::scan(SCD41::cmd::I2C_ADDRESS);
    return;
  }

  if (head == "begin") {
    cancelQueuedWork();
    clearSettingsCache();
    const app_driver::Status st = device.begin(gConfig);
    gLastPending = device.pendingCommand();
    printStatus(st);
    return;
  }

  if (head == "end") {
    cancelQueuedWork();
    clearSettingsCache();
    device.end();
    gLastPending = device.pendingCommand();
    Serial.println("driver ended");
    return;
  }

  if (head == "probe") {
    HealthSnapshot<app_driver::Device> before;
    before.capture(device);
    const app_driver::Status st = device.probe();
    HealthSnapshot<app_driver::Device> after;
    after.capture(device);
    printStatus(st);
    Serial.println("  Health changes:");
    printHealthDiff(before, after);
    return;
  }

  if (head == "recover") {
    HealthSnapshot<app_driver::Device> before;
    before.capture(device);
    const app_driver::Status st = device.recover();
    HealthSnapshot<app_driver::Device> after;
    after.capture(device);
    printStatus(st);
    Serial.println("  Health changes:");
    printHealthDiff(before, after);
    printDriverHealth();
    return;
  }

  if (head == "diag") {
    runDiagnostics();
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
          (gPendingRead && gPendingStartMs > 0U) ? (millis() - gPendingStartMs) : 0U;
      gPendingRead = false;
      gPendingStartMs = 0;
      if (gVerbose && latencyMs > 0U) {
        Serial.printf("  latency_ms=%lu\n", static_cast<unsigned long>(latencyMs));
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
        (gPendingRead && gPendingStartMs > 0U) ? (millis() - gPendingStartMs) : 0U;
    gPendingRead = false;
    gPendingStartMs = 0;
    if (gVerbose && latencyMs > 0U) {
      Serial.printf("  latency_ms=%lu\n", static_cast<unsigned long>(latencyMs));
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
    Serial.printf("data_ready=%s%s%s raw=0x%04X\n",
                  ready.ready ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                  yesNo(ready.ready),
                  LOG_COLOR_RESET,
                  static_cast<unsigned>(ready.raw));
    return;
  }

  if (head == "watch") {
    if (tail.length() == 0U) {
      printToggleState("Watch", gWatchEnabled);
      return;
    }

    bool enabled = false;
    if (!cmd::parseBool01(tail, enabled)) {
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
    if (tail.length() > 0U && !cmd::parseBool01(tail, enabled)) {
      LOGW("Expected verbose 0|1");
      return;
    }
    gVerbose = enabled;
    printToggleState("Verbose", gVerbose);
    return;
  }

  if (head == "stress") {
    if (tail.length() == 0U) {
      Serial.printf("stress_active=%s target=%d attempts=%d success=%d errors=%lu remaining=%d\n",
                    yesNo(gStressStats.active),
                    gStressStats.target,
                    gStressStats.attempts,
                    gStressStats.success,
                    static_cast<unsigned long>(gStressStats.errors),
                    gStressRemaining);
      return;
    }

    uint32_t count = DEFAULT_STRESS_COUNT;
    if (!cmd::parseU32(tail, count)) {
      LOGW("Expected stress [count]");
      return;
    }
    if (count == 0U || count > MAX_STRESS_COUNT) {
      Serial.printf("  Expected stress [1..%lu]\n", static_cast<unsigned long>(MAX_STRESS_COUNT));
      return;
    }

    cancelQueuedWork();
    gWatchEnabled = false;
    resetStressStats(static_cast<int>(count));
    gStressRemaining = static_cast<int>(count);
    Serial.printf("stress_target=%lu\n", static_cast<unsigned long>(count));
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
    if (tail.length() == 0U || tail == "full") {
      st = device.startSingleShotMeasurement();
    } else if (tail == "rht") {
      st = device.startSingleShotRhtOnlyMeasurement();
    } else {
      LOGW("Usage: single_start [full|rht]");
      return;
    }
    if (st.inProgress()) {
      gPendingRead = true;
      gPendingStartMs = millis();
    }
    printStatus(st);
    if (st.inProgress()) {
      printPendingWorkView();
    }
    return;
  }

  if (head == "single") {
    if (tail.length() == 0U) {
      app_driver::SingleShotMode mode = app_driver::SingleShotMode::CO2_T_RH;
      const app_driver::Status st = device.getSingleShotMode(mode);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("single=%s\n", singleShotModeToString(mode));
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
    Serial.printf("mode=%s single=%s\n",
                  app_driver::modeToString(device.operatingMode()),
                  singleShotModeToString(single));
    return;
  }

  if (head == "periodic") {
    if (tail.length() == 0U || tail == "on") {
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
    Serial.printf("variant=%s\n", app_driver::variantToString(variant));
    return;
  }

  if (head == "toffset") {
    if (tail.length() == 0U) {
      float offsetC = 0.0f;
      const app_driver::Status st = device.getTemperatureOffsetC(offsetC);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("toffset=%.3f C\n", static_cast<double>(offsetC));
      return;
    }

    float offsetC = 0.0f;
    if (!cmd::parseFloat(tail, offsetC)) {
      LOGW("Expected toffset <degC>");
      return;
    }
    printStatus(device.setTemperatureOffsetC(offsetC));
    return;
  }

  if (head == "altitude") {
    if (tail.length() == 0U) {
      uint16_t altitudeM = 0;
      const app_driver::Status st = device.getSensorAltitudeM(altitudeM);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("altitude=%u m\n", static_cast<unsigned>(altitudeM));
      return;
    }

    uint32_t altitudeM = 0;
    if (!cmd::parseU32(tail, altitudeM) || altitudeM > 0xFFFFUL) {
      LOGW("Expected altitude <m>");
      return;
    }
    printStatus(device.setSensorAltitudeM(static_cast<uint16_t>(altitudeM)));
    return;
  }

  if (head == "pressure") {
    if (tail.length() == 0U) {
      uint32_t pressurePa = 0;
      const app_driver::Status st = device.getAmbientPressurePa(pressurePa);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("pressure=%lu Pa\n", static_cast<unsigned long>(pressurePa));
      return;
    }

    uint32_t pressurePa = 0;
    if (!cmd::parseU32(tail, pressurePa)) {
      LOGW("Expected pressure <Pa>");
      return;
    }
    printStatus(device.setAmbientPressurePa(pressurePa));
    return;
  }

  if (head == "asc_enabled") {
    if (tail.length() == 0U) {
      bool enabled = false;
      const app_driver::Status st = device.getAutomaticSelfCalibrationEnabled(enabled);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("asc_enabled=%s\n", yesNo(enabled));
      return;
    }

    bool enabled = false;
    if (!cmd::parseBool01(tail, enabled)) {
      LOGW("Expected asc_enabled 0|1");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationEnabled(enabled));
    return;
  }

  if (head == "asc_target") {
    if (tail.length() == 0U) {
      uint16_t ppm = 0;
      const app_driver::Status st = device.getAutomaticSelfCalibrationTargetPpm(ppm);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("asc_target=%u ppm\n", static_cast<unsigned>(ppm));
      return;
    }

    uint16_t ppm = 0;
    if (!cmd::parseU16(tail, ppm)) {
      LOGW("Expected asc_target <ppm>");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationTargetPpm(ppm));
    return;
  }

  if (head == "asc_initial") {
    if (tail.length() == 0U) {
      uint16_t hours = 0;
      const app_driver::Status st = device.getAutomaticSelfCalibrationInitialPeriodHours(hours);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("asc_initial=%u h\n", static_cast<unsigned>(hours));
      return;
    }

    uint16_t hours = 0;
    if (!cmd::parseU16(tail, hours)) {
      LOGW("Expected asc_initial <hours>");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationInitialPeriodHours(hours));
    return;
  }

  if (head == "asc_standard") {
    if (tail.length() == 0U) {
      uint16_t hours = 0;
      const app_driver::Status st = device.getAutomaticSelfCalibrationStandardPeriodHours(hours);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("asc_standard=%u h\n", static_cast<unsigned>(hours));
      return;
    }

    uint16_t hours = 0;
    if (!cmd::parseU16(tail, hours)) {
      LOGW("Expected asc_standard <hours>");
      return;
    }
    printStatus(device.setAutomaticSelfCalibrationStandardPeriodHours(hours));
    return;
  }

  if (head == "persist") {
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
    clearSettingsCache();
    const app_driver::Status st = device.startFactoryReset();
    printStatus(st);
    if (st.inProgress()) {
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
    uint16_t referencePpm = 0;
    if (!cmd::parseU16(tail, referencePpm)) {
      LOGW("Usage: frc <reference_ppm>");
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
    String sub;
    String rest;
    if (!cmd::splitHeadTail(tail, sub, rest)) {
      LOGW("Usage: command write|write_data|read|read_word|read_words ...");
      return;
    }

    if (sub == "write") {
      uint16_t command = 0;
      if (!cmd::parseU16(rest, command)) {
        LOGW("Usage: command write <cmd>");
        return;
      }
      printStatus(device.writeCommand(command));
      return;
    }

    if (sub == "write_data") {
      String cmdToken;
      String dataToken;
      if (!cmd::splitHeadTail(rest, cmdToken, dataToken)) {
        LOGW("Usage: command write_data <cmd> <data>");
        return;
      }
      uint16_t command = 0;
      uint16_t data = 0;
      if (!cmd::parseU16(cmdToken, command) || !cmd::parseU16(dataToken, data)) {
        LOGW("Invalid command or data");
        return;
      }
      printStatus(device.writeCommandWithData(command, data));
      return;
    }

    if (sub == "read") {
      String cmdToken;
      String lenToken;
      if (!cmd::splitHeadTail(rest, cmdToken, lenToken)) {
        LOGW("Usage: command read <cmd> <len>");
        return;
      }
      uint16_t command = 0;
      uint32_t len = 0;
      if (!cmd::parseU16(cmdToken, command) || !cmd::parseU32(lenToken, len) || len == 0U ||
          len > 16U) {
        LOGW("Length must be 1..16");
        return;
      }
      uint8_t buf[16] = {};
      const app_driver::Status st = device.readCommand(command, buf, static_cast<size_t>(len));
      printStatus(st);
      if (!st.ok()) {
        return;
      }
      Serial.print("data=");
      for (uint32_t i = 0; i < len; ++i) {
        Serial.printf("%s%02X", (i == 0U) ? "" : " ", static_cast<unsigned>(buf[i]));
      }
      Serial.println();
      return;
    }

    if (sub == "read_word") {
      uint16_t command = 0;
      if (!cmd::parseU16(rest, command)) {
        LOGW("Usage: command read_word <cmd>");
        return;
      }
      uint16_t value = 0;
      const app_driver::Status st = device.readWordCommand(command, value);
      printStatus(st);
      if (st.ok()) {
        Serial.printf("word=0x%04X (%u)\n", static_cast<unsigned>(value), static_cast<unsigned>(value));
      }
      return;
    }

    if (sub == "read_words") {
      String cmdToken;
      String countToken;
      if (!cmd::splitHeadTail(rest, cmdToken, countToken)) {
        LOGW("Usage: command read_words <cmd> <count>");
        return;
      }
      uint16_t command = 0;
      uint32_t count = 0;
      if (!cmd::parseU16(cmdToken, command) || !cmd::parseU32(countToken, count) || count == 0U ||
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
        Serial.printf("word[%lu]=0x%04X (%u)\n",
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

}  // namespace

void setup() {
  log_begin(115200);
  delay(100);
  LOGI("=== SCD41 Bringup CLI ===");
  printVersionInfo();

  if (!board::initI2c()) {
    LOGE("Wire init failed");
    return;
  }
  LOGI("I2C initialized (SDA=%d, SCL=%d)", board::I2C_SDA, board::I2C_SCL);
  bus_diag::scan(SCD41::cmd::I2C_ADDRESS);

  gConfig.i2cWrite = transport::wireWrite;
  gConfig.i2cWriteRead = transport::wireWriteRead;
  gConfig.i2cUser = &Wire;
  gConfig.i2cAddress = SCD41::cmd::I2C_ADDRESS;
  gConfig.transportCapabilities = app_driver::TransportCapability::NONE;
  gConfig.strictVariantCheck = true;

  const app_driver::Status st = device.begin(gConfig);
  gLastPending = device.pendingCommand();
  if (!st.ok()) {
    LOGE("Device initialization failed; CLI remains available for probe/recover");
    printStatus(st);
  } else {
    LOGI("Device initialized successfully");
    printDriverHealth();
  }

  Serial.println();
  Serial.println("Type 'help' for commands");
  printPrompt();
}

void loop() {
  device.tick(millis());
  handlePendingTransitions();
  handleMeasurementReady();

  if (gVerbose && gPendingRead) {
    static uint32_t lastLogMs = 0;
    const uint32_t now = millis();
    if ((now - lastLogMs) >= 1000U) {
      lastLogMs = now;
      Serial.printf("pending=%s ready=%s watch=%s stress=%d\n",
                    app_driver::pendingToString(device.pendingCommand()),
                    yesNo(device.measurementReady()),
                    yesNo(gWatchEnabled),
                    gStressRemaining);
    }
  }

  String line;
  if (cli_shell::readLine(line)) {
    processCommand(line);
    printPrompt();
  }
}
