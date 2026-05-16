/// @file test_basic.cpp
/// @brief Unit tests for the SCD41 driver and example transport

#include <unity.h>
#include <math.h>

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;
uint32_t gMillis = 0;
uint32_t gMicros = 0;
uint32_t gMillisStep = 0;
uint32_t gMicrosStep = 0;

#define private public
#include "SCD41/SCD41.h"
#undef private

#include "examples/common/I2cTransport.h"

using namespace SCD41;
using SCD41Device = SCD41::SCD41;

namespace {

struct ScriptedTransport {
  Status writeStatus[32] = {};
  size_t writeCalls = 0;
  size_t writeCount = 0;
  size_t writeIndex = 0;

  Status readStatus[32] = {};
  uint8_t readData[32][9] = {};
  size_t readLen[32] = {};
  size_t readCalls = 0;
  size_t readCount = 0;
  size_t readIndex = 0;

  uint8_t lastWrite[16] = {};
  size_t lastWriteLen = 0;

  uint32_t nowMs = 0;
  uint32_t nowUs = 0;
  uint32_t yieldCount = 0;
  bool advanceOnYield = true;
};

uint8_t packWord(uint16_t word, uint8_t* out) {
  out[0] = static_cast<uint8_t>((word >> 8) & 0xFF);
  out[1] = static_cast<uint8_t>(word & 0xFF);
  out[2] = SCD41Device::_crc8(out, 2);
  return out[2];
}

void queueReadWord(ScriptedTransport& bus, uint16_t word) {
  TEST_ASSERT_TRUE(bus.readCount < 32);
  bus.readStatus[bus.readCount] = Status::Ok();
  bus.readLen[bus.readCount] = 3;
  packWord(word, bus.readData[bus.readCount]);
  ++bus.readCount;
}

void queueReadBytes(ScriptedTransport& bus, const uint8_t* data, size_t len) {
  TEST_ASSERT_TRUE(bus.readCount < 32);
  TEST_ASSERT_TRUE(len <= sizeof(bus.readData[bus.readCount]));
  bus.readStatus[bus.readCount] = Status::Ok();
  bus.readLen[bus.readCount] = len;
  memcpy(bus.readData[bus.readCount], data, len);
  ++bus.readCount;
}

void queueReadStatus(ScriptedTransport& bus, const Status& status, size_t len = 0) {
  TEST_ASSERT_TRUE(bus.readCount < 32);
  bus.readStatus[bus.readCount] = status;
  bus.readLen[bus.readCount] = len;
  ++bus.readCount;
}

void queueReadMeasurement(ScriptedTransport& bus, uint16_t co2, uint16_t temp,
                          uint16_t humidity) {
  TEST_ASSERT_TRUE(bus.readCount < 32);
  bus.readStatus[bus.readCount] = Status::Ok();
  bus.readLen[bus.readCount] = 9;
  packWord(co2, &bus.readData[bus.readCount][0]);
  packWord(temp, &bus.readData[bus.readCount][3]);
  packWord(humidity, &bus.readData[bus.readCount][6]);
  ++bus.readCount;
}

void queueReadSerial(ScriptedTransport& bus, uint16_t word0, uint16_t word1, uint16_t word2) {
  queueReadMeasurement(bus, word0, word1, word2);
}

Status scriptedWrite(uint8_t addr, const uint8_t* data, size_t len, uint32_t timeoutMs,
                     void* user) {
  (void)timeoutMs;
  auto* ctx = static_cast<ScriptedTransport*>(user);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDRESS, addr);
  ++ctx->writeCalls;
  ctx->lastWriteLen = len;
  memset(ctx->lastWrite, 0, sizeof(ctx->lastWrite));
  memcpy(ctx->lastWrite, data, len);
  if (ctx->writeIndex < ctx->writeCount) {
    return ctx->writeStatus[ctx->writeIndex++];
  }
  return Status::Ok();
}

Status scriptedWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                         uint8_t* rxData, size_t rxLen, uint32_t timeoutMs, void* user) {
  (void)txData;
  (void)timeoutMs;
  auto* ctx = static_cast<ScriptedTransport*>(user);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDRESS, addr);
  TEST_ASSERT_EQUAL_UINT32(0u, txLen);
  ++ctx->readCalls;
  if (ctx->readIndex < ctx->readCount) {
    const Status st = ctx->readStatus[ctx->readIndex];
    if (st.ok()) {
      TEST_ASSERT_EQUAL_UINT32(ctx->readLen[ctx->readIndex], rxLen);
      memcpy(rxData, ctx->readData[ctx->readIndex], rxLen);
    }
    ++ctx->readIndex;
    return st;
  }
  return Status::Ok();
}

uint32_t scriptedNowMs(void* user) {
  return static_cast<ScriptedTransport*>(user)->nowMs;
}

uint32_t scriptedNowUs(void* user) {
  return static_cast<ScriptedTransport*>(user)->nowUs;
}

void scriptedYield(void* user) {
  auto* ctx = static_cast<ScriptedTransport*>(user);
  ++ctx->yieldCount;
  if (ctx->advanceOnYield) {
    ++ctx->nowMs;
    ctx->nowUs += 1000;
  }
}

Config makeConfig(ScriptedTransport& bus) {
  Config cfg;
  cfg.i2cWrite = scriptedWrite;
  cfg.i2cWriteRead = scriptedWriteRead;
  cfg.i2cUser = &bus;
  cfg.nowMs = scriptedNowMs;
  cfg.nowUs = scriptedNowUs;
  cfg.cooperativeYield = scriptedYield;
  cfg.timeUser = &bus;
  cfg.offlineThreshold = 3;
  cfg.powerUpDelayMs = 0;
  return cfg;
}

void queueBeginSuccess(ScriptedTransport& bus) {
  queueReadSerial(bus, 0x1001, 0x2345, 0x6789);
}

uint16_t lastWriteCommand(const ScriptedTransport& bus) {
  return static_cast<uint16_t>((static_cast<uint16_t>(bus.lastWrite[0]) << 8) | bus.lastWrite[1]);
}

uint16_t lastWriteWord(const ScriptedTransport& bus) {
  return static_cast<uint16_t>((static_cast<uint16_t>(bus.lastWrite[2]) << 8) | bus.lastWrite[3]);
}

}  // namespace

void setUp() {
  gMillis = 0;
  gMicros = 0;
  gMillisStep = 0;
  gMicrosStep = 0;
  Wire._clearRequestFromOverride();
  Wire._clearReadCallCount();
  Wire._clearClockSetCount();
}

void tearDown() {}

void test_status_helpers() {
  const Status ok = Status::Ok();
  TEST_ASSERT_TRUE(ok.ok());
  TEST_ASSERT_TRUE(ok.is(Err::OK));
  TEST_ASSERT_TRUE(ok);

  const Status pending{Err::IN_PROGRESS, 0, "pending"};
  TEST_ASSERT_FALSE(pending.ok());
  TEST_ASSERT_TRUE(pending.inProgress());

  const Status err = Status::Error(Err::CRC_MISMATCH, "crc");
  TEST_ASSERT_FALSE(err.ok());
  TEST_ASSERT_TRUE(err.is(Err::CRC_MISMATCH));
}

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_EQUAL(nullptr, cfg.i2cWrite);
  TEST_ASSERT_EQUAL(nullptr, cfg.i2cWriteRead);
  TEST_ASSERT_EQUAL_HEX8(0x62, cfg.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, cfg.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(1u, cfg.commandDelayMs);
  TEST_ASSERT_EQUAL_UINT32(30u, cfg.powerUpDelayMs);
  TEST_ASSERT_EQUAL_UINT32(250u, cfg.dataReadyRetryMs);
  TEST_ASSERT_EQUAL_UINT32(100u, cfg.recoverBackoffMs);
  TEST_ASSERT_EQUAL_UINT8(5u, cfg.offlineThreshold);
  TEST_ASSERT_TRUE(cfg.strictVariantCheck);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SingleShotMode::CO2_T_RH),
                    static_cast<uint8_t>(cfg.singleShotMode));
}

void test_crc_and_conversion_helpers() {
  const uint8_t data[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, SCD41Device::_crc8(data, 2));

  TEST_ASSERT_EQUAL_INT(-45000, SCD41Device::convertTemperatureC_x1000(0));
  TEST_ASSERT_EQUAL_UINT32(0u, SCD41Device::convertHumidityPct_x1000(0));
  TEST_ASSERT_EQUAL_INT(129997, SCD41Device::convertTemperatureC_x1000(65535));
  TEST_ASSERT_EQUAL_UINT32(99998u, SCD41Device::convertHumidityPct_x1000(65535));

  const uint16_t rawOffset = SCD41Device::encodeTemperatureOffsetC_x1000(4000);
  TEST_ASSERT_EQUAL_INT(4000, SCD41Device::decodeTemperatureOffsetC_x1000(rawOffset));
  TEST_ASSERT_EQUAL_UINT16(1013u, SCD41Device::encodeAmbientPressurePa(101300));
  TEST_ASSERT_EQUAL_UINT32(101300u, SCD41Device::decodeAmbientPressurePa(1013));
}

void test_begin_waits_power_up_delay_and_does_not_track_startup_io() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  cfg.powerUpDelayMs = 3;
  queueBeginSuccess(bus);

  SCD41Device device;
  const Status st = device.begin(cfg);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(bus.yieldCount >= 3u);
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastOkMs());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::READY),
                    static_cast<uint8_t>(device.driverState()));
}

void test_update_health_ignores_in_progress() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  bus.nowMs = 123;

  const Status st = device._updateHealth(Status{Err::IN_PROGRESS, 0, "pending"});
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastOkMs());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::OK),
                    static_cast<uint8_t>(device.lastError().code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::READY),
                    static_cast<uint8_t>(device.state()));
}

void test_begin_wait_guard_times_out_if_time_source_stalls() {
  ScriptedTransport bus;
  bus.advanceOnYield = false;
  Config cfg = makeConfig(bus);
  cfg.powerUpDelayMs = 1;
  cfg.i2cTimeoutMs = 1;
  queueBeginSuccess(bus);

  SCD41Device device;
  const Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::TIMEOUT), static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_TRUE(bus.yieldCount > 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.writeIndex);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.readIndex);
}

void test_failed_begin_after_success_clears_health_snapshot() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  (void)device._updateHealth(Status::Error(Err::I2C_TIMEOUT, "forced stale error"));
  TEST_ASSERT_GREATER_THAN_UINT32(0u, device.totalFailures());

  ScriptedTransport stalledBus;
  stalledBus.advanceOnYield = false;
  Config stalledCfg = makeConfig(stalledBus);
  stalledCfg.powerUpDelayMs = 1;
  stalledCfg.i2cTimeoutMs = 1;

  const Status st = device.begin(stalledCfg);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::TIMEOUT), static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::UNINIT),
                    static_cast<uint8_t>(device.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastErrorMs());
}

void test_begin_reads_serial_and_variant() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  const Status st = device.begin(cfg);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::READY),
                    static_cast<uint8_t>(device.state()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SensorVariant::SCD41),
                    static_cast<uint8_t>(device._sensorVariant));
  TEST_ASSERT_TRUE(device._serialNumberValid);
}

void test_begin_rejects_non_scd41_variant() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueReadSerial(bus, 0x0001, 0x2222, 0x3333);

  SCD41Device device;
  const Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED), static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::UNINIT),
                    static_cast<uint8_t>(device.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastOkMs());
}

void test_probe_works_after_variant_mismatch_begin() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueReadSerial(bus, 0x0001, 0x2222, 0x3333);
  queueReadSerial(bus, 0x1001, 0x2345, 0x6789);

  SCD41Device device;
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                    static_cast<uint8_t>(device.begin(cfg).code));
  TEST_ASSERT_TRUE(device.probe().ok());
}

void test_single_shot_measurement_flow() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);
  queueReadMeasurement(bus, 500, 20000, 32768);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  bus.nowMs = 100;
  bus.nowUs = 100000;
  const Status st = device.requestMeasurement();
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PendingCommand::SINGLE_SHOT),
                    static_cast<uint8_t>(device.pendingCommand()));

  bus.nowMs = 5200;
  bus.nowUs = 5200000;
  device.tick(bus.nowMs);

  TEST_ASSERT_TRUE(device.measurementReady());
  Measurement sample;
  TEST_ASSERT_TRUE(device.getMeasurement(sample).ok());
  TEST_ASSERT_EQUAL_UINT16(500u, sample.co2Ppm);
  TEST_ASSERT_TRUE(sample.co2Valid);
}

void test_single_shot_rht_only_marks_co2_invalid() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);
  queueReadMeasurement(bus, 0, 25000, 30000);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.setSingleShotMode(SingleShotMode::T_RH_ONLY).ok());

  bus.nowMs = 50;
  bus.nowUs = 50000;
  TEST_ASSERT_TRUE(device.requestMeasurement().inProgress());
  bus.nowMs = 200;
  bus.nowUs = 200000;
  device.tick(bus.nowMs);

  Measurement sample;
  TEST_ASSERT_TRUE(device.getMeasurement(sample).ok());
  TEST_ASSERT_FALSE(sample.co2Valid);
  TEST_ASSERT_EQUAL_UINT16(0u, sample.co2Ppm);
}

void test_named_single_shot_helpers_schedule_correct_commands() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  TEST_ASSERT_TRUE(device.startSingleShotMeasurement().inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_MEASURE_SINGLE_SHOT, lastWriteCommand(bus));
  device._clearPendingCommand();
  device._clearMeasurementRequest();
  device._measurementReady = false;

  TEST_ASSERT_TRUE(device.startSingleShotRhtOnlyMeasurement().inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_MEASURE_SINGLE_SHOT_RHT_ONLY, lastWriteCommand(bus));
}

void test_direct_read_measurement_completes_pending_single_shot() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);
  queueReadMeasurement(bus, 0, 25000, 30000);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startSingleShotRhtOnlyMeasurement().inProgress());

  bus.nowMs = 200;
  bus.nowUs = 200000;

  Measurement sample;
  TEST_ASSERT_TRUE(device.readMeasurement(sample).ok());
  TEST_ASSERT_FALSE(sample.co2Valid);
  TEST_ASSERT_EQUAL_UINT16(0u, sample.co2Ppm);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PendingCommand::NONE),
                    static_cast<uint8_t>(device.pendingCommand()));
}

void test_periodic_start_request_and_stop() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);
  queueReadMeasurement(bus, 700, 18000, 40000);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  bus.nowMs = 0;
  bus.nowUs = 0;
  TEST_ASSERT_TRUE(device.startPeriodicMeasurement().ok());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::PERIODIC),
                    static_cast<uint8_t>(device.operatingMode()));

  TEST_ASSERT_TRUE(device.requestMeasurement().inProgress());
  bus.nowMs = 5000;
  bus.nowUs = 5000000;
  device.tick(bus.nowMs);
  TEST_ASSERT_TRUE(device.measurementReady());

  const Status st = device.stopPeriodicMeasurement();
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PendingCommand::STOP_PERIODIC),
                    static_cast<uint8_t>(device.pendingCommand()));

  bus.nowMs = 5600;
  bus.nowUs = 5600000;
  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::IDLE),
                    static_cast<uint8_t>(device.operatingMode()));
}

void test_direct_read_measurement_reads_periodic_sample() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);
  queueReadMeasurement(bus, 700, 18000, 40000);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startPeriodicMeasurement().ok());

  Measurement sample;
  TEST_ASSERT_TRUE(device.readMeasurement(sample).ok());
  TEST_ASSERT_EQUAL_UINT16(700u, sample.co2Ppm);
  TEST_ASSERT_TRUE(sample.co2Valid);
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_READ_MEASUREMENT, lastWriteCommand(bus));
}

void test_measurement_helpers_track_pending_and_preserve_last_sample() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);
  queueReadMeasurement(bus, 550, 21000, 33000);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_FALSE(device.measurementPending());
  TEST_ASSERT_EQUAL_UINT32(0u, device.measurementReadyMs());

  bus.nowMs = 100;
  bus.nowUs = 100000;
  TEST_ASSERT_TRUE(device.requestMeasurement().inProgress());
  TEST_ASSERT_TRUE(device.measurementPending());
  TEST_ASSERT_EQUAL_UINT32(5100u, device.measurementReadyMs());

  bus.nowMs = 5200;
  bus.nowUs = 5200000;
  device.tick(bus.nowMs);

  TEST_ASSERT_FALSE(device.measurementPending());
  TEST_ASSERT_TRUE(device.measurementReady());
  TEST_ASSERT_EQUAL_UINT32(0u, device.measurementReadyMs());

  SettingsSnapshot snap = {};
  TEST_ASSERT_TRUE(device.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.hasSample);

  Measurement last = {};
  TEST_ASSERT_TRUE(device.getLastMeasurement(last).ok());
  TEST_ASSERT_TRUE(device.measurementReady());
  TEST_ASSERT_EQUAL_UINT16(550u, last.co2Ppm);
  TEST_ASSERT_TRUE(last.co2Valid);

  Measurement sample = {};
  TEST_ASSERT_TRUE(device.getMeasurement(sample).ok());
  TEST_ASSERT_FALSE(device.measurementReady());
  TEST_ASSERT_EQUAL_UINT16(last.co2Ppm, sample.co2Ppm);
  TEST_ASSERT_EQUAL(last.co2Valid, sample.co2Valid);

  Measurement retained = {};
  TEST_ASSERT_TRUE(device.getLastMeasurement(retained).ok());
  TEST_ASSERT_EQUAL_UINT16(sample.co2Ppm, retained.co2Ppm);
  TEST_ASSERT_EQUAL(sample.co2Valid, retained.co2Valid);
}

void test_zero_timestamp_sample_is_still_available() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  bus.nowMs = 0;
  RawSample injected = {601, 20000, 30000};
  device._storeSample(injected, true);
  TEST_ASSERT_EQUAL_UINT32(0u, device.sampleTimestampMs());
  TEST_ASSERT_TRUE(device.hasSample());

  Measurement last = {};
  TEST_ASSERT_TRUE(device.getLastMeasurement(last).ok());
  TEST_ASSERT_EQUAL_UINT16(601u, last.co2Ppm);

  RawSample raw = {};
  TEST_ASSERT_TRUE(device.getRawSample(raw).ok());
  TEST_ASSERT_EQUAL_UINT16(601u, raw.rawCo2);

  CompensatedSample comp = {};
  TEST_ASSERT_TRUE(device.getCompensatedSample(comp).ok());
  TEST_ASSERT_TRUE(comp.co2Valid);

  SettingsSnapshot snap = {};
  TEST_ASSERT_TRUE(device.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.hasSample);
}

void test_offline_request_measurement_does_not_schedule_or_touch_bus() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startPeriodicMeasurement().ok());
  device._driverState = DriverState::OFFLINE;

  const size_t writeCalls = bus.writeCalls;
  const size_t readCalls = bus.readCalls;
  const Status st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(device.measurementPending());
  TEST_ASSERT_EQUAL_UINT32(0u, device.measurementReadyMs());
  TEST_ASSERT_EQUAL_UINT32(writeCalls, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readCalls, bus.readCalls);
}

void test_get_identity_uses_cached_serial_and_variant() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  bus.lastWriteLen = 0;
  memset(bus.lastWrite, 0, sizeof(bus.lastWrite));

  Identity identity = {};
  TEST_ASSERT_TRUE(device.getIdentity(identity).ok());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lastWriteLen);
  TEST_ASSERT_EQUAL_UINT64(device._serialNumber, identity.serialNumber);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(device._sensorVariant),
                    static_cast<uint8_t>(identity.variant));
}

void test_low_power_periodic_start_and_variant_guard() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startLowPowerPeriodicMeasurement().ok());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_START_LOW_POWER_PERIODIC_MEASUREMENT, lastWriteCommand(bus));

  ScriptedTransport otherBus;
  Config otherCfg = makeConfig(otherBus);
  otherCfg.strictVariantCheck = false;
  queueReadSerial(otherBus, 0x0001, 0x2222, 0x3333);

  SCD41Device otherDevice;
  TEST_ASSERT_TRUE(otherDevice.begin(otherCfg).ok());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                    static_cast<uint8_t>(otherDevice.startLowPowerPeriodicMeasurement().code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                    static_cast<uint8_t>(otherDevice.startSingleShotMeasurement().code));
}

void test_periodic_mode_allows_ambient_pressure_override() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 1013);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startPeriodicMeasurement().ok());

  TEST_ASSERT_TRUE(device.setAmbientPressurePa(101300).ok());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_SET_AMBIENT_PRESSURE, lastWriteCommand(bus));
  TEST_ASSERT_EQUAL_UINT16(1013u, lastWriteWord(bus));
  TEST_ASSERT_EQUAL_HEX8(SCD41Device::_crc8(&bus.lastWrite[2], 2), bus.lastWrite[4]);

  uint32_t pressurePa = 0;
  TEST_ASSERT_TRUE(device.getAmbientPressurePa(pressurePa).ok());
  TEST_ASSERT_EQUAL_UINT32(101300u, pressurePa);
}

void test_temperature_offset_roundtrip() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, SCD41Device::encodeTemperatureOffsetC_x1000(4000));

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.setTemperatureOffsetC_x1000(4000).ok());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_SET_TEMPERATURE_OFFSET, lastWriteCommand(bus));

  int32_t offsetC_x1000 = 0;
  TEST_ASSERT_TRUE(device.getTemperatureOffsetC_x1000(offsetC_x1000).ok());
  TEST_ASSERT_EQUAL_INT(4000, offsetC_x1000);
}

void test_temperature_offset_rejects_non_finite_values() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  bus.lastWriteLen = 0;

  Status st = device.setTemperatureOffsetC(NAN);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lastWriteLen);

  st = device.setTemperatureOffsetC(INFINITY);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
}

void test_low_level_command_helpers_work() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  const uint8_t rawResponse[3] = {0x12, 0x34, 0x56};
  queueReadBytes(bus, rawResponse, sizeof(rawResponse));
  queueReadWord(bus, 0x3456);
  queueReadMeasurement(bus, 0x1001, 0x2002, 0x3003);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  TEST_ASSERT_TRUE(device.writeCommand(0xABCD).ok());
  TEST_ASSERT_EQUAL_HEX16(0xABCD, lastWriteCommand(bus));

  TEST_ASSERT_TRUE(device.writeCommandWithData(0x2222, 0x1234).ok());
  TEST_ASSERT_EQUAL_HEX16(0x2222, lastWriteCommand(bus));
  TEST_ASSERT_EQUAL_UINT16(0x1234, lastWriteWord(bus));
  TEST_ASSERT_EQUAL_HEX8(SCD41Device::_crc8(&bus.lastWrite[2], 2), bus.lastWrite[4]);

  uint8_t out[3] = {};
  TEST_ASSERT_TRUE(device.readCommand(0x3333, out, sizeof(out)).ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rawResponse, out, sizeof(out));

  uint16_t word = 0;
  TEST_ASSERT_TRUE(device.readWordCommand(cmd::CMD_GET_SENSOR_ALTITUDE, word).ok());
  TEST_ASSERT_EQUAL_UINT16(0x3456u, word);

  uint16_t words[3] = {};
  TEST_ASSERT_TRUE(device.readWordsCommand(cmd::CMD_GET_SERIAL_NUMBER, words, 3).ok());
  TEST_ASSERT_EQUAL_UINT16(0x1001u, words[0]);
  TEST_ASSERT_EQUAL_UINT16(0x2002u, words[1]);
  TEST_ASSERT_EQUAL_UINT16(0x3003u, words[2]);
}

void test_low_level_raw_read_rejects_oversized_buffer_without_i2c() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  bus.lastWriteLen = 0;

  uint8_t oversized[10] = {};
  const Status st = device.readCommand(cmd::CMD_GET_SERIAL_NUMBER,
                                       oversized, sizeof(oversized));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lastWriteLen);
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
}

void test_command_delay_guard_times_out_if_time_source_stalls() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  cfg.i2cTimeoutMs = 1;
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  bus.advanceOnYield = false;
  bus.yieldCount = 0;

  bool ready = false;
  const Status st = device.readDataReadyStatus(ready);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::TIMEOUT), static_cast<uint8_t>(st.code));
  TEST_ASSERT_TRUE(bus.yieldCount > 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
}

void test_low_level_command_helpers_handle_expected_nack_and_no_data() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  bus.writeStatus[0] = Status::Error(Err::I2C_NACK_ADDR, "expected nack");
  bus.writeCount = 1;
  TEST_ASSERT_TRUE(device.writeCommand(0xABCD, true).ok());
  TEST_ASSERT_EQUAL_UINT32(1u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());

  queueReadStatus(bus, Status::Error(Err::I2C_NACK_READ, "no data"));
  uint8_t out[3] = {};
  const Status st = device.readCommand(0x3333, out, sizeof(out), true);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::MEASUREMENT_NOT_READY),
                    static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::READY),
                    static_cast<uint8_t>(device.state()));
}

void test_low_level_command_helper_failures_update_health() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadStatus(bus, Status::Error(Err::I2C_TIMEOUT, "timeout"));

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  uint16_t word = 0;
  const Status st = device.readWordCommand(cmd::CMD_GET_SENSOR_ALTITUDE, word);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::I2C_TIMEOUT), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, device.totalFailures());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::DEGRADED),
                    static_cast<uint8_t>(device.state()));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                    static_cast<uint8_t>(device.lastError().code));
}

void test_offline_set_temperature_offset_returns_busy_without_i2c() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  queueBeginSuccess(bus);
  queueReadStatus(bus, Status::Error(Err::I2C_TIMEOUT, "timeout"));

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  uint16_t word = 0;
  (void)device.readWordCommand(cmd::CMD_GET_SENSOR_ALTITUDE, word);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::OFFLINE),
                    static_cast<uint8_t>(device.state()));

  const size_t writesBefore = bus.writeCalls;
  const size_t readsBefore = bus.readCalls;
  Status st = device.setTemperatureOffsetC(1.5f);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
}

void test_failed_recover_from_offline_keeps_latch_after_intermediate_success() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 3;
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  bus.writeCount = cfg.offlineThreshold;
  bus.writeIndex = 0;
  for (uint8_t i = 0; i < cfg.offlineThreshold; ++i) {
    bus.writeStatus[i] = Status::Error(Err::I2C_TIMEOUT, "forced write timeout");
    TEST_ASSERT_FALSE(device.writeCommand(0xABCD).ok());
  }
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::OFFLINE),
                    static_cast<uint8_t>(device.state()));

  queueReadStatus(bus, Status::Error(Err::I2C_TIMEOUT, "recover read timeout"));
  const Status st = device.recover();
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::OFFLINE),
                    static_cast<uint8_t>(device.state()));
  TEST_ASSERT_TRUE(device.consecutiveFailures() >= cfg.offlineThreshold);
}

void test_low_level_command_helpers_reject_managed_and_periodic_restricted_commands() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                    static_cast<uint8_t>(
                        device.writeCommand(cmd::CMD_START_PERIODIC_MEASUREMENT).code));

  TEST_ASSERT_TRUE(device.startPeriodicMeasurement().ok());

  uint16_t raw = 0;
  TEST_ASSERT_TRUE(device.readWordCommand(cmd::CMD_GET_DATA_READY_STATUS, raw).ok());
  TEST_ASSERT_EQUAL_UINT16(0x0001u, raw);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::BUSY),
                    static_cast<uint8_t>(
                        device.readWordCommand(cmd::CMD_GET_TEMPERATURE_OFFSET, raw).code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED),
                    static_cast<uint8_t>(
                        device.writeCommand(cmd::CMD_STOP_PERIODIC_MEASUREMENT).code));
}

void test_read_settings_reads_live_device_configuration() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, SCD41Device::encodeTemperatureOffsetC_x1000(4250));
  queueReadWord(bus, 321);
  queueReadWord(bus, 1013);
  queueReadWord(bus, 1);
  queueReadWord(bus, 400);
  queueReadWord(bus, 44);
  queueReadWord(bus, 156);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(device.readSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.liveConfigValid);
  TEST_ASSERT_EQUAL_INT(
      SCD41Device::decodeTemperatureOffsetC_x1000(
          SCD41Device::encodeTemperatureOffsetC_x1000(4250)),
      snap.temperatureOffsetC_x1000);
  TEST_ASSERT_EQUAL_UINT16(321u, snap.sensorAltitudeM);
  TEST_ASSERT_EQUAL_UINT32(101300u, snap.ambientPressurePa);
  TEST_ASSERT_TRUE(snap.automaticSelfCalibrationEnabled);
  TEST_ASSERT_EQUAL_UINT16(400u, snap.automaticSelfCalibrationTargetPpm);
  TEST_ASSERT_EQUAL_UINT16(44u, snap.automaticSelfCalibrationInitialPeriodHours);
  TEST_ASSERT_EQUAL_UINT16(156u, snap.automaticSelfCalibrationStandardPeriodHours);
}

void test_read_settings_reads_periodic_ambient_pressure_only() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 1013);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startPeriodicMeasurement().ok());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(device.readSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.liveConfigValid);
  TEST_ASSERT_EQUAL_UINT32(101300u, snap.ambientPressurePa);
}

void test_asc_period_validation() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(
                        device.setAutomaticSelfCalibrationInitialPeriodHours(2).code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(
                        device.setAutomaticSelfCalibrationStandardPeriodHours(0).code));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::INVALID_PARAM),
                    static_cast<uint8_t>(
                        device.setAutomaticSelfCalibrationStandardPeriodHours(6).code));
}

void test_persist_reinit_factory_reset_schedule() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  TEST_ASSERT_TRUE(device.startPersistSettings().inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_PERSIST_SETTINGS, lastWriteCommand(bus));
  bus.nowMs = 1000;
  bus.nowUs = 1000000;
  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PendingCommand::NONE),
                    static_cast<uint8_t>(device.pendingCommand()));

  TEST_ASSERT_TRUE(device.startReinit().inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_REINIT, lastWriteCommand(bus));
  bus.nowMs = 2000;
  bus.nowUs = 2000000;
  device.tick(bus.nowMs);

  TEST_ASSERT_TRUE(device.startFactoryReset().inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_PERFORM_FACTORY_RESET, lastWriteCommand(bus));
  bus.nowMs = 4000;
  bus.nowUs = 4000000;
  device.tick(bus.nowMs);
}

void test_wake_up_accepts_expected_nack() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  device._operatingMode = OperatingMode::POWER_DOWN;
  bus.writeStatus[0] = Status::Error(Err::I2C_NACK_ADDR, "expected wake nack");
  bus.writeCount = 1;

  const Status st = device.wakeUp();
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PendingCommand::WAKE_UP),
                    static_cast<uint8_t>(device.pendingCommand()));

  bus.nowMs = cfg.powerUpDelayMs + 10;
  bus.nowUs = static_cast<uint32_t>(bus.nowMs) * 1000U;
  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::IDLE),
                    static_cast<uint8_t>(device.operatingMode()));
}

void test_self_test_completion_reads_pass_code() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, cmd::SELF_TEST_PASS);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startSelfTest().inProgress());

  bus.nowMs = 11000;
  bus.nowUs = 11000000;
  device.tick(bus.nowMs);

  uint16_t raw = 0xFFFF;
  TEST_ASSERT_TRUE(device.getSelfTestResult(raw).ok());
  TEST_ASSERT_EQUAL_UINT16(cmd::SELF_TEST_PASS, raw);
}

void test_self_test_raw_accessor_preserves_failure_word() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0007);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startSelfTest().inProgress());

  bus.nowMs = 11000;
  bus.nowUs = 11000000;
  device.tick(bus.nowMs);

  uint16_t raw = 0;
  TEST_ASSERT_TRUE(device.getSelfTestRawResult(raw).ok());
  TEST_ASSERT_EQUAL_UINT16(0x0007u, raw);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::COMMAND_FAILED),
                    static_cast<uint8_t>(device.getSelfTestResult(raw).code));
}

void test_forced_recalibration_reads_signed_correction() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x800A);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  bus.nowMs = 100;
  bus.nowUs = 100000;
  const Status st = device.startForcedRecalibration(400);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_PERFORM_FORCED_RECALIBRATION, lastWriteCommand(bus));
  TEST_ASSERT_EQUAL_HEX8(SCD41Device::_crc8(&bus.lastWrite[2], 2), bus.lastWrite[4]);

  bus.nowMs = 600;
  bus.nowUs = 600000;
  device.tick(bus.nowMs);

  int16_t correction = 0;
  TEST_ASSERT_TRUE(device.getForcedRecalibrationCorrectionPpm(correction).ok());
  TEST_ASSERT_EQUAL_INT(10, correction);
}

void test_forced_recalibration_raw_accessor_preserves_failure_sentinel() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, cmd::FRC_FAILED);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_TRUE(device.startForcedRecalibration(400).inProgress());

  bus.nowMs = 600;
  bus.nowUs = 600000;
  device.tick(bus.nowMs);

  uint16_t raw = 0;
  TEST_ASSERT_TRUE(device.getForcedRecalibrationRawResult(raw).ok());
  TEST_ASSERT_EQUAL_UINT16(cmd::FRC_FAILED, raw);

  int16_t correction = 0;
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::COMMAND_FAILED),
                    static_cast<uint8_t>(
                        device.getForcedRecalibrationCorrectionPpm(correction).code));
}

void test_data_ready_read_requires_init() {
  SCD41Device device;
  bool ready = false;
  const Status st = device.readDataReadyStatus(ready);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::NOT_INITIALIZED), static_cast<uint8_t>(st.code));
}

void test_data_ready_positive_and_power_down_schedule() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);
  queueReadWord(bus, 0x0001);

  SCD41Device device;
  TEST_ASSERT_TRUE(device.begin(cfg).ok());

  bool ready = false;
  TEST_ASSERT_TRUE(device.readDataReadyStatus(ready).ok());
  TEST_ASSERT_TRUE(ready);

  TEST_ASSERT_TRUE(device.powerDown().inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_POWER_DOWN, lastWriteCommand(bus));
  bus.nowMs = 10;
  bus.nowUs = 10000;
  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::POWER_DOWN),
                    static_cast<uint8_t>(device.operatingMode()));
}

void test_example_transport_maps_zero_byte_read_to_i2c_error() {
  uint8_t rx[3] = {};
  TEST_ASSERT_TRUE(transport::initWire(8, 9, 400000, 50));
  TEST_ASSERT_EQUAL_UINT32(400000u, Wire.getClock());
  TEST_ASSERT_EQUAL_UINT32(50u, Wire.getTimeOut());

  Wire._setRequestFromResult(0);
  const Status st = transport::wireWriteRead(cmd::I2C_ADDRESS, nullptr, 0, rx, sizeof(rx), 50,
                                             &Wire);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, Wire._readCallCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_status_helpers);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_crc_and_conversion_helpers);
  RUN_TEST(test_begin_waits_power_up_delay_and_does_not_track_startup_io);
  RUN_TEST(test_update_health_ignores_in_progress);
  RUN_TEST(test_begin_wait_guard_times_out_if_time_source_stalls);
  RUN_TEST(test_failed_begin_after_success_clears_health_snapshot);
  RUN_TEST(test_begin_reads_serial_and_variant);
  RUN_TEST(test_begin_rejects_non_scd41_variant);
  RUN_TEST(test_probe_works_after_variant_mismatch_begin);
  RUN_TEST(test_single_shot_measurement_flow);
  RUN_TEST(test_single_shot_rht_only_marks_co2_invalid);
  RUN_TEST(test_named_single_shot_helpers_schedule_correct_commands);
  RUN_TEST(test_direct_read_measurement_completes_pending_single_shot);
  RUN_TEST(test_periodic_start_request_and_stop);
  RUN_TEST(test_direct_read_measurement_reads_periodic_sample);
  RUN_TEST(test_measurement_helpers_track_pending_and_preserve_last_sample);
  RUN_TEST(test_zero_timestamp_sample_is_still_available);
  RUN_TEST(test_offline_request_measurement_does_not_schedule_or_touch_bus);
  RUN_TEST(test_get_identity_uses_cached_serial_and_variant);
  RUN_TEST(test_low_power_periodic_start_and_variant_guard);
  RUN_TEST(test_periodic_mode_allows_ambient_pressure_override);
  RUN_TEST(test_temperature_offset_roundtrip);
  RUN_TEST(test_temperature_offset_rejects_non_finite_values);
  RUN_TEST(test_low_level_command_helpers_work);
  RUN_TEST(test_low_level_raw_read_rejects_oversized_buffer_without_i2c);
  RUN_TEST(test_command_delay_guard_times_out_if_time_source_stalls);
  RUN_TEST(test_low_level_command_helpers_handle_expected_nack_and_no_data);
  RUN_TEST(test_low_level_command_helper_failures_update_health);
  RUN_TEST(test_offline_set_temperature_offset_returns_busy_without_i2c);
  RUN_TEST(test_failed_recover_from_offline_keeps_latch_after_intermediate_success);
  RUN_TEST(test_low_level_command_helpers_reject_managed_and_periodic_restricted_commands);
  RUN_TEST(test_read_settings_reads_live_device_configuration);
  RUN_TEST(test_read_settings_reads_periodic_ambient_pressure_only);
  RUN_TEST(test_asc_period_validation);
  RUN_TEST(test_persist_reinit_factory_reset_schedule);
  RUN_TEST(test_wake_up_accepts_expected_nack);
  RUN_TEST(test_self_test_completion_reads_pass_code);
  RUN_TEST(test_self_test_raw_accessor_preserves_failure_word);
  RUN_TEST(test_forced_recalibration_reads_signed_correction);
  RUN_TEST(test_forced_recalibration_raw_accessor_preserves_failure_sentinel);
  RUN_TEST(test_data_ready_read_requires_init);
  RUN_TEST(test_data_ready_positive_and_power_down_schedule);
  RUN_TEST(test_example_transport_maps_zero_byte_read_to_i2c_error);
  return UNITY_END();
}
