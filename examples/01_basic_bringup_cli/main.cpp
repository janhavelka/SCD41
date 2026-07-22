#include <Arduino.h>
#include <Wire.h>

#include "common/BoardConfig.h"
#include "common/BusDiag.h"
#include "common/CliShell.h"
#include "common/CliStyle.h"
#include "common/CommandHandler.h"
#include "common/DriverCompat.h"
#include "common/I2cTransport.h"
#include "common/Log.h"

namespace {

app_driver::Device device;
app_driver::Config config;
app_driver::OperationResult lastResult;
bool lastResultValid = false;
uint32_t nextRequestId = 1;

uint32_t operationBudgetMs(app_driver::OperationKind kind) {
  const SCD41::OperationLimits limits = app_driver::Device::limits(kind);
  return limits.maxWaitMs +
         static_cast<uint32_t>(limits.maxCallbacks) * config.transferTimeoutMs +
         1000U;
}

void printStatus(const app_driver::Status& status) {
  LOG_SERIAL.printf("status=%s detail=%ld msg=%s\n",
                    app_driver::errToString(status.code),
                    static_cast<long>(status.detail), status.msg);
}

void printSample(const app_driver::FixedSample& sample) {
  LOG_SERIAL.printf(
      "sample seq=%lu epoch=%lu mode=%s co2=%u temp_mC=%ld rh_mPct=%lu flags=0x%04X at=%lu\n",
      static_cast<unsigned long>(sample.sequence),
      static_cast<unsigned long>(sample.sensorEpoch),
      app_driver::modeToString(sample.mode), static_cast<unsigned>(sample.co2Ppm),
      static_cast<long>(sample.temperatureMilliC),
      static_cast<unsigned long>(sample.humidityMilliPercent),
      static_cast<unsigned>(sample.flags),
      static_cast<unsigned long>(sample.capturedAtMs));
}

void printConfiguration(const app_driver::ConfigurationSnapshot& snapshot) {
  LOG_SERIAL.printf(
      "config offset_mC=%ld altitude_m=%u pressure_Pa=%lu asc=%s target_ppm=%u initial_h=%u standard_h=%u verified=0x%04X dirty=0x%04X persistence_indeterminate=%s\n",
      static_cast<long>(snapshot.temperatureOffsetMilliC),
      static_cast<unsigned>(snapshot.sensorAltitudeM),
      static_cast<unsigned long>(snapshot.ambientPressurePa),
      snapshot.ascEnabled ? "on" : "off",
      static_cast<unsigned>(snapshot.ascTargetPpm),
      static_cast<unsigned>(snapshot.ascInitialPeriodHours),
      static_cast<unsigned>(snapshot.ascStandardPeriodHours),
      static_cast<unsigned>(snapshot.verifiedMask),
      static_cast<unsigned>(snapshot.dirtyMask),
      snapshot.persistenceIndeterminate ? "yes" : "no");
}

void printResult(const app_driver::OperationResult& result) {
  LOG_SERIAL.printf(
      "result request=%lu generation=%lu op=%s outcome=%s effect=%s status=%s callbacks=%u reconcile=%s\n",
      static_cast<unsigned long>(result.id.requestId),
      static_cast<unsigned long>(result.id.generation),
      app_driver::operationToString(result.kind),
      app_driver::outcomeToString(result.outcome),
      app_driver::effectToString(result.effect),
      app_driver::errToString(result.status.code),
      static_cast<unsigned>(result.callbacksUsed),
      result.reconciliationRequired ? "yes" : "no");

  switch (result.kind) {
    case app_driver::OperationKind::ATTACH:
    case app_driver::OperationKind::READ_IDENTITY:
    case app_driver::OperationKind::WAKE_UP:
    case app_driver::OperationKind::REINIT:
    case app_driver::OperationKind::FACTORY_RESET:
      LOG_SERIAL.printf("identity valid=%s serial=0x%012llX variant=%s epoch=%lu\n",
                        result.value.identity.valid ? "yes" : "no",
                        static_cast<unsigned long long>(result.value.identity.serialNumber),
                        app_driver::variantToString(result.value.identity.variant),
                        static_cast<unsigned long>(result.value.identity.sensorEpoch));
      break;
    case app_driver::OperationKind::FETCH_SAMPLE:
    case app_driver::OperationKind::SINGLE_SHOT:
    case app_driver::OperationKind::SINGLE_SHOT_RHT_ONLY:
      if ((result.value.sample.flags & SCD41::SAMPLE_FRESH) != 0U) {
        printSample(result.value.sample);
      }
      break;
    case app_driver::OperationKind::READ_TEMPERATURE_OFFSET:
    case app_driver::OperationKind::SET_TEMPERATURE_OFFSET:
      LOG_SERIAL.printf("value signed=%ld\n",
                        static_cast<long>(result.value.signedValue));
      break;
    case app_driver::OperationKind::READ_ASC_ENABLED:
    case app_driver::OperationKind::SET_ASC_ENABLED:
      LOG_SERIAL.printf("value bool=%s\n",
                        result.value.boolValue ? "true" : "false");
      break;
    case app_driver::OperationKind::READ_SENSOR_ALTITUDE:
    case app_driver::OperationKind::SET_SENSOR_ALTITUDE:
    case app_driver::OperationKind::READ_AMBIENT_PRESSURE:
    case app_driver::OperationKind::SET_AMBIENT_PRESSURE:
    case app_driver::OperationKind::READ_ASC_TARGET:
    case app_driver::OperationKind::SET_ASC_TARGET:
    case app_driver::OperationKind::READ_ASC_INITIAL_PERIOD:
    case app_driver::OperationKind::SET_ASC_INITIAL_PERIOD:
    case app_driver::OperationKind::READ_ASC_STANDARD_PERIOD:
    case app_driver::OperationKind::SET_ASC_STANDARD_PERIOD:
      LOG_SERIAL.printf("value unsigned=%lu\n",
                        static_cast<unsigned long>(result.value.value));
      break;
    case app_driver::OperationKind::SELF_TEST:
      LOG_SERIAL.printf("selftest raw=0x%04X\n",
                        static_cast<unsigned>(result.value.rawWords[0]));
      break;
    case app_driver::OperationKind::FORCED_RECALIBRATION:
      LOG_SERIAL.printf("frc correction_ppm=%ld raw=0x%04X\n",
                        static_cast<long>(result.value.signedValue),
                        static_cast<unsigned>(result.value.rawWords[0]));
      break;
    case app_driver::OperationKind::READ_CONFIGURATION:
      printConfiguration(result.value.configuration);
      break;
    case app_driver::OperationKind::READ_DATA_READY:
      LOG_SERIAL.printf("data_ready=%s raw=0x%04X\n",
                        result.value.dataReady.ready ? "yes" : "no",
                        static_cast<unsigned>(result.value.dataReady.raw));
      break;
    case app_driver::OperationKind::DIAGNOSTIC_READ_WORDS:
      for (uint8_t i = 0; i < result.value.wordCount; ++i) {
        LOG_SERIAL.printf("word[%u]=0x%04X\n", static_cast<unsigned>(i),
                          static_cast<unsigned>(result.value.rawWords[i]));
      }
      break;
    default:
      break;
  }
}

void takeAndPrint(const app_driver::OperationId& id) {
  app_driver::OperationResult result;
  const app_driver::Status status = device.takeResult(id, result);
  if (!status.ok()) {
    printStatus(status);
    return;
  }
  lastResult = result;
  lastResultValid = true;
  printResult(result);
}

void pollDriver() {
  const app_driver::PollResult poll = device.poll(millis(), 1U);
  if (poll.state == app_driver::OperationState::RESULT_PENDING) {
    takeAndPrint(poll.id);
  } else if (!poll.status.ok() && !poll.status.inProgress()) {
    printStatus(poll.status);
  }
}

bool bindDriver() {
  config = app_driver::Config{};
  config.transfer = transport::wireTransfer;
  config.transferUser = &Wire;
  config.transferTimeoutMs = board::I2C_TIMEOUT_MS;
  const app_driver::Status status = device.begin(config);
  printStatus(status);
  return status.ok();
}

void startOperation(const app_driver::OperationRequest& request) {
  const uint32_t now = millis();
  app_driver::OperationOptions options;
  options.requestId = nextRequestId++;
  if (nextRequestId == 0U) {
    nextRequestId = 1U;
  }
  options.nowMs = now;
  options.deadlineMs = now + operationBudgetMs(request.kind);
  app_driver::OperationId id;
  const app_driver::Status status = device.start(request, options, id);
  printStatus(status);
  if (status.inProgress()) {
    LOG_SERIAL.printf("started request=%lu generation=%lu op=%s deadline=%lu\n",
                      static_cast<unsigned long>(id.requestId),
                      static_cast<unsigned long>(id.generation),
                      app_driver::operationToString(request.kind),
                      static_cast<unsigned long>(options.deadlineMs));
  }
}

void printRuntime() {
  const app_driver::RuntimeSnapshot runtime = device.runtimeSnapshot();
  const app_driver::HealthSnapshot health = device.healthSnapshot();
  LOG_SERIAL.printf(
      "runtime bound=%s attached=%s state=%s mode=%s operation=%s next_due=%lu reconcile=%s sample=%s\n",
      runtime.bound ? "yes" : "no", runtime.attached ? "yes" : "no",
      app_driver::stateToString(runtime.driverState),
      app_driver::modeToString(runtime.operatingMode),
      app_driver::operationToString(runtime.operationKind),
      static_cast<unsigned long>(runtime.nextDueMs),
      runtime.reconciliationRequired ? "yes" : "no",
      runtime.sampleAvailable ? "yes" : "no");
  LOG_SERIAL.printf(
      "health transfer_ok=%lu transfer_fail=%lu expected_nack=%lu protocol_fail=%lu operation_ok=%lu operation_fail=%lu cancelled=%lu\n",
      static_cast<unsigned long>(health.totalTransferSuccess),
      static_cast<unsigned long>(health.totalTransferFailures),
      static_cast<unsigned long>(health.expectedNacks),
      static_cast<unsigned long>(health.totalProtocolFailures),
      static_cast<unsigned long>(health.totalOperationSuccess),
      static_cast<unsigned long>(health.totalOperationFailures),
      static_cast<unsigned long>(health.totalOperationCancelled));
}

void printHelp() {
  cli::printHelpHeader("SCD41 Owner-Safe CLI v1");
  cli::printHelpSection("Lifecycle");
  cli::printHelpItem("help/?", "Show this help");
  cli::printHelpItem("version", "Show library version");
  cli::printHelpItem("scan", "Scan the example-owned I2C bus while idle");
  cli::printHelpItem("begin", "Zero-I2C bind, then start attach");
  cli::printHelpItem("end", "Cancel active work and unbind without I2C");
  cli::printHelpItem("status", "Show cache-only runtime and health");
  cli::printHelpItem("result", "Show the example's last consumed result");
  cli::printHelpItem("cancel", "Cancel the active operation without I2C");
  cli::printHelpSection("Measurement");
  cli::printHelpItem("identity", "Read serial number and variant");
  cli::printHelpItem("periodic on|lp|off", "Change periodic mode");
  cli::printHelpItem("dataready", "Read data-ready status");
  cli::printHelpItem("read", "Fetch one periodic sample");
  cli::printHelpItem("single full|rht", "Run one bounded single-shot job");
  cli::printHelpItem("sample", "Show the latest cached fixed-point sample");
  cli::printHelpItem("settings", "Read all live configuration fields");
  cli::printHelpItem("sleep", "Enter sensor power-down mode");
  cli::printHelpItem("wake", "Wake and verify the sensor");
  cli::printHelpSection("Configuration");
  cli::printHelpItem("toffset [mC]", "Read or set temperature offset");
  cli::printHelpItem("altitude [m]", "Read or set sensor altitude");
  cli::printHelpItem("pressure [Pa]", "Read or set ambient pressure");
  cli::printHelpItem("asc_enabled [0|1]", "Read or set ASC enable");
  cli::printHelpItem("asc_target [ppm]", "Read or set ASC target");
  cli::printHelpItem("asc_initial [h]", "Read or set ASC initial period");
  cli::printHelpItem("asc_standard [h]", "Read or set ASC standard period");
  cli::printHelpSection("Maintenance");
  cli::printHelpItem("reinit", "Reload persisted sensor settings");
  cli::printHelpItem("selftest", "Run the 10-second sensor self-test");
  cli::printHelpItem("frc confirm <reference_ppm>", "Run forced recalibration");
  cli::printHelpItem("persist confirm", "Write current settings to EEPROM");
  cli::printHelpItem("factory_reset confirm", "Reset persisted sensor state");
  cli::printHelpSection("Diagnostics");
  cli::printHelpItem("command read_words <cmd> <count>", "CRC-check 1..3 words; then reattach");
}

bool parseOptionalU32(const String& text, uint32_t& value) {
  return text.length() > 0U && cmd::parseU32(text, value);
}

void processCommand(const String& line) {
  String head;
  String tail;
  if (!cmd::splitHeadTail(line, head, tail)) {
    return;
  }

  if (head == "help" || head == "?") {
    printHelp();
  } else if (head == "version") {
    LOG_SERIAL.printf("SCD41 version=%s\n", SCD41::VERSION);
  } else if (head == "scan") {
    if (device.operationState() != app_driver::OperationState::IDLE) {
      printStatus(app_driver::Status::Error(app_driver::Err::BUSY, "Operation active"));
    } else {
      (void)bus_diag::scan();
    }
  } else if (head == "begin") {
    const app_driver::RuntimeSnapshot before = device.runtimeSnapshot();
    device.end();
    if (before.operationState != app_driver::OperationState::IDLE) {
      takeAndPrint(before.operationId);
    }
    if (bindDriver()) {
      startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::ATTACH));
    }
  } else if (head == "end") {
    const app_driver::RuntimeSnapshot before = device.runtimeSnapshot();
    device.end();
    if (before.operationState != app_driver::OperationState::IDLE) {
      takeAndPrint(before.operationId);
    }
  } else if (head == "status") {
    printRuntime();
  } else if (head == "result") {
    if (lastResultValid) printResult(lastResult); else LOG_SERIAL.println("result=none");
  } else if (head == "cancel") {
    const app_driver::RuntimeSnapshot runtime = device.runtimeSnapshot();
    const app_driver::Status status = device.cancel(runtime.operationId, millis());
    printStatus(status);
    if (status.ok()) takeAndPrint(runtime.operationId);
  } else if (head == "identity") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::READ_IDENTITY));
  } else if (head == "periodic") {
    if (tail == "on") startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::START_PERIODIC));
    else if (tail == "lp") startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::START_LOW_POWER_PERIODIC));
    else if (tail == "off") startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::STOP_PERIODIC));
    else LOGW("Usage: periodic on|lp|off");
  } else if (head == "dataready") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::READ_DATA_READY));
  } else if (head == "read") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::FETCH_SAMPLE));
  } else if (head == "single") {
    const app_driver::OperationKind kind = tail == "rht"
        ? app_driver::OperationKind::SINGLE_SHOT_RHT_ONLY
        : app_driver::OperationKind::SINGLE_SHOT;
    if (tail == "full" || tail == "rht") startOperation(app_driver::OperationRequest::make(kind));
    else LOGW("Usage: single full|rht");
  } else if (head == "sample") {
    app_driver::FixedSample sample;
    const app_driver::Status status = device.peekLatestSample(sample);
    if (status.ok()) printSample(sample); else printStatus(status);
  } else if (head == "settings") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::READ_CONFIGURATION));
  } else if (head == "sleep") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::POWER_DOWN));
  } else if (head == "wake") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::WAKE_UP));
  } else if (head == "toffset") {
    int32_t value = 0;
    if (tail.length() == 0U) startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::READ_TEMPERATURE_OFFSET));
    else if (cmd::parseInt32(tail, value)) startOperation(app_driver::OperationRequest::setTemperatureOffsetMilliC(value));
    else LOGW("Usage: toffset [mC]");
  } else if (head == "altitude" || head == "pressure" || head == "asc_target" || head == "asc_initial" || head == "asc_standard") {
    uint32_t value = 0;
    const bool hasValue = parseOptionalU32(tail, value);
    if (tail.length() > 0U && !hasValue) { LOGW("Expected an unsigned integer"); return; }
    if (hasValue && head != "pressure" && value > 65535U) {
      LOGW("Value must fit in 16 bits");
      return;
    }
    if (head == "altitude") startOperation(hasValue ? app_driver::OperationRequest::setSensorAltitudeM(static_cast<uint16_t>(value)) : app_driver::OperationRequest::make(app_driver::OperationKind::READ_SENSOR_ALTITUDE));
    else if (head == "pressure") startOperation(hasValue ? app_driver::OperationRequest::setAmbientPressurePa(value) : app_driver::OperationRequest::make(app_driver::OperationKind::READ_AMBIENT_PRESSURE));
    else if (head == "asc_target") startOperation(hasValue ? app_driver::OperationRequest::setAscTargetPpm(static_cast<uint16_t>(value)) : app_driver::OperationRequest::make(app_driver::OperationKind::READ_ASC_TARGET));
    else if (head == "asc_initial") startOperation(hasValue ? app_driver::OperationRequest::setAscInitialPeriodHours(static_cast<uint16_t>(value)) : app_driver::OperationRequest::make(app_driver::OperationKind::READ_ASC_INITIAL_PERIOD));
    else startOperation(hasValue ? app_driver::OperationRequest::setAscStandardPeriodHours(static_cast<uint16_t>(value)) : app_driver::OperationRequest::make(app_driver::OperationKind::READ_ASC_STANDARD_PERIOD));
  } else if (head == "asc_enabled") {
    bool enabled = false;
    if (tail.length() == 0U) startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::READ_ASC_ENABLED));
    else if (cmd::parseBool01(tail, enabled)) startOperation(app_driver::OperationRequest::setAscEnabled(enabled));
    else LOGW("Usage: asc_enabled [0|1]");
  } else if (head == "reinit") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::REINIT));
  } else if (head == "selftest") {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::SELF_TEST));
  } else if (head == "frc") {
    String confirm;
    String reference;
    uint16_t ppm = 0;
    if (!cmd::splitHeadTail(tail, confirm, reference) || confirm != "confirm" || !cmd::parseU16(reference, ppm)) {
      LOGW("use 'frc confirm <reference_ppm>' to update calibration history");
    } else startOperation(app_driver::OperationRequest::forcedRecalibration(ppm));
  } else if (head == "persist") {
    if (tail != "confirm") LOGW("use 'persist confirm' to write EEPROM");
    else startOperation(app_driver::OperationRequest::persistSettings());
  } else if (head == "factory_reset") {
    if (tail != "confirm") LOGW("use 'factory_reset confirm' to erase/reset settings");
    else startOperation(app_driver::OperationRequest::factoryReset());
  } else if (head == "command") {
    String sub;
    String args;
    String commandText;
    String countText;
    uint16_t command = 0;
    uint32_t count = 0;
    if (!cmd::splitHeadTail(tail, sub, args) || sub != "read_words" ||
        !cmd::splitHeadTail(args, commandText, countText) ||
        !cmd::parseU16(commandText, command) || !cmd::parseU32(countText, count) ||
        count == 0U || count > 3U) {
      LOGW("Usage: command read_words <cmd> <count>");
    } else startOperation(app_driver::OperationRequest::diagnosticReadWords(command, static_cast<uint8_t>(count)));
  } else {
    cli::printUnknownCommand(head.c_str());
  }
}

}  // namespace

void setup() {
  log_begin(115200);
  delay(50);
  LOGI("SCD41 owner-safe bring-up CLI");
  if (!board::initI2c()) {
    LOGE("I2C initialization failed");
  } else if (bindDriver()) {
    startOperation(app_driver::OperationRequest::make(app_driver::OperationKind::ATTACH));
  }
  printHelp();
  cli::printPrompt();
}

void loop() {
  pollDriver();
  String line;
  if (cli_shell::readLine(line)) {
    processCommand(line);
    cli::printPrompt();
  }
  delay(1);
}
