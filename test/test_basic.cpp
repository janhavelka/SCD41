/// @file test_basic.cpp
/// @brief Basic unit tests for SCD41 driver

#include <unity.h>

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

using namespace SCD41;
using SCD41Device = SCD41::SCD41;

void setUp() {}
void tearDown() {}

void test_status_helpers() {
  Status ok = Status::Ok();
  TEST_ASSERT_TRUE(ok.ok());
  TEST_ASSERT_TRUE(ok.is(Err::OK));
  TEST_ASSERT_TRUE(ok);

  Status pending{Err::IN_PROGRESS, 0, "pending"};
  TEST_ASSERT_FALSE(pending.ok());
  TEST_ASSERT_TRUE(pending.inProgress());

  Status err = Status::Error(Err::CRC_MISMATCH, "crc");
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
}

struct ScriptedTransport {
  Status writeStatus[16] = {};
  size_t writeCount = 0;
  size_t writeIndex = 0;

  Status readStatus[16] = {};
  uint8_t readData[16][9] = {};
  size_t readLen[16] = {};
  size_t readCount = 0;
  size_t readIndex = 0;

  uint8_t lastWrite[16] = {};
  size_t lastWriteLen = 0;

  uint32_t nowMs = 0;
  uint32_t nowUs = 0;
  uint32_t yieldCount = 0;
};

static uint8_t packWord(uint16_t word, uint8_t* out) {
  out[0] = static_cast<uint8_t>((word >> 8) & 0xFF);
  out[1] = static_cast<uint8_t>(word & 0xFF);
  out[2] = SCD41Device::_crc8(out, 2);
  return out[2];
}

static void queueReadWord(ScriptedTransport& bus, uint16_t word) {
  TEST_ASSERT_TRUE(bus.readCount < 16);
  bus.readStatus[bus.readCount] = Status::Ok();
  bus.readLen[bus.readCount] = 3;
  packWord(word, bus.readData[bus.readCount]);
  bus.readCount++;
}

static void queueReadMeasurement(ScriptedTransport& bus, uint16_t co2, uint16_t temp,
                                 uint16_t humidity) {
  TEST_ASSERT_TRUE(bus.readCount < 16);
  bus.readStatus[bus.readCount] = Status::Ok();
  bus.readLen[bus.readCount] = 9;
  packWord(co2, &bus.readData[bus.readCount][0]);
  packWord(temp, &bus.readData[bus.readCount][3]);
  packWord(humidity, &bus.readData[bus.readCount][6]);
  bus.readCount++;
}

static void queueReadSerial(ScriptedTransport& bus, uint16_t word0, uint16_t word1, uint16_t word2) {
  queueReadMeasurement(bus, word0, word1, word2);
}

static Status scriptedWrite(uint8_t addr, const uint8_t* data, size_t len, uint32_t timeoutMs,
                            void* user) {
  (void)timeoutMs;
  auto* ctx = static_cast<ScriptedTransport*>(user);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDRESS, addr);
  ctx->lastWriteLen = len;
  memset(ctx->lastWrite, 0, sizeof(ctx->lastWrite));
  memcpy(ctx->lastWrite, data, len);
  if (ctx->writeIndex < ctx->writeCount) {
    return ctx->writeStatus[ctx->writeIndex++];
  }
  return Status::Ok();
}

static Status scriptedWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                uint8_t* rxData, size_t rxLen, uint32_t timeoutMs, void* user) {
  (void)txData;
  (void)timeoutMs;
  auto* ctx = static_cast<ScriptedTransport*>(user);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDRESS, addr);
  TEST_ASSERT_EQUAL_UINT32(0u, txLen);
  if (ctx->readIndex < ctx->readCount) {
    const Status st = ctx->readStatus[ctx->readIndex];
    if (st.ok()) {
      TEST_ASSERT_EQUAL_UINT32(ctx->readLen[ctx->readIndex], rxLen);
      memcpy(rxData, ctx->readData[ctx->readIndex], rxLen);
    }
    ctx->readIndex++;
    return st;
  }
  return Status::Ok();
}

static uint32_t scriptedNowMs(void* user) {
  return static_cast<ScriptedTransport*>(user)->nowMs;
}

static uint32_t scriptedNowUs(void* user) {
  return static_cast<ScriptedTransport*>(user)->nowUs;
}

static void scriptedYield(void* user) {
  auto* ctx = static_cast<ScriptedTransport*>(user);
  ctx->yieldCount++;
  ctx->nowMs++;
  ctx->nowUs += 1000;
}

static Config makeConfig(ScriptedTransport& bus) {
  Config cfg;
  cfg.i2cWrite = scriptedWrite;
  cfg.i2cWriteRead = scriptedWriteRead;
  cfg.i2cUser = &bus;
  cfg.nowMs = scriptedNowMs;
  cfg.nowUs = scriptedNowUs;
  cfg.cooperativeYield = scriptedYield;
  cfg.timeUser = &bus;
  cfg.offlineThreshold = 3;
  return cfg;
}

static void queueBeginSuccess(ScriptedTransport& bus) {
  queueReadSerial(bus, 0x1001, 0x2345, 0x6789);
}

void test_begin_reads_serial_and_variant() {
  ScriptedTransport bus;
  Config cfg = makeConfig(bus);
  queueBeginSuccess(bus);

  SCD41Device device;
  Status st = device.begin(cfg);
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
  Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::UNSUPPORTED), static_cast<uint8_t>(st.code));
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
  Status st = device.requestMeasurement();
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

  Status st = device.stopPeriodicMeasurement();
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PendingCommand::STOP_PERIODIC),
                    static_cast<uint8_t>(device.pendingCommand()));

  bus.nowMs = 5600;
  bus.nowUs = 5600000;
  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::IDLE),
                    static_cast<uint8_t>(device.operatingMode()));
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
  bus.writeIndex = 0;

  Status st = device.wakeUp();
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PendingCommand::WAKE_UP),
                    static_cast<uint8_t>(device.pendingCommand()));

  bus.nowMs = cfg.powerUpDelayMs + 10;
  bus.nowUs = static_cast<uint32_t>(bus.nowMs) * 1000U;
  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(OperatingMode::IDLE),
                    static_cast<uint8_t>(device.operatingMode()));
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
  Status st = device.startForcedRecalibration(400);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_PERFORM_FORCED_RECALIBRATION,
                          static_cast<uint16_t>((bus.lastWrite[0] << 8) | bus.lastWrite[1]));
  TEST_ASSERT_EQUAL_HEX8(SCD41Device::_crc8(&bus.lastWrite[2], 2), bus.lastWrite[4]);

  bus.nowMs = 600;
  bus.nowUs = 600000;
  device.tick(bus.nowMs);

  int16_t correction = 0;
  TEST_ASSERT_TRUE(device.getForcedRecalibrationCorrectionPpm(correction).ok());
  TEST_ASSERT_EQUAL_INT(10, correction);
}

void test_data_ready_read_requires_init() {
  SCD41Device device;
  bool ready = false;
  Status st = device.readDataReadyStatus(ready);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Err::NOT_INITIALIZED), static_cast<uint8_t>(st.code));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_status_helpers);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_crc_and_conversion_helpers);
  RUN_TEST(test_begin_reads_serial_and_variant);
  RUN_TEST(test_begin_rejects_non_scd41_variant);
  RUN_TEST(test_single_shot_measurement_flow);
  RUN_TEST(test_single_shot_rht_only_marks_co2_invalid);
  RUN_TEST(test_periodic_start_request_and_stop);
  RUN_TEST(test_wake_up_accepts_expected_nack);
  RUN_TEST(test_forced_recalibration_reads_signed_correction);
  RUN_TEST(test_data_ready_read_requires_init);
  return UNITY_END();
}
