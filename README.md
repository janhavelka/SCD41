# SCD41 Driver Library

Production-grade PlatformIO/Arduino and ESP-IDF component package for the Sensirion SCD41 photoacoustic NDIR CO2, temperature, and humidity sensor on ESP32-S2 / ESP32-S3.

## Overview

This repository follows the same managed I2C library pattern used across the workspace:

- injected I2C transport, no direct `Wire` dependency in library code
- framework-neutral core with Arduino and ESP-IDF integration behind callbacks/adapters
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
- public diagnostic command helpers via `writeCommand()`, `writeCommandWithData()`, `readCommand()`, `readCommandUnsafe()`, `readWordCommand()`, and `readWordsCommand()`

## Driver Model

The driver uses a managed asynchronous model:

- `begin()` validates the transport, waits the configured power-up settle time, reads the serial number, and records the observed sensor variant.
- `tick(nowMs)` advances bounded long-running work such as single-shot completion, periodic-stop settle timing, self-test completion, and forced recalibration completion. Capture its returned `Status` and log non-OK async completion failures.
- `requestMeasurement()` schedules work using the current operating mode.
- `measurementPending()` and `measurementReadyMs()` expose the driver's local sample-fetch state directly, so applications do not need to mirror it with their own shadow flags.
- `readMeasurement()` is the direct helper for the sensor's `read_measurement` command. It completes a due single-shot request or fetches a ready periodic sample while keeping the cached sample state coherent.
- `getMeasurement()` returns the cached converted sample and clears the ready flag.
- `getLastMeasurement()` returns the most recent converted sample without consuming the ready flag.

This split keeps long device operations explicit and predictable. Public calls do not hide multi-second waits behind a single synchronous API.

## Timing Model

`Config::nowMs` and `Config::nowUs` are required. `nowMs` is the driver's
canonical monotonic millisecond clock for waits, deadlines, and scheduled
completion. `nowUs` must be a microsecond view of the same monotonic clock
domain and is used for the 1 ms inter-command guard. `cooperativeYield` is
optional, but Arduino and RTOS examples provide it so bounded waits can yield to
the scheduler.

`Status tick(uint32_t nowMs)` is kept as the lifecycle API for existing sketches and
applications. The driver samples `Config::nowMs` internally during `tick()`, so
pass values from the same clock domain to `tick()` and `sampleAgeMs()`. Do not
mix wall time, RTOS ticks, and Arduino `millis()` values in one driver instance.

## Async Completion Status

`tick()` returns `Status::Ok()` before initialization, when no async completion
is due, when due work succeeds, or when a measurement is still legitimately not
ready and has been rescheduled. Transport, CRC, and sensor-reported failures
from due completions are returned immediately and stored in `lastAsyncStatus()`
with the associated `lastAsyncOperation()`.

Call `clearLastAsyncStatus()` after recording an async failure if your
application wants to acknowledge it explicitly. A later successful async
completion also supersedes the previous async status with OK. No-due ticks do
not clear a previous failure, so a loop cannot erase an error before the
application inspects it.

Each `tick()` processes at most one due async completion. Pending commands are
completed before scheduled periodic fetches, so applications should call
`tick()` regularly from the main loop or task.

## Thread, ISR, And Recovery Model

- The driver is single-threaded: call public APIs from one task or loop context, or serialize access externally.
- Do not call public driver APIs from ISRs. Use an interrupt only to set an application flag, then call the driver from normal task context.
- The driver does not perform internal locking. Transport callbacks must not recursively call into the same driver instance, and callback user contexts must outlive driver use.
- Public APIs may perform I2C and/or bounded command-spacing waits unless explicitly documented as cache-only helpers.
- `OFFLINE` is latched. Normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch the bus.
- `probe()` remains a raw diagnostic check and does not update health counters. `recover()` and explicit reset commands such as `startReinit()` and `startFactoryReset()` may access I2C while offline.

## Important Device Rules

- The sensor must be given up to 30 ms after power-up before the first command. `begin()` honors `Config::powerUpDelayMs` before probing the serial number.
- All commands are 16-bit, MSB-first. Every returned 16-bit word is followed by a CRC byte.
- During command execution, read attempts may NACK. This is normal busy behavior.
- `wake_up` is special: the device NACKs the command by design, then wakes within 30 ms. Only precise address/data NACK statuses are treated as expected wake-up behavior; generic I2C errors, timeouts, and bus faults remain tracked failures.
- While periodic measurement is active, only `read_measurement`, `get_data_ready_status`, `set_ambient_pressure`, `get_ambient_pressure`, and `stop_periodic_measurement` are valid.
- `stop_periodic_measurement` requires a 500 ms settle window before idle-only commands.
- Low-power periodic mode trades some CO2 accuracy for lower average current.
- In single-shot workflows, the first 1..3 CO2 results after power-up or mode changes may be unreliable. The driver reports RHT-only CO2 invalidity directly, but warm-up discard policy remains application-managed.
- `persist_settings` writes EEPROM. Sensirion rates this storage for at least 2000 write cycles, so persistence must remain explicit and infrequent.
- The sensor can draw 175-205 mA peak current during the photoacoustic pulse. Budget the supply accordingly and place at least 10 uF bulk capacitance near the device.

## Variant Support Boundary

This package is production-targeted at SCD41. `begin()` validates serial-number
variant bits as SCD41 by default. Setting `Config::strictVariantCheck=false` is a
diagnostic escape hatch that lets `begin()` complete on observed SCD40, SCD42,
SCD43, or unknown variants, but it does not enable family-device support.

When a non-SCD41 variant is initialized this way, known SCD41-only operations
return `UNSUPPORTED`: low-power periodic mode, single-shot CO2/RHT commands,
power-down and wake-up, ASC target, ASC initial/standard period commands, and
raw helper access to those known command words. Common SCD4x diagnostic and
configuration commands remain available where the local docs identify them as
shared.

## Public API

The public driver follows the same family conventions as the mature workspace libraries:

- lifecycle: `begin()`, `tick()`, `end()`, `probe()`, `recover()`
- measurement flow: `requestMeasurement()`, `measurementPending()`, `measurementReadyMs()`, `readMeasurement()`, `getMeasurement()`, `getLastMeasurement()`, `getRawSample()`, `getCompensatedSample()`, `hasSample()`, `hasFreshSample()`, `sampleStale()`, `sensorEpoch()`, `sampleEpoch()`
- state and health: `state()`, `driverState()`, `isOnline()`, `isBusy()`, `isPeriodicActive()`, `operatingMode()`, `pendingCommand()`, `commandReadyMs()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, `totalSuccess()`, `totalProtocolFailures()`, `totalCrcFailures()`, `consecutiveProtocolFailures()`, `lastProtocolErrorMs()`, `lastProtocolError()`
- measurement readiness and status: `lastSampleCo2Valid()`, `sampleTimestampMs()`, `sampleAgeMs()`, `missedSamplesEstimate()`, `readDataReadyStatus()`
- mode control: `setSingleShotMode()`, `startPeriodicMeasurement()`, `startLowPowerPeriodicMeasurement()`, `stopPeriodicMeasurement()`, `powerDown()`, `wakeUp()`
- mode/query helpers: `getSingleShotMode()`, `getIdentity()`, `readSerialNumber()`, `readSensorVariant()`
- named single-shot commands: `startSingleShotMeasurement()`, `startSingleShotRhtOnlyMeasurement()`
- compensation/config: `setTemperatureOffsetC()` (finite values only), `setSensorAltitudeM()`, `setAmbientPressurePa()`, ASC getters/setters
- maintenance: `startPersistSettings()`, `startReinit()`, `startFactoryReset()`, `startSelfTest()`, `getSelfTestResult()`, `getSelfTestRawResult()`, `startForcedRecalibration()`, `getForcedRecalibrationCorrectionPpm()`, `getForcedRecalibrationRawResult()`
- raw command access: `writeCommand()`, `writeCommandWithData()`, `readCommand()`, `readCommandUnsafe()`, `readWordCommand()`, `readWordsCommand()`
- snapshots: `getSettings()` for local driver state, including sample freshness/epoch fields, and `readSettings()` for a best-effort state plus live-configuration snapshot

`readSettings()` is mode-dependent by design:

- in idle mode it reads the live device configuration fields
- in periodic mode it refreshes ambient pressure only, because the other configuration commands are idle-only
- while a command is pending, a measurement is in progress, or the sensor is powered down, it returns the local driver snapshot without live refresh

The raw command helpers are limited to immediate, non-stateful diagnostic transactions. Managed mode transitions and long-running commands must still use the typed APIs, and the raw helpers continue to enforce the sensor's periodic-mode command restrictions. Raw read helpers reject buffers larger than the largest documented SCD41 response (9 bytes), invalid buffers, and locally invalid requests before touching I2C.

`readWordCommand()` and `readWordsCommand()` are the supported low-level helpers for word-returning SCD41 commands because they validate the Sensirion CRC byte on every returned 16-bit word. CRC mismatches return `CRC_MISMATCH` and update protocol telemetry: `totalProtocolFailures()`, `totalCrcFailures()`, `consecutiveProtocolFailures()`, `lastProtocolError()`, and `lastProtocolErrorMs()`. This telemetry is intentionally separate from I2C transport health, so CRC errors do not increment `totalFailures()` or move `DriverState` to `DEGRADED`/`OFFLINE`; a later successful CRC-checked word read resets only the consecutive protocol-failure counter.

`readCommand()` is an unvalidated diagnostic byte-read compatibility API. It rejects known word-returning SCD41 commands so production paths do not bypass CRC validation accidentally. `readCommandUnsafe()` is the explicit opt-in byte dump path for diagnostics; it does not validate CRCs and malformed payloads do not update protocol telemetry. Raw byte reads no longer map arbitrary read-header NACKs to `MEASUREMENT_NOT_READY`; that mapping is reserved for managed measurement contexts where the SCD41 protocol defines "not ready" as a valid outcome.

`writeCommand()` rejects known SCD41 word-payload and word-returning commands so callers do not send a valid command word with the wrong transaction shape. Use `writeCommandWithData()` for known one-word payload commands and the CRC-checked read helpers for known word responses. Unknown diagnostic command values remain available for lab use after the normal managed-command and periodic-mode guards pass.

Transport callbacks must treat `Status::Ok()` as an exact-transfer contract: a write callback accepted every requested byte, and a read callback filled every requested response byte. Short writes, zero-byte reads when bytes were requested, and short reads must return a non-OK status with the actual byte count in `Status::detail` when available.

Cached raw, fixed-point, and converted sample access is gated by whether any
sample has ever been cached (`hasSample()` / `SettingsSnapshot::hasSample`), not
by a nonzero timestamp. Freshness is separate: `hasFreshSample()` is true only
when the cached sample belongs to the current `sensorEpoch()`, and
`sampleStale()` is true when `reinit`, factory reset, or power-cycle recovery has
started a newer sensor epoch. `measurementReady()` and `getMeasurement()` expose
only fresh unconsumed samples. `getLastMeasurement()`, `getRawSample()`, and
`getCompensatedSample()` may still return a retained historical cache for
diagnostics; check the freshness accessors or `SettingsSnapshot` epoch fields
before treating it as current sensor output.

## Public API Latency Contract

Notation:

- `W` / `R`: one injected I2C write / read callback. Sensor read commands are shaped as one command write plus one read callback.
- `T`: `Config::i2cTimeoutMs`.
- `D`: `Config::commandDelayMs`; `Gprev` is any remaining guard before the first transaction, bounded by `D`.
- `RD`: the larger of the 1 ms short-command execution wait and `D`.
- `P`: `Config::powerUpDelayMs`.
- `N`: number of live fields read by `readSettings()`.

| API group | Class | I2C callbacks | Built-in wait or deadline | Worst-case success terms |
| --- | --- | ---: | --- | --- |
| Inline getters, health counters, conversion helpers, `sampleAgeMs()` | cache-only | 0 | none | CPU only |
| `getSettings()`, `getMeasurement()`, `getLastMeasurement()`, `getRawSample()`, `getCompensatedSample()`, result accessors | local/cache | 0 | none | CPU only |
| `begin()` | blocking startup | `1W + 1R` | waits `P`, then CRC-checked serial read | `P + Gprev + 2T + RD` |
| `tick()` before begin or no due work | scheduler | 0 | none | CPU only |
| `tick()` due state-only command (`STOP_PERIODIC`, `POWER_DOWN`, `WAKE_UP`, `PERSIST_SETTINGS`, `REINIT`, `FACTORY_RESET`, `POWER_CYCLE`) | tick-driven completion | 0 | deadline already elapsed | CPU only |
| `tick()` due self-test or FRC | tick-driven completion | `1R` | reads the result word after the 10 s / 400 ms deadline | `Gprev + T` |
| `tick()` due measurement, not ready | tick-driven completion | `1W + 1R` | data-ready read; reschedules `dataReadyRetryMs` | `Gprev + 2T + RD` |
| `tick()` due measurement, ready | tick-driven completion | `2W + 2R` | data-ready read plus `read_measurement` | `Gprev + 4T + 2RD + D` |
| `probe()` | diagnostic-only | `1W + 1R` | serial read, or data-ready read in periodic mode; drains post-probe `D` then restores driver spacing state | `Gprev + 2T + RD + D` |
| `recover()` | manual recovery | 0 to `2W + 2R`, plus optional callbacks | recover backoff; optional app `busReset`; optional app `powerCycle` schedules `P` | up to `Gprev + 4T + 2RD + D + busReset + powerCycle` before scheduled settle |
| `requestMeasurement()` in idle, `startSingleShotMeasurement()`, `startSingleShotRhtOnlyMeasurement()` | tick-driven start | `1W` | schedules 5 s or 50 ms execution | start call `Gprev + T`; completion through `tick()` |
| `requestMeasurement()` in periodic mode | local schedule | 0 | schedules the next fetch deadline | CPU only now; completion through `tick()` |
| `readMeasurement()` | mixed | 0, `1W + 1R`, or `2W + 2R` | consumes fresh cache, completes a due single-shot, or probes/fetches periodic data | same as due measurement path |
| `readDataReadyStatus()`, `readSerialNumber()`, identity/variant cache miss, typed live getters | short read | `1W + 1R` | 1 ms short-command wait plus command spacing | `Gprev + 2T + RD` |
| offset/altitude/pressure/ASC setters | short write | `1W` | payload write plus 1 ms short-command settle | `Gprev + T + 1 ms` |
| `startPeriodicMeasurement()`, `startLowPowerPeriodicMeasurement()` | mode start | `1W` | no long settle; periodic sample cadence is 5 s or 30 s | `Gprev + T` |
| `stopPeriodicMeasurement()` | tick-driven start | `1W` | schedules 500 ms settle before idle-only commands | start call `Gprev + T`; completion through `tick()` |
| `powerDown()` / `wakeUp()` | tick-driven start | `1W` | power-down schedules 1 ms; wake schedules `P` and accepts precise wake NACK | start call `Gprev + T`; completion through `tick()` |
| `startPersistSettings()` | EEPROM, tick-driven | `1W` | schedules 800 ms EEPROM settle | start call `Gprev + T`; explicit opt-in only |
| `startReinit()` | reset epoch, tick-driven | `1W` | schedules 30 ms settle and marks cached samples stale immediately | start call `Gprev + T`; completion through `tick()` |
| `startFactoryReset()` | destructive reset epoch, tick-driven | `1W` | schedules 1200 ms settle and marks cached samples stale immediately | start call `Gprev + T`; explicit opt-in only |
| `startSelfTest()` | tick-driven | `1W` | schedules 10 s result read | start call `Gprev + T`; due tick `Gprev + T` |
| `startForcedRecalibration()` | calibration, tick-driven | `1W` | schedules 400 ms result read | start call `Gprev + T`; due tick `Gprev + T` |
| `readSettings()` | blocking diagnostic live refresh | periodic: `N=1`; idle common fields: `N=4`; idle SCD41 fields: `N=7` | chained live reads; busy/power-down returns local cache only | `Gprev + 2N*T + N*RD + (N-1)*D` |
| raw `writeCommand()`, `writeCommandWithData()` | diagnostic write | `1W` | managed/long/stateful commands rejected | `Gprev + T` |
| raw `readCommand()`, `readCommandUnsafe()`, `readWordCommand()`, `readWordsCommand()` | diagnostic read | `1W + 1R` | 1 ms short-command wait plus command spacing; CRC checked for word helpers | `Gprev + 2T + RD` |

Long operations are modeled as bounded start/poll/read flows driven by `tick()` rather than blocking the public call for hundreds of milliseconds or seconds. Short synchronous wait guards also have a finite stalled-timebase escape path, so a broken injected clock/yield hook returns `TIMEOUT` instead of spinning indefinitely.

`begin()` remains an explicitly blocking compatibility startup API. `readSettings()` remains an explicitly blocking diagnostic live refresh. Neither is hidden inside the steady-state loop; latency-sensitive applications should call them during controlled startup or diagnostics windows.

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

Temperature-offset helpers use the datasheet scale factor `2^16 - 1`:

```text
temperature_offset_word = round(offset_degC * 65535 / 175)
temperature_offset_mdegC = round(word * 175000 / 65535)
```

## Installation

### PlatformIO

```ini
lib_deps =
  https://github.com/janhavelka/SCD41.git
```

### Manual

Copy [include/SCD41](include/SCD41) and [src](src) into your project.

### ESP-IDF Component

The repository root can be used as an ESP-IDF component through
`EXTRA_COMPONENT_DIRS` or your component manager workflow. The core driver owns
no I2C bus, pins, power rail, logging, or scheduler policy; applications provide
transport and timing callbacks through `SCD41::Config`.

The core component does not include Arduino or ESP-IDF framework headers. All
applications must inject `Config::nowMs` and `Config::nowUs`; scheduler-aware
applications should also inject `Config::cooperativeYield` so bounded waits can
yield cooperatively.

See [examples/idf/basic](examples/idf/basic) for an ESP-IDF v6-style
`i2c_master` adapter and native bring-up CLI with the same user-visible
command contract as the Arduino serial CLI.

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
  const size_t written = wire->write(data, len);
  const uint8_t result = wire->endTransmission(true);
  switch (result) {
    case 0: break;
    case 2: return SCD41::Status::Error(SCD41::Err::I2C_NACK_ADDR, "I2C NACK addr", result);
    case 3: return SCD41::Status::Error(SCD41::Err::I2C_NACK_DATA, "I2C NACK data", result);
    case 4: return SCD41::Status::Error(SCD41::Err::I2C_BUS, "I2C bus error", result);
    case 5: return SCD41::Status::Error(SCD41::Err::I2C_TIMEOUT, "I2C timeout", result);
    default: return SCD41::Status::Error(SCD41::Err::I2C_ERROR, "I2C write failed", result);
  }
  if (written != len) {
    return SCD41::Status::Error(SCD41::Err::I2C_ERROR, "I2C write incomplete",
                                static_cast<int32_t>(written));
  }
  return SCD41::Status::Ok();
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
  if (rxLen == 0) {
    return SCD41::Status::Ok();
  }
  const size_t received = wire->requestFrom(addr, rxLen);
  if (received == 0) {
    return SCD41::Status::Error(SCD41::Err::I2C_ERROR, "I2C read returned 0 bytes", 0);
  }
  if (received != rxLen) {
    for (size_t i = 0; i < received; ++i) {
      (void)wire->read();
    }
    return SCD41::Status::Error(SCD41::Err::I2C_ERROR, "I2C read incomplete",
                                static_cast<int32_t>(received));
  }
  for (size_t i = 0; i < rxLen; ++i) {
    rxData[i] = static_cast<uint8_t>(wire->read());
  }
  return SCD41::Status::Ok();
}

static uint32_t arduinoNowMs(void*) {
  return millis();
}

static uint32_t arduinoNowUs(void*) {
  return micros();
}

static void arduinoYield(void*) {
  yield();
}

SCD41::SCD41 sensor;

void setup() {
  Wire.begin(8, 9);
  Wire.setClock(400000);

  SCD41::Config cfg;
  cfg.i2cWrite = i2cWrite;
  cfg.i2cWriteRead = i2cWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = arduinoNowMs;
  cfg.nowUs = arduinoNowUs;
  cfg.cooperativeYield = arduinoYield;

  if (!sensor.begin(cfg).ok()) {
    return;
  }
}

void loop() {
  SCD41::Status tickSt = sensor.tick(arduinoNowMs(nullptr));
  if (!tickSt.ok() && tickSt.code != SCD41::Err::MEASUREMENT_NOT_READY) {
    Serial.printf("tick: %u %s\n", static_cast<unsigned>(tickSt.code), tickSt.msg);
  }

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

[examples/idf/basic](examples/idf/basic) provides a native ESP-IDF CLI project
that owns the I2C bus/device handles and wires them into the same transport
callback contract used by Arduino examples.

Main command groups:

- Common: `help`, `version`, `info`, `scan`, `begin`, `end`, `probe`, `recover`, `diag`, `demo`, `drv`, `drv1`, `state`, `cfg`, `settings`, `status`, `verbose`
- Measurement: `read`, `fetch`, `sample`, `last`, `raw`, `comp`, `dataready`, `watch`, `stress`, `single`, `single_start`, `convert`
- Mode and power: `mode`, `periodic`, `sleep`, `wake`
- Identity and compensation: `serial`, `variant`, `toffset`, `altitude`, `pressure`, `asc_enabled`, `asc_target`, `asc_initial`, `asc_standard`
- Maintenance: `persist confirm`, `reinit`, `factory_reset confirm`, `selftest`, `selftest_result`, `frc confirm <reference_ppm>`, `frc_result`
- Raw commands: `command write`, `command write_data`, `command read`, `command read_word`, `command read_words`

The Arduino and ESP-IDF examples intentionally expose the same command names,
aliases, help sections, prompts, colorized status/health output, diagnostics,
the safe one-sample `demo` workflow, maintenance workflows, measurement flows,
compensation controls, and raw command access. `read` follows the managed driver
path: it prints a sample immediately if one is already ready, or
it schedules a managed fetch when no sample is pending yet. `fetch` is the explicit direct-read path
for a sample that is expected to be ready now. `verbose` follows the family CLI convention and acts
as a toggle with no argument, or as an explicit setter with `0` / `1`. `status` is the concise
chip/runtime view for pending commands, timing, and live `get_data_ready_status`, while `sample` /
`last` expose the most recently cached converted, raw, and fixed-point sample without consuming it.
Deferred operations such as self-test, forced recalibration, wake-up, stop-periodic, reinit, and
factory reset now print explicit pending-work and completion summaries so the operator can follow the
chip state without polling internal flags manually.
Both CLI examples capture `Status st = tick(...)` on every loop pass and print non-OK async
completion statuses. `MEASUREMENT_NOT_READY` is intentionally quiet because it only means no new
sample or command result is available yet.

The raw command CLI is intentionally limited to immediate diagnostic commands. `command read` is an
explicit unsafe byte dump without CRC validation; use `command read_word` or `command read_words` for
CRC-checked word responses. Managed transitions such as periodic-mode entry/exit, wake-up, self-test,
and forced recalibration should be driven through the typed commands so the driver state stays coherent.

EEPROM-writing and persistent calibration commands require explicit operator
confirmation in both CLIs. Bare `persist`, `factory_reset`, and `frc` commands
refuse with the required confirmation form; only `persist confirm`,
`factory_reset confirm`, and `frc confirm <reference_ppm>` execute those
maintenance flows.

Typical bring-up flow:

```text
scan
drv
settings
periodic on
watch 1
watch 0
selftest
frc confirm 400
persist confirm
command read_word 0xE4B8
```

## Build And Validation

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack . -o dist
python tools/check_package_contents.py dist/*.tar.gz
```

In an ESP-IDF v6.0.1 environment, also build the native IDF example for both
supported targets:

```bash
idf.py -C examples/idf/basic -B build-esp32s3 set-target esp32s3
idf.py -C examples/idf/basic -B build-esp32s3 build
idf.py -C examples/idf/basic -B build-esp32s2 set-target esp32s2
idf.py -C examples/idf/basic -B build-esp32s2 build
```

Hardware/HIL validation remains a separate opt-in step because it requires real
boards, an SCD41, and explicit operator control for EEPROM/destructive commands.
Use [docs/SCD41_HARDWARE_VALIDATION.md](docs/SCD41_HARDWARE_VALIDATION.md) for
the manual matrix and optional runner workflow.

## Repository Notes

- Public headers live in <a href="include/SCD41">include/SCD41</a>
- Implementation lives in <a href="src">src</a>
- Version metadata is generated into `include/SCD41/Version.h` from <a href="library.json">library.json</a>; the generated header is tracked for clean package consumers and must not be edited by hand
- `examples/common` is example-only glue and is not installed as part of the library
- The library never configures I2C pins or owns the bus
- <a href="ASSUMPTIONS.md">ASSUMPTIONS.md</a> records the remaining SCD41-specific policy assumptions

## Documentation

- <a href="CHANGELOG.md">CHANGELOG.md</a> - release history
- <a href="AGENTS.md">AGENTS.md</a> - repository engineering rules
- <a href="ASSUMPTIONS.md">ASSUMPTIONS.md</a> - explicit assumptions and scope notes
- <a href="docs/IDF_PORT.md">docs/IDF_PORT.md</a> - ESP-IDF portability guidance
- <a href="docs/IDF_PORT_IMPLEMENTATION.md">docs/IDF_PORT_IMPLEMENTATION.md</a> - implemented IDF component/example notes
- <a href="docs/SCD41_HARDWARE_VALIDATION.md">docs/SCD41_HARDWARE_VALIDATION.md</a> - opt-in hardware/HIL matrix and evidence rules
- <a href="docs/SCD41_HARDENING_FINAL_REPORT.md">docs/SCD41_HARDENING_FINAL_REPORT.md</a> - final hardening disposition and release gate
- <a href="docs/SCD41_datasheet.md">docs/SCD41_datasheet.md</a> - datasheet-derived implementation reference

## License

MIT License. See [LICENSE](LICENSE).
