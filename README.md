# SCD41 Driver Library

Production-grade PlatformIO/Arduino package for the Sensirion SCD41 photoacoustic NDIR CO2, temperature, and humidity sensor on ESP32-S2 / ESP32-S3.

## Overview

This repository follows the same managed I2C library pattern used across the workspace:

- injected I2C transport, no direct `Wire` dependency in library code
- explicit `Status` return values on every fallible operation
- 4-state health tracking: `UNINIT`, `READY`, `DEGRADED`, `OFFLINE`
- deterministic timing with bounded waits only
- CRC-8 validation on every returned 16-bit data word
- no steady-state heap allocation and no logging inside the library

The device source of truth for this repository is [docs/SCD41_datasheet.md](docs/SCD41_datasheet.md).

## Managed Feature Coverage

The library covers the practical documented SCD41 runtime surface:

- fixed I2C address `0x62`
- serial-number probe and SCD41 variant verification
- periodic measurement mode, 1 sample every 5 s
- low-power periodic mode, 1 sample every 30 s
- single-shot CO2 + temperature + humidity measurement
- single-shot temperature + humidity-only measurement
- explicit `readMeasurement()` helper for the `read_measurement` command
- data-ready polling and CRC-checked measurement reads
- temperature offset, sensor altitude, and ambient pressure compensation
- automatic self-calibration enable, target, and period controls
- forced recalibration, persist settings, reinit, self-test, factory reset, power-down, and wake-up
- live settings readback via `readSettings()`
- public raw command helpers via `writeCommand()`, `writeCommandWithData()`, `readCommand()`, `readWordCommand()`, and `readWordsCommand()`

## Driver Model

The driver uses a managed asynchronous model:

- `begin()` validates the transport, waits the configured power-up settle time, reads the serial number, and records the observed sensor variant.
- `tick(nowMs)` advances bounded long-running work such as single-shot completion, periodic-stop settle timing, self-test completion, and forced recalibration completion.
- `requestMeasurement()` schedules work using the current operating mode.
- `measurementPending()` and `measurementReadyMs()` expose the driver's local sample-fetch state directly, so applications do not need to mirror it with their own shadow flags.
- `readMeasurement()` is the direct helper for the sensor's `read_measurement` command. It completes a due single-shot request or fetches a ready periodic sample while keeping the cached sample state coherent.
- `getMeasurement()` returns the cached converted sample and clears the ready flag.
- `getLastMeasurement()` returns the most recent converted sample without consuming the ready flag.

This split keeps long device operations explicit and predictable. Public calls do not hide multi-second waits behind a single synchronous API.

## Thread, ISR, And Recovery Model

- The driver is single-threaded: call public APIs from one task or loop context, or serialize access externally.
- Do not call I2C-backed APIs from ISRs. Use an interrupt only to set an application flag, then call the driver from normal task context.
- `OFFLINE` is latched. Normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch the bus.
- `probe()` remains a raw diagnostic check and does not update health counters. `recover()` and explicit reset commands such as `startReinit()` and `startFactoryReset()` may access I2C while offline.

## Important Device Rules

- The sensor must be given up to 30 ms after power-up before the first command. `begin()` honors `Config::powerUpDelayMs` before probing the serial number.
- All commands are 16-bit, MSB-first. Every returned 16-bit word is followed by a CRC byte.
- During command execution, read attempts may NACK. This is normal busy behavior.
- `wake_up` is special: the device NACKs the command by design, then wakes within 30 ms.
- While periodic measurement is active, only `read_measurement`, `get_data_ready_status`, `set_ambient_pressure`, `get_ambient_pressure`, and `stop_periodic_measurement` are valid.
- `stop_periodic_measurement` requires a 500 ms settle window before idle-only commands.
- Low-power periodic mode trades some CO2 accuracy for lower average current.
- In single-shot workflows, the first 1..3 CO2 results after power-up or mode changes may be unreliable. The driver reports RHT-only CO2 invalidity directly, but warm-up discard policy remains application-managed.
- `persist_settings` writes EEPROM. Sensirion rates this storage for at least 2000 write cycles, so persistence must remain explicit and infrequent.
- The sensor can draw 175-205 mA peak current during the photoacoustic pulse. Budget the supply accordingly and place at least 10 uF bulk capacitance near the device.

## Public API

The public driver follows the same family conventions as the mature workspace libraries:

- lifecycle: `begin()`, `tick()`, `end()`, `probe()`, `recover()`
- measurement flow: `requestMeasurement()`, `measurementPending()`, `measurementReadyMs()`, `readMeasurement()`, `getMeasurement()`, `getLastMeasurement()`, `getRawSample()`, `getCompensatedSample()`, `hasSample()`
- state and health: `state()`, `driverState()`, `isOnline()`, `isBusy()`, `isPeriodicActive()`, `operatingMode()`, `pendingCommand()`, `commandReadyMs()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, `totalSuccess()`
- measurement readiness and status: `lastSampleCo2Valid()`, `sampleTimestampMs()`, `sampleAgeMs()`, `missedSamplesEstimate()`, `readDataReadyStatus()`
- mode control: `setSingleShotMode()`, `startPeriodicMeasurement()`, `startLowPowerPeriodicMeasurement()`, `stopPeriodicMeasurement()`, `powerDown()`, `wakeUp()`
- mode/query helpers: `getSingleShotMode()`, `getIdentity()`, `readSerialNumber()`, `readSensorVariant()`
- named single-shot commands: `startSingleShotMeasurement()`, `startSingleShotRhtOnlyMeasurement()`
- compensation/config: `setTemperatureOffsetC()` (finite values only), `setSensorAltitudeM()`, `setAmbientPressurePa()`, ASC getters/setters
- maintenance: `startPersistSettings()`, `startReinit()`, `startFactoryReset()`, `startSelfTest()`, `getSelfTestResult()`, `getSelfTestRawResult()`, `startForcedRecalibration()`, `getForcedRecalibrationCorrectionPpm()`, `getForcedRecalibrationRawResult()`
- raw command access: `writeCommand()`, `writeCommandWithData()`, `readCommand()`, `readWordCommand()`, `readWordsCommand()`
- snapshots: `getSettings()` for local driver state, including `hasSample`, and `readSettings()` for a best-effort state plus live-configuration snapshot

`readSettings()` is mode-dependent by design:

- in idle mode it reads the live device configuration fields
- in periodic mode it refreshes ambient pressure only, because the other configuration commands are idle-only
- while a command is pending, a measurement is in progress, or the sensor is powered down, it returns the local driver snapshot without live refresh

The raw command helpers are limited to immediate, non-stateful diagnostic transactions. Managed mode transitions and long-running commands must still use the typed APIs, and the raw helpers continue to enforce the sensor's periodic-mode command restrictions. Raw read helpers reject buffers larger than the largest documented SCD41 response (9 bytes), invalid buffers, and locally invalid requests before touching I2C.

Cached raw, fixed-point, and converted sample access is gated by whether any
sample has ever been cached (`hasSample()` / `SettingsSnapshot::hasSample`), not
by `measurementReady` or by a nonzero timestamp. `getMeasurement()` can consume
the current ready flag while the cached sample remains available through the
snapshot and cached-sample helpers.

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

Long operations are modeled as bounded start/poll/read flows driven by `tick()` rather than blocking the public call for hundreds of milliseconds or seconds. Short synchronous wait guards also have a finite stalled-timebase escape path, so a broken injected clock/yield hook returns `TIMEOUT` instead of spinning indefinitely.

## Conversion Rules

`read_measurement` returns three CRC-protected words:

- CO2: raw word interpreted directly as ppm
- temperature: `-45 + 175 * raw / 65535`
- humidity: `100 * raw / 65535`

The library also exposes fixed-point helpers:

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
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return SCD41::Status::Error(SCD41::Err::INVALID_CONFIG, "Wire instance is null");
  }
  if (txLen != 0) {
    return SCD41::Status::Error(SCD41::Err::INVALID_PARAM, "Combined write+read not supported");
  }
  const size_t received = wire->requestFrom(addr, rxLen);
  if (received != rxLen) {
    return SCD41::Status::Error(SCD41::Err::I2C_ERROR, "I2C read failed",
                                static_cast<int32_t>(received));
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

  if (!sensor.measurementPending() && !sensor.measurementReady()) {
    (void)sensor.requestMeasurement();
  }

  if (sensor.measurementReady()) {
    SCD41::Measurement sample;
    if (sensor.getMeasurement(sample).ok()) {
      Serial.printf("CO2=%u ppm T=%.2f C RH=%.2f %%\n",
                    sample.co2Ppm,
                    sample.temperatureC,
                    sample.humidityPct);
    }
  }
}
```

This example uses the cached-sample flow. If you want a direct command-style read path, call
`readMeasurement()` once a single-shot deadline has elapsed or a periodic sample is reported ready.
If you need the latest converted sample for logging or diagnostics without consuming the ready flag,
use `getLastMeasurement()`.

## Example CLI

[examples/01_basic_bringup_cli](examples/01_basic_bringup_cli) provides a family-style serial REPL for bring-up and diagnostics.

Main command groups:

- Common: `help`, `version`, `info`, `scan`, `begin`, `end`, `probe`, `recover`, `diag`, `drv`, `drv1`, `state`, `cfg`, `settings`, `status`, `verbose`
- Measurement: `read`, `fetch`, `sample`, `last`, `raw`, `comp`, `dataready`, `watch`, `stress`, `single`, `single_start`, `convert`
- Mode and power: `mode`, `periodic`, `sleep`, `wake`
- Identity and compensation: `serial`, `variant`, `toffset`, `altitude`, `pressure`, `asc_enabled`, `asc_target`, `asc_initial`, `asc_standard`
- Maintenance: `persist`, `reinit`, `factory_reset`, `selftest`, `selftest_result`, `frc`, `frc_result`
- Raw commands: `command write`, `command write_data`, `command read`, `command read_word`, `command read_words`

`read` follows the managed driver path: it prints a sample immediately if one is already ready, or
it schedules a managed fetch when no sample is pending yet. `fetch` is the explicit direct-read path
for a sample that is expected to be ready now. `verbose` follows the family CLI convention and acts
as a toggle with no argument, or as an explicit setter with `0` / `1`. `status` is the concise
chip/runtime view for pending commands, timing, and live `get_data_ready_status`, while `sample` /
`last` expose the most recently cached converted, raw, and fixed-point sample without consuming it.
Deferred operations such as self-test, forced recalibration, wake-up, stop-periodic, reinit, and
factory reset now print explicit pending-work and completion summaries so the operator can follow the
chip state without polling internal flags manually.

The raw command CLI is intentionally limited to immediate diagnostic commands. Managed transitions such
as periodic-mode entry/exit, wake-up, self-test, and forced recalibration should be driven through the
typed commands so the driver state stays coherent.

Typical bring-up flow:

```text
scan
drv
settings
periodic on
watch 1
watch 0
selftest
frc 400
persist
command read_word 0xE4B8
```

## Build And Validation

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
```

## Repository Notes

- Public headers live in <a href="include/SCD41">include/SCD41</a>
- Implementation lives in <a href="src">src</a>
- Version metadata is generated into `include/SCD41/Version.h` from <a href="library.json">library.json</a>
- `examples/common` is example-only glue and is not installed as part of the library
- The library never configures I2C pins or owns the bus
- <a href="ASSUMPTIONS.md">ASSUMPTIONS.md</a> records the remaining SCD41-specific policy assumptions

## Documentation

- <a href="CHANGELOG.md">CHANGELOG.md</a> - release history
- <a href="AGENTS.md">AGENTS.md</a> - repository engineering rules
- <a href="ASSUMPTIONS.md">ASSUMPTIONS.md</a> - explicit assumptions and scope notes
- <a href="docs/SCD41_datasheet.md">docs/SCD41_datasheet.md</a> - datasheet-derived implementation reference

## License

MIT License. See [LICENSE](LICENSE).
