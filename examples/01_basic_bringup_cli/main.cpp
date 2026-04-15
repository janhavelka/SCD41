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
#include "common/Log.h"

namespace {

app_driver::Device device;
app_driver::Config gConfig;
bool gVerbose = false;
bool gPendingRead = false;
bool gWatchEnabled = false;
int gStressRemaining = 0;
app_driver::PendingCommand gLastPending = app_driver::PendingCommand::NONE;

const char* yesNo(bool value) {
  return value ? "yes" : "no";
}

const char* singleShotModeToString(app_driver::SingleShotMode mode) {
  switch (mode) {
    case app_driver::SingleShotMode::CO2_T_RH: return "full";
    case app_driver::SingleShotMode::T_RH_ONLY: return "rht";
    default: return "unknown";
  }
}

void cancelQueuedWork() {
  gPendingRead = false;
  gWatchEnabled = false;
  gStressRemaining = 0;
}

void printStatus(const app_driver::Status& st) {
  const bool ok = st.ok() || st.inProgress();
  Serial.printf("%sstatus=%s code=%u detail=%ld%s\n",
                LOG_COLOR_RESULT(ok),
                app_driver::errToString(st.code),
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail),
                LOG_COLOR_RESET);
  if (st.msg != nullptr && st.msg[0] != '\0') {
    Serial.printf("msg=%s\n", st.msg);
  }
}

void printVersionInfo() {
  Serial.println("=== Version Info ===");
  Serial.printf("  Example firmware build: %s %s\n", __DATE__, __TIME__);
  Serial.printf("  %s library version: %s\n", app_driver::driverName(), app_driver::version());
  Serial.printf("  %s library full: %s\n", app_driver::driverName(), app_driver::versionFull());
  Serial.printf("  %s library build: %s\n", app_driver::driverName(), app_driver::buildTimestamp());
  Serial.printf("  %s library commit: %s (%s)\n",
                app_driver::driverName(),
                app_driver::gitCommit(),
                app_driver::gitStatus());
}

void printMeasurement(const app_driver::Measurement& sample) {
  Serial.printf("CO2=%u ppm (%s) T=%.3f C RH=%.3f %%\n",
                static_cast<unsigned>(sample.co2Ppm),
                yesNo(sample.co2Valid),
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
  Serial.printf("co2=%u ppm (%s) temp=%ld mC rh=%lu m%%\n",
                static_cast<unsigned>(sample.co2Ppm),
                yesNo(sample.co2Valid),
                static_cast<long>(sample.tempC_x1000),
                static_cast<unsigned long>(sample.humidityPct_x1000));
}

void printIdentity() {
  uint64_t serial = 0;
  const app_driver::Status serialSt = device.readSerialNumber(serial);
  if (!serialSt.ok()) {
    printStatus(serialSt);
    return;
  }

  app_driver::SensorVariant variant = app_driver::SensorVariant::UNKNOWN;
  const app_driver::Status variantSt = device.readSensorVariant(variant);
  if (!variantSt.ok()) {
    printStatus(variantSt);
    return;
  }

  Serial.printf("variant=%s serial=0x%012llX\n",
                app_driver::variantToString(variant),
                static_cast<unsigned long long>(serial));
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
  if (!device.isInitialized()) {
    Serial.println("=== Driver ===");
    printHealthView(device);
    Serial.printf("mode=%s pending=%s busy=%s measurementPending=%s measurementReady=%s\n",
                  app_driver::modeToString(device.operatingMode()),
                  app_driver::pendingToString(device.pendingCommand()),
                  yesNo(device.isBusy()),
                  yesNo(gPendingRead),
                  yesNo(device.measurementReady()));
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.println("=== Driver ===");
  diag::printHealthVerbose(device);
  Serial.printf("  mode: %s\n", app_driver::modeToString(snap.operatingMode));
  Serial.printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  Serial.printf("  pending: %s\n", app_driver::pendingToString(snap.pendingCommand));
  Serial.printf("  busy: %s\n", yesNo(snap.busy));
  Serial.printf("  command_ready_ms: %lu\n", static_cast<unsigned long>(snap.commandReadyMs));
  Serial.printf("  measurement_pending: %s\n", yesNo(snap.measurementPending));
  Serial.printf("  measurement_ready: %s\n", yesNo(snap.measurementReady));
  Serial.printf("  measurement_ready_ms: %lu\n",
                static_cast<unsigned long>(snap.measurementReadyMs));
  Serial.printf("  sample_age_ms: %lu\n",
                static_cast<unsigned long>(device.sampleAgeMs(millis())));
  Serial.printf("  missed_samples_estimate: %lu\n",
                static_cast<unsigned long>(snap.missedSamples));
  Serial.printf("  last_sample_co2_valid: %s\n", yesNo(snap.lastSampleCo2Valid));
  Serial.printf("  serial_valid: %s\n", yesNo(snap.serialNumberValid));
  Serial.printf("  serial: 0x%012llX\n", static_cast<unsigned long long>(snap.serialNumber));
  Serial.printf("  variant: %s\n", app_driver::variantToString(snap.sensorVariant));
  Serial.printf("  selftest_ready: %s frc_ready: %s\n",
                yesNo(device.selfTestReady()),
                yesNo(device.forcedRecalibrationReady()));
}

void printSettingsView() {
  if (!device.isInitialized()) {
    Serial.println("settings unavailable before begin()");
    printConfigView();
    return;
  }

  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.readSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.println("=== Settings ===");
  Serial.printf("  mode: %s\n", app_driver::modeToString(snap.operatingMode));
  Serial.printf("  single_shot_mode: %s\n", singleShotModeToString(snap.singleShotMode));
  Serial.printf("  serial: 0x%012llX (%s)\n",
                static_cast<unsigned long long>(snap.serialNumber),
                app_driver::variantToString(snap.sensorVariant));

  if (!snap.liveConfigValid) {
    Serial.println("  live_device_config: unavailable (driver busy, periodic active, or powered down)");
    return;
  }

  Serial.printf("  temperature_offset: %.3f C\n",
                static_cast<double>(snap.temperatureOffsetC_x1000) / 1000.0);
  Serial.printf("  altitude: %u m\n", static_cast<unsigned>(snap.sensorAltitudeM));
  Serial.printf("  ambient_pressure: %lu Pa\n",
                static_cast<unsigned long>(snap.ambientPressurePa));
  Serial.printf("  asc_enabled: %s\n", yesNo(snap.automaticSelfCalibrationEnabled));
  Serial.printf("  asc_target: %u ppm\n",
                static_cast<unsigned>(snap.automaticSelfCalibrationTargetPpm));
  Serial.printf("  asc_initial_period: %u h\n",
                static_cast<unsigned>(snap.automaticSelfCalibrationInitialPeriodHours));
  Serial.printf("  asc_standard_period: %u h\n",
                static_cast<unsigned>(snap.automaticSelfCalibrationStandardPeriodHours));
}

app_driver::Status scheduleMeasurement() {
  const app_driver::Status st = device.requestMeasurement();
  if (st.inProgress()) {
    gPendingRead = true;
  }
  return st;
}

void scheduleFollowupMeasurement() {
  if (gStressRemaining > 0) {
    --gStressRemaining;
    if (gStressRemaining == 0) {
      Serial.println("stress done");
      if (!gWatchEnabled) {
        return;
      }
    }
  }

  if (!gWatchEnabled && gStressRemaining == 0) {
    return;
  }

  const app_driver::Status st = scheduleMeasurement();
  if (!st.ok() && !st.inProgress()) {
    printStatus(st);
    gWatchEnabled = false;
    gStressRemaining = 0;
  }
}

void handleMeasurementReady() {
  if (!gPendingRead || !device.measurementReady()) {
    return;
  }

  app_driver::Measurement sample;
  const app_driver::Status st = device.getMeasurement(sample);
  if (!st.ok()) {
    printStatus(st);
    cancelQueuedWork();
    return;
  }

  printMeasurement(sample);
  gPendingRead = false;
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
        uint16_t raw = 0;
        const app_driver::Status st = device.getSelfTestResult(raw);
        printStatus(st);
        if (st.ok()) {
          Serial.printf("selftest=0x%04X pass=%s\n",
                        static_cast<unsigned>(raw),
                        yesNo(raw == 0U));
        }
      } break;

      case app_driver::PendingCommand::FORCED_RECALIBRATION: {
        int16_t correctionPpm = 0;
        const app_driver::Status st = device.getForcedRecalibrationCorrectionPpm(correctionPpm);
        printStatus(st);
        if (st.ok()) {
          Serial.printf("frc_correction=%d ppm\n", static_cast<int>(correctionPpm));
        }
      } break;

      default:
        Serial.printf("completed=%s mode=%s\n",
                      app_driver::pendingToString(gLastPending),
                      app_driver::modeToString(device.operatingMode()));
        break;
    }
  }

  gLastPending = current;
}

void printHelp() {
  auto helpSection = [](const char* title) {
    Serial.printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
  };
  auto helpItem = [](const char* command, const char* desc) {
    Serial.printf("  %s%-28s%s - %s\n",
                  LOG_COLOR_CYAN,
                  command,
                  LOG_COLOR_RESET,
                  desc);
  };

  Serial.println();
  Serial.printf("%s=== SCD41 CLI Help ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);

  helpSection("Common");
  helpItem("help / ?", "Show this help");
  helpItem("version / ver", "Print firmware and library version info");
  helpItem("info", "Print version info and current identity");
  helpItem("scan", "Scan I2C bus");
  helpItem("begin", "Run begin() with the current example config");
  helpItem("end", "End the current driver session");
  helpItem("probe", "Probe device without health tracking");
  helpItem("recover", "Attempt manual driver recovery");
  helpItem("drv", "Print driver state and health");
  helpItem("drv1", "Print compact health view");
  helpItem("cfg", "Show current example config");
  helpItem("settings", "Show driver snapshot and live device settings");
  helpItem("verbose [0|1]", "Enable or disable verbose polling logs");

  helpSection("Measurement");
  helpItem("read", "Request one measurement");
  helpItem("raw", "Print the last raw sample");
  helpItem("comp", "Print the last compensated sample");
  helpItem("dataready", "Read get_data_ready_status");
  helpItem("watch [0|1]", "Continuously schedule measurements");
  helpItem("stress [N]", "Run N measurement cycles");
  helpItem("single [full|rht]", "Show or set idle single-shot mode");
  helpItem("convert <rawT> <rawRH> [co2]", "Convert raw values using library helpers");

  helpSection("Mode And Power");
  helpItem("mode", "Show current operating and single-shot mode");
  helpItem("periodic [on|lp|off]", "Start standard or low-power periodic mode, or stop");
  helpItem("sleep", "Power down the sensor");
  helpItem("wake", "Wake from power-down");

  helpSection("Identity And Compensation");
  helpItem("serial", "Read and print serial number and variant");
  helpItem("variant", "Read and print current variant");
  helpItem("toffset [degC]", "Show or set temperature offset");
  helpItem("altitude [m]", "Show or set sensor altitude");
  helpItem("pressure [Pa]", "Show or set ambient pressure compensation");
  helpItem("asc_enabled [0|1]", "Show or set automatic self-calibration enable");
  helpItem("asc_target [ppm]", "Show or set ASC target");
  helpItem("asc_initial [hours]", "Show or set ASC initial period");
  helpItem("asc_standard [hours]", "Show or set ASC standard period");

  helpSection("Maintenance");
  helpItem("persist", "Persist EEPROM-backed settings");
  helpItem("reinit", "Reload persisted settings into RAM");
  helpItem("factory_reset", "Perform factory reset");
  helpItem("selftest", "Run the 10 s self-test");
  helpItem("frc <reference_ppm>", "Start forced recalibration");
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
    const app_driver::Status st = device.begin(gConfig);
    gLastPending = device.pendingCommand();
    printStatus(st);
    return;
  }

  if (head == "end") {
    cancelQueuedWork();
    device.end();
    gLastPending = device.pendingCommand();
    Serial.println("driver ended");
    return;
  }

  if (head == "probe") {
    printStatus(device.probe());
    return;
  }

  if (head == "recover") {
    printStatus(device.recover());
    return;
  }

  if (head == "drv") {
    printDriverView();
    return;
  }

  if (head == "drv1") {
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

  if (head == "read") {
    printStatus(scheduleMeasurement());
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
    Serial.printf("data_ready=%s raw=0x%04X\n",
                  yesNo(ready.ready),
                  static_cast<unsigned>(ready.raw));
    return;
  }

  if (head == "watch") {
    if (tail.length() == 0U) {
      Serial.printf("watch=%s\n", yesNo(gWatchEnabled));
      return;
    }

    bool enabled = false;
    if (!cmd::parseBool01(tail, enabled)) {
      LOGW("Expected watch 0|1");
      return;
    }

    gWatchEnabled = enabled;
    Serial.printf("watch=%s\n", yesNo(gWatchEnabled));
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
    if (tail.length() == 0U) {
      Serial.printf("verbose=%s\n", yesNo(gVerbose));
      return;
    }

    bool enabled = false;
    if (!cmd::parseBool01(tail, enabled)) {
      LOGW("Expected verbose 0|1");
      return;
    }
    gVerbose = enabled;
    Serial.printf("verbose=%s\n", yesNo(gVerbose));
    return;
  }

  if (head == "stress") {
    uint32_t count = 10;
    if (tail.length() > 0U && !cmd::parseU32(tail, count)) {
      LOGW("Expected stress [count]");
      return;
    }
    if (count == 0U) {
      count = 1U;
    }
    gStressRemaining = static_cast<int>(count);
    Serial.printf("stress=%d\n", gStressRemaining);
    printStatus(scheduleMeasurement());
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
      printStatus(device.stopPeriodicMeasurement());
      return;
    }
    LOGW("Usage: periodic [on|lp|off]");
    return;
  }

  if (head == "sleep") {
    printStatus(device.powerDown());
    return;
  }

  if (head == "wake") {
    printStatus(device.wakeUp());
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
    printStatus(device.startPersistSettings());
    return;
  }

  if (head == "reinit") {
    printStatus(device.startReinit());
    return;
  }

  if (head == "factory_reset") {
    printStatus(device.startFactoryReset());
    return;
  }

  if (head == "selftest") {
    printStatus(device.startSelfTest());
    return;
  }

  if (head == "frc") {
    uint16_t referencePpm = 0;
    if (!cmd::parseU16(tail, referencePpm)) {
      LOGW("Usage: frc <reference_ppm>");
      return;
    }
    printStatus(device.startForcedRecalibration(referencePpm));
    return;
  }

  if (head == "convert") {
    processConvert(tail);
    return;
  }

  Serial.printf("unknown command: %s\n", head.c_str());
  printHelp();
}

}  // namespace

void setup() {
  log_begin(115200);
  delay(100);
  LOGI("=== SCD41 Bringup Example ===");

  if (!board::initI2c()) {
    LOGE("Wire init failed");
    return;
  }

  gConfig.i2cWrite = transport::wireWrite;
  gConfig.i2cWriteRead = transport::wireWriteRead;
  gConfig.i2cUser = &Wire;
  gConfig.i2cAddress = SCD41::cmd::I2C_ADDRESS;
  gConfig.transportCapabilities = app_driver::TransportCapability::NONE;
  gConfig.strictVariantCheck = true;

  const app_driver::Status st = device.begin(gConfig);
  gLastPending = device.pendingCommand();
  printStatus(st);
  printHelp();
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
  }
}
