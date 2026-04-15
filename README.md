# SCD41 Driver Library

Production-grade package for the Sensirion SCD41 photoacoustic NDIR CO2, temperature, and humidity sensor on ESP32-S2 / ESP32-S3 using Arduino and PlatformIO.

## Overview

This repository follows the same managed I2C library pattern used across the workspace:

- injected I2C transport, no direct `Wire` dependency in library code
- explicit `Status` return values on every fallible operation
- 4-state health tracking: `UNINIT`, `READY`, `DEGRADED`, `OFFLINE`
- deterministic behavior with bounded timing only
- CRC-8 validation on every returned 16-bit data word
- no steady-state heap allocation and no logging inside the library

The device source of truth for this repository is [docs/SCD41_datasheet.md](docs/SCD41_datasheet.md).

## Managed Feature Coverage

The intended SCD41 library surface covers the practical runtime features of the device:

- fixed I2C address `0x62`
- serial-number probe and SCD41 variant verification
- periodic measurement mode, 1 sample every 5 s
- low-power periodic mode, 1 sample every 30 s
- single-shot CO2 + temperature + humidity measurement
- single-shot temperature + humidity-only measurement
- data-ready polling and CRC-checked measurement reads
- temperature offset, sensor altitude, and ambient pressure compensation
- automatic self-calibration enable, target, and period controls
- forced recalibration, reinit, self-test, factory reset, power-down, and wake-up
- explicit persistence flow via `persist_settings`

## API Shape

The implemented driver follows these main entry points:

- lifecycle: `begin()`, `tick()`, `end()`, `probe()`, `recover()`
- measurement flow: `requestMeasurement()`, `measurementReady()`, `getMeasurement()`
- mode control: `startPeriodicMeasurement()`, `startLowPowerPeriodicMeasurement()`, `stopPeriodicMeasurement()`, `powerDown()`, `wakeUp()`
- configuration: `setTemperatureOffsetC()`, `setSensorAltitudeM()`, `setAmbientPressurePa()`, ASC getters/setters
- long operations: `startPersistSettings()`, `startReinit()`, `startFactoryReset()`, `startSelfTest()`, `startForcedRecalibration()`
- results for long operations: `getSelfTestResult()`, `getForcedRecalibrationCorrectionPpm()`

## Important Device Rules

- The sensor must be given up to 30 ms after power-up before the first command.
- All commands are 16-bit, MSB-first. Every returned 16-bit word is followed by a CRC byte.
- During command execution, read attempts may NACK. This is normal busy behavior.
- `wake_up` is special: the device NACKs the command by design, then wakes within 30 ms.
- While periodic measurement is active, only `read_measurement`, `get_data_ready_status`, `set_ambient_pressure`, `get_ambient_pressure`, and `stop_periodic_measurement` are valid.
- `stop_periodic_measurement` requires a 500 ms settle window before idle-only commands.
- `persist_settings` writes EEPROM. Sensirion rates this storage for at least 2000 write cycles, so persistence must remain explicit and infrequent.
- The sensor can draw 175-205 mA peak current during the photoacoustic pulse. Budget the supply accordingly and place at least 10 uF bulk capacitance near the device.

## Measurement Timing Summary

| Operation | Typical bounded execution window |
| --- | --- |
| `read_measurement`, get/set compensation values, `get_serial_number` | 1 ms |
| `wake_up`, `reinit` | 30 ms |
| `measure_single_shot_rht_only` | 50 ms |
| `perform_forced_recalibration` | 400 ms |
| `stop_periodic_measurement` | 500 ms |
| `persist_settings` | 800 ms |
| `perform_factory_reset` | 1200 ms |
| `measure_single_shot` | 5000 ms |
| `perform_self_test` | 10000 ms |

The repository guidelines require long operations to be modeled as bounded start/poll/read flows rather than 400 ms to 10 s busy-waits in a public API call.

## Conversion Rules

`read_measurement` returns three CRC-protected words:

- CO2: raw word interpreted directly as ppm
- temperature: `-45 + 175 * raw / 65535`
- humidity: `100 * raw / 65535`

Recommended fixed-point helpers:

```text
temperature_mdegC = ((21875 * raw) >> 13) - 45000
humidity_milliPct = ((12500 * raw) >> 13)
```

`get_data_ready_status` is true when `(word & 0x07FF) != 0`.

## Installation

### PlatformIO

```ini
lib_deps =
  https://github.com/janhavelka/SCD41.git
```

### Manual

Copy [include/SCD41](include/SCD41) and [src](src) into your project.

## Quick Start

```cpp
#include <Wire.h>
#include "SCD41/SCD41.h"

static SCD41::Status i2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user) {
  (void)timeoutMs;
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return SCD41::Status::Error(SCD41::Err::INVALID_CONFIG, "Wire instance is null");
  }
  wire->beginTransmission(addr);
  wire->write(data, len);
  const uint8_t result = wire->endTransmission(true);
  switch (result) {
    case 0: return SCD41::Status::Ok();
    case 2: return SCD41::Status::Error(SCD41::Err::I2C_NACK_ADDR, "I2C NACK addr", result);
    case 3: return SCD41::Status::Error(SCD41::Err::I2C_NACK_DATA, "I2C NACK data", result);
    case 4: return SCD41::Status::Error(SCD41::Err::I2C_BUS, "I2C bus error", result);
    case 5: return SCD41::Status::Error(SCD41::Err::I2C_TIMEOUT, "I2C timeout", result);
    default: return SCD41::Status::Error(SCD41::Err::I2C_ERROR, "I2C write failed", result);
  }
}

static SCD41::Status i2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user) {
  (void)timeoutMs;
  (void)txData;
  if (txLen != 0) {
    return SCD41::Status::Error(SCD41::Err::INVALID_PARAM, "Combined write+read not supported");
  }
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return SCD41::Status::Error(SCD41::Err::INVALID_CONFIG, "Wire instance is null");
  }
  const size_t received = wire->requestFrom(addr, rxLen);
  if (received != rxLen) {
    return SCD41::Status::Error(SCD41::Err::I2C_ERROR, "I2C read failed", static_cast<int32_t>(received));
  }
  for (size_t i = 0; i < rxLen; ++i) {
    rxData[i] = static_cast<uint8_t>(wire->read());
  }
  return SCD41::Status::Ok();
}

SCD41::SCD41 sensor;

void setup() {
  Wire.begin(8, 9);
  Wire.setClock(400000);

  SCD41::Config cfg;
  cfg.i2cWrite = i2cWrite;
  cfg.i2cWriteRead = i2cWriteRead;
  cfg.i2cUser = &Wire;

  if (!sensor.begin(cfg).ok()) {
    return;
  }
}

void loop() {
  sensor.tick(millis());

  static bool pending = false;
  if (!pending) {
    if (sensor.requestMeasurement().inProgress()) {
      pending = true;
    }
  }

  if (pending && sensor.measurementReady()) {
    SCD41::Measurement m;
    if (sensor.getMeasurement(m).ok()) {
      Serial.printf("CO2=%u ppm T=%.2f C RH=%.2f %%\n", m.co2Ppm, m.temperatureC, m.humidityPct);
    }
    pending = false;
  }
}
```

For single-shot mode the driver schedules a 50 ms or 5000 ms deadline and completes the read in `tick()`.
For periodic mode the driver schedules the next fetch window and reads when the sensor reports data ready.

## Build And Validation

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
```

## Repository Notes

- Public headers live in [include/SCD41](include/SCD41)
- Implementation lives in [src](src)
- Version metadata is generated into `include/SCD41/Version.h` from [library.json](library.json)
- `examples/common` is example-only glue and is not installed as part of the library
- The library never configures I2C pins or owns the bus
- [ASSUMPTIONS.md](ASSUMPTIONS.md) records the SCD41-specific assumptions that remain application policy decisions

## Documentation

- [CHANGELOG.md](CHANGELOG.md) - release history
- [AGENTS.md](AGENTS.md) - repository engineering rules
- [ASSUMPTIONS.md](ASSUMPTIONS.md) - explicit assumptions and scope notes
- [docs/SCD41_datasheet.md](docs/SCD41_datasheet.md) - datasheet-derived implementation reference

## License

MIT License. See [LICENSE](LICENSE).
