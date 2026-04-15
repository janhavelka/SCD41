/// @file main.cpp
/// @brief Basic bringup CLI example for SCD41
/// @note This is an EXAMPLE, not part of the library

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "common/BoardConfig.h"
#include "common/BusDiag.h"
#include "common/CliShell.h"
#include "common/DriverCompat.h"
#include "common/HealthView.h"
#include "common/I2cTransport.h"
#include "common/Log.h"

app_driver::Device device;
app_driver::Config gConfig;
bool gVerbose = false;
bool gPendingRead = false;
int gStressRemaining = 0;

const char* yesNo(bool value) {
  return value ? "yes" : "no";
}

void printStatus(const app_driver::Status& st) {
  Serial.printf("status=%s code=%u detail=%ld\n",
                app_driver::errToString(st.code),
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail));
  if (st.msg != nullptr && st.msg[0] != '\0') {
    Serial.printf("msg=%s\n", st.msg);
  }
}

void printMeasurement(const app_driver::Measurement& m) {
  Serial.printf("CO2=%u ppm (%s) T=%.3f C RH=%.3f %%\n",
                static_cast<unsigned>(m.co2Ppm),
                yesNo(m.co2Valid),
                static_cast<double>(m.temperatureC),
                static_cast<double>(m.humidityPct));
}

void printDriverView() {
  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  printHealthView(device);
  Serial.printf("mode=%s pending=%s busy=%s measurementPending=%s measurementReady=%s sampleAgeMs=%lu\n",
                app_driver::modeToString(snap.operatingMode),
                app_driver::pendingToString(snap.pendingCommand),
                yesNo(snap.busy),
                yesNo(snap.measurementPending),
                yesNo(snap.measurementReady),
                static_cast<unsigned long>(device.sampleAgeMs(millis())));
}

void printSettings() {
  app_driver::SettingsSnapshot snap;
  const app_driver::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.printf("addr=0x%02X timeoutMs=%lu singleShot=%u variant=%s serialValid=%s serial=0x%012llX\n",
                snap.i2cAddress,
                static_cast<unsigned long>(snap.i2cTimeoutMs),
                static_cast<unsigned>(snap.singleShotMode),
                app_driver::variantToString(snap.sensorVariant),
                yesNo(snap.serialNumberValid),
                static_cast<unsigned long long>(snap.serialNumber));
}

app_driver::Status scheduleMeasurement() {
  const app_driver::Status st = device.requestMeasurement();
  if (st.inProgress()) {
    gPendingRead = true;
  }
  return st;
}

void handleMeasurementReady() {
  if (!gPendingRead || !device.measurementReady()) {
    return;
  }

  app_driver::Measurement measurement;
  const app_driver::Status st = device.getMeasurement(measurement);
  if (!st.ok()) {
    printStatus(st);
    gPendingRead = false;
    gStressRemaining = 0;
    return;
  }

  printMeasurement(measurement);
  gPendingRead = false;

  if (gStressRemaining > 0) {
    gStressRemaining--;
    if (gStressRemaining > 0) {
      const app_driver::Status next = scheduleMeasurement();
      if (!next.ok() && !next.inProgress()) {
        printStatus(next);
        gStressRemaining = 0;
      }
    } else {
      Serial.println("stress done");
    }
  }
}

int parseTrailingInt(const char* text, int defaultValue) {
  const char* space = strchr(text, ' ');
  if (space == nullptr) {
    return defaultValue;
  }
  const int value = atoi(space + 1);
  return value > 0 ? value : defaultValue;
}

void printHelp() {
  Serial.println("commands:");
  Serial.println("  help");
  Serial.println("  scan");
  Serial.println("  probe");
  Serial.println("  recover");
  Serial.println("  drv");
  Serial.println("  cfg");
  Serial.println("  settings");
  Serial.println("  read");
  Serial.println("  verbose on|off");
  Serial.println("  stress [count]");
  Serial.println("  periodic on|lp|off");
  Serial.println("  single full|rht");
  Serial.println("  sleep");
  Serial.println("  wake");
  Serial.println("  serial");
}

void handleCommand(const String& line) {
  const char* text = line.c_str();

  if (strcmp(text, "help") == 0) {
    printHelp();
    return;
  }
  if (strcmp(text, "scan") == 0) {
    bus_diag::scan(SCD41::cmd::I2C_ADDRESS);
    return;
  }
  if (strcmp(text, "probe") == 0) {
    printStatus(device.probe());
    return;
  }
  if (strcmp(text, "recover") == 0) {
    printStatus(device.recover());
    return;
  }
  if (strcmp(text, "drv") == 0) {
    printDriverView();
    return;
  }
  if (strcmp(text, "cfg") == 0 || strcmp(text, "settings") == 0) {
    printSettings();
    return;
  }
  if (strcmp(text, "read") == 0) {
    printStatus(scheduleMeasurement());
    return;
  }
  if (strncmp(text, "verbose", 7) == 0) {
    gVerbose = strstr(text, "off") == nullptr;
    Serial.printf("verbose=%s\n", yesNo(gVerbose));
    return;
  }
  if (strncmp(text, "stress", 6) == 0) {
    gStressRemaining = parseTrailingInt(text, 10);
    Serial.printf("stress=%d\n", gStressRemaining);
    printStatus(scheduleMeasurement());
    return;
  }
  if (strcmp(text, "periodic on") == 0 || strcmp(text, "periodic") == 0) {
    printStatus(device.startPeriodicMeasurement());
    return;
  }
  if (strcmp(text, "periodic lp") == 0) {
    printStatus(device.startLowPowerPeriodicMeasurement());
    return;
  }
  if (strcmp(text, "periodic off") == 0 || strcmp(text, "stop") == 0) {
    printStatus(device.stopPeriodicMeasurement());
    return;
  }
  if (strcmp(text, "single full") == 0) {
    printStatus(device.setSingleShotMode(app_driver::SingleShotMode::CO2_T_RH));
    return;
  }
  if (strcmp(text, "single rht") == 0) {
    printStatus(device.setSingleShotMode(app_driver::SingleShotMode::T_RH_ONLY));
    return;
  }
  if (strcmp(text, "sleep") == 0) {
    printStatus(device.powerDown());
    return;
  }
  if (strcmp(text, "wake") == 0) {
    printStatus(device.wakeUp());
    return;
  }
  if (strcmp(text, "serial") == 0) {
    uint64_t serial = 0;
    app_driver::SensorVariant variant = app_driver::SensorVariant::UNKNOWN;
    printStatus(device.readSerialNumber(serial));
    if (device.readSensorVariant(variant).ok()) {
      Serial.printf("variant=%s serial=0x%012llX\n",
                    app_driver::variantToString(variant),
                    static_cast<unsigned long long>(serial));
    }
    return;
  }

  Serial.printf("unknown command: %s\n", text);
  printHelp();
}

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
  printStatus(st);
  printHelp();
}

void loop() {
  device.tick(millis());
  handleMeasurementReady();

  if (gVerbose && gPendingRead) {
    static uint32_t lastLogMs = 0;
    const uint32_t now = millis();
    if ((now - lastLogMs) >= 1000U) {
      lastLogMs = now;
      Serial.printf("pending=%s ready=%s\n",
                    app_driver::pendingToString(device.pendingCommand()),
                    yesNo(device.measurementReady()));
    }
  }

  String line;
  if (cli_shell::readLine(line)) {
    handleCommand(line);
  }
}
