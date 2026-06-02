# SCD41 Hardware Validation Matrix

Last updated: 2026-06-02

This matrix defines optional hardware/HIL validation for the SCD41 library on
ESP32-S2 / ESP32-S3 using either:

- `examples/01_basic_bringup_cli` through Arduino/PlatformIO serial monitor; or
- `examples/idf/basic` through ESP-IDF monitor.

Both examples expose the same command contract. The examples own I2C pins, bus
setup, serial console, timing hooks, bus reset hooks, and power-cycle hooks. The
core library remains framework-neutral and does not own the bus.

Do not record a matrix row as passed without a raw transcript or log. If no
hardware is connected, mark the row `not run`.

## Evidence Format

For every hardware run record:

- board type and revision;
- SCD41 module/board and wiring;
- I2C SDA/SCL pins and pullups;
- supply voltage and any external power switching;
- firmware commit and build environment;
- serial transcript path;
- ambient reference notes, if any;
- operator and date/time.

The optional runner can collect evidence:

```bash
python tools/scd41_hil_runner.py --port COM7 --output-dir hil-results
```

The runner requires `pyserial`. It runs safe commands by default, writes a raw
transcript plus JSON/Markdown summaries, and stops on the first failed expected
response. A passing runner result is evidence only for the connected hardware
and environment recorded in that transcript.

## Common CLI Setup

Build and flash one of the example CLIs, open the serial monitor, and verify the
prompt is visible:

```text
help
version
cfg
```

Pass criteria:

- `help` prints the command list.
- `version` prints library version metadata.
- `cfg` shows `address: 0x62`, timing hooks present, and initialized state.

Fail criteria:

- no prompt after reset;
- missing timing hooks;
- wrong I2C address;
- repeated non-OK async status messages before any test action.

## Safe Smoke Matrix

Safe smoke tests must not write EEPROM, factory-reset calibration, or run forced
recalibration. Run them first on every board/sensor combination.

| ID | Purpose | Commands | Pass Criteria | Fail Criteria |
| --- | --- | --- | --- | --- |
| S-01 | Identify SCD41 at fixed address | `scan` | Output reports a device at `0x62`. | No device at `0x62`, scan hangs, or bus fault. |
| S-02 | Initialize or reinitialize driver | `begin` then `drv` | Status OK; driver state `READY`; failures unchanged or zero. | `NOT_INITIALIZED`, `DEVICE_NOT_FOUND`, `UNSUPPORTED`, `OFFLINE`, or growing failures. |
| S-03 | Read serial number | `serial` | Prints nonzero 12-hex-digit serial and variant `SCD41`. | Serial zero, CRC error, unsupported variant, or I2C error. |
| S-04 | Verify variant helper | `variant` | Prints `variant=SCD41`. | Any other variant unless this is an intentional non-SCD41 diagnostic run. |
| S-05 | Read idle data-ready status | `dataready` | Command returns status and usually `data_ready=no` before measurement starts. | I2C error, CRC mismatch, or driver busy when no work is pending. |
| S-06 | Start periodic measurement | `periodic on` then `status` | Periodic mode starts; status shows periodic or pending work. | Command rejected while idle, unexpected busy, or health failure. |
| S-07 | Wait for first periodic data-ready | wait at least 5 s, then `dataready` | Data-ready eventually becomes `yes` or a later `read` returns a sample. | Still no sample after two 5 s periods without explainable environment issue. |
| S-08 | Read periodic measurement | `read` or `fetch`, then `sample`, `raw`, `comp` | CO2 is plausible for environment, temperature and RH are plausible, raw/fixed-point output is coherent. | CRC mismatch, no sample after ready, implausible values, or stale sample flags. |
| S-09 | Continue periodic for N samples | `stress 10` or `watch 1`, then later `watch 0`, `drv` | All samples succeed or failures are explained; health remains `READY`; async errors are visible. | Hidden failures, `OFFLINE`, repeated `MEASUREMENT_NOT_READY` after cadence, or sample ages inconsistent with cadence. |
| S-10 | Stop periodic and verify settle | `periodic off`, wait at least 500 ms, then `status`, `settings` | Stop reports pending/in-progress then idle; idle-only settings work after settle. | Idle-only commands accepted before settle unexpectedly, or remain busy past settle window. |
| S-11 | Single-shot CO2/T/RH | `single full`, `single_start full`, wait at least 5 s, `read` | One full sample is returned; CO2 valid flag is true. | No sample, invalid CO2, CRC/I2C failure, or stale sample. |
| S-12 | Single-shot RHT-only | `single rht`, `single_start rht`, wait at least 50 ms, `read` | Temperature/RH sample returned; CO2 invalidity is visible/expected. | Full CO2 reported as valid for RHT-only, no sample, or I2C/CRC failure. |
| S-13 | Low-power periodic short run | `periodic lp`, wait at least 30 s, `read`, `periodic off` | Low-power sample returns and stop settles. | Low-power command rejected on SCD41, no sample after expected cadence, or stop never settles. |
| S-14 | Power down / wake / serial verify | `sleep`, wait 1 s, `wake`, wait at least 30 ms, `serial` | Wake completes; serial still reads nonzero SCD41. | Wake reports generic bus fault, health poisoned by expected NACK, or serial fails after wake. |
| S-15 | Manual recover after stop/start | `periodic on`, wait 5 s, `periodic off`, `recover`, `drv` | Recover is OK or no-op and driver remains usable. | Recover causes `OFFLINE`, clears valid config unexpectedly, or hides transport failure. |

Recommended safe smoke transcript command sequence:

```text
help
version
cfg
scan
begin
drv
serial
variant
dataready
diag
periodic on
status
# wait >= 5 s
dataready
read
sample
raw
comp
stress 10
periodic off
# wait >= 500 ms
status
settings
single full
single_start full
# wait >= 5 s
read
single rht
single_start rht
# wait >= 50 ms
read
periodic lp
# wait >= 30 s
read
periodic off
sleep
# wait >= 1 s
wake
# wait >= 30 ms
serial
recover
drv
```

Plausible sample ranges for smoke testing:

- CO2: 300..5000 ppm for ordinary indoor/outdoor tests; record context if
  outside this range.
- Temperature: -10..60 C unless the board is intentionally in a chamber.
- Relative humidity: 0..100 %.

These ranges are smoke criteria, not calibration proof.

## Timing And Soak Matrix

Timing and soak tests prove stability over repeated asynchronous operation.
They should record success counts, failure counts, async status output, and
health deltas.

| ID | Purpose | Commands | Pass Criteria | Fail Criteria |
| --- | --- | --- | --- | --- |
| T-01 | 5-minute periodic smoke | `periodic on`, `stress 60`, `periodic off`, `drv` | About 60 valid samples at 5 s cadence; zero unexpected failures; state `READY`. | Any hidden async failure, `OFFLINE`, repeated CRC/I2C failures, or sample cadence drift. |
| T-02 | 30-minute periodic soak | `periodic on`, `stress 360`, `periodic off`, `drv` | Sustained sample flow; total failures remain zero or explained; no memory/console instability. | Watchdog resets, lockups, growing failures, stale samples, or missed cadence beyond expected scheduling margin. |
| T-03 | Low-power periodic soak | `periodic lp`, manually collect 20+ samples with `read` or use a custom loop, `periodic off` | Sample cadence near 30 s; health stable. | No data after repeated low-power periods or stop settle failure. |
| T-04 | Repeated single-shot loop | Repeat `single_start full`, wait 5 s, `read` for 20+ iterations | Every command completes through `tick()`; no stale cached sample across iterations. | Stale sample, lost async error, or stuck pending command. |
| T-05 | Reset epoch freshness | Cache sample, then `reinit`, wait 30 ms, `sample`; optionally repeat with safe power-cycle support | Sample view reports stale/older epoch until a new sample is read. | Old sample presented as fresh after reinit/recover/power cycle. |
| T-06 | Async counters and health | Before/after `drv` around all soak runs | `totalSuccess` increases; `totalFailures` and protocol failures stay zero unless fault injected. | Counters inconsistent with transcript or failures not visible. |

## Fault And Recovery Matrix

Fault tests may require special hardware, a switchable sensor rail, a bus fault
fixture, or a fake transport/HIL shim. Run only tests that are safe for the
connected board and sensor.

| ID | Fault | Method | Pass Criteria | Fail Criteria |
| --- | --- | --- | --- | --- |
| F-01 | Missing device/address NACK | Disconnect sensor or use wrong address in a custom test fixture, then `begin`, `probe`, `recover` | Failure is explicit; normal tracked failures affect health; recovery succeeds after reconnect. | Silent success without device, permanent lockup, or hidden failure. |
| F-02 | Data NACK | Use a transport shim or hardware condition that can distinguish data NACK | Precise NACK maps to the expected `Status`; wake expected-NACK path does not poison health. | Generic bus errors are incorrectly treated as expected wake NACK. |
| F-03 | Bad CRC injection | Use native fake transport tests or a HIL shim that corrupts one CRC byte | `CRC_MISMATCH` is visible; protocol counters update; transport health policy remains as documented. | Bad CRC accepted as data or hidden from status. |
| F-04 | Truncated response | Use fake transport or a serial shim that returns short read with non-OK status | Transport returns non-OK; driver does not parse stale/partial data. | Partial data accepted under `Status::Ok()`. |
| F-05 | Safe unplug/replug | Stop measurement, disconnect sensor if hardware allows, run `probe`, reconnect, `recover`, `serial` | Failure visible while unplugged; recovery restores `READY` and serial. | Lockup, unsafe hotplug behavior, or unrecovered `OFFLINE`. |
| F-06 | Brownout/power-cycle | Use controlled sensor power switch, not random supply shorts | Driver sees fault, power-cycle hook or manual `recover` restores operation after settle. | Board reset loops, sensor does not recover, or old sample remains fresh. |
| F-07 | Bus reset hook | Application wiring supports bus reset callback | `recover` calls hook and records status; normal operation resumes. | Hook failure hidden or normal operations keep touching offline bus. |
| F-08 | Power-cycle hook | Application wiring supports sensor rail/reset callback | Power-cycle schedules settle; old sample is stale; later serial/sample works. | Settle skipped, stale sample marked fresh, or health incorrectly reset. |
| F-09 | Recover from `OFFLINE` | Inject failures until `OFFLINE`, then restore bus and call `recover` | Normal public operations are blocked while offline; `recover` is the manual path back. | Public I2C proceeds while offline or `OFFLINE` cannot recover with healthy bus. |

## Destructive / Opt-In Only Matrix

Destructive and EEPROM-backed tests are not part of safe smoke. They must be
run only with explicit operator approval and a transcript. Do not include them
in automated safe CI/HIL runs.

Risks:

- `persist_settings` writes EEPROM; Sensirion rates storage for at least 2000
  cycles, so repeated tests can consume wear budget.
- `factory_reset confirm` erases stored settings and calibration history.
- Forced recalibration changes calibration history and requires a known
  reference concentration.
- ASC persistence and temperature offset persistence can affect future samples.

The optional runner refuses destructive steps unless both are provided:

```bash
python tools/scd41_hil_runner.py --port COM7 --include-destructive --confirm-destructive "I understand EEPROM and calibration risk"
```

Manual destructive command matrix:

| ID | Purpose | Commands | Required Precondition | Pass Criteria | Fail Criteria |
| --- | --- | --- | --- | --- | --- |
| D-01 | Persist current settings | `settings`, `persist` (must refuse), `persist confirm`, wait 800 ms, `settings` | Operator approves one EEPROM write. | Bare `persist` refuses; confirmed persist completes; later settings readable. | Bare command writes EEPROM, persist loops, or health failure. |
| D-02 | Factory reset | `factory_reset` (must refuse), `factory_reset confirm`, wait 1200 ms, `serial`, `settings` | Operator accepts calibration/settings reset. | Bare command refuses; confirmed reset completes; serial remains valid; old sample is stale. | Bare command resets, serial lost, or stale sample marked fresh. |
| D-03 | Forced recalibration | `frc` (must refuse), `frc confirm <reference_ppm>`, wait 400 ms, `frc_result` | Stable reference gas and valid reference ppm. | Bare command refuses; confirmed FRC completes with valid correction or explicit documented failure sentinel. | FRC runs without confirmation or result is hidden. |
| D-04 | ASC setting persistence | `asc_enabled <0|1>`, `asc_target <ppm>`, `asc_initial <hours>`, `asc_standard <hours>`, `persist confirm`, power-cycle, `settings` | Operator accepts EEPROM write and future ASC behavior change. | Settings survive intended power cycle. | Settings lost unexpectedly or persist runs without confirmation. |
| D-05 | Temperature offset persistence | `toffset <degC>`, `persist confirm`, power-cycle, `toffset` | Offset is intentionally chosen and recorded. | Readback matches expected tolerance. | Offset persists unintentionally or cannot be restored. |

After destructive tests, restore known project defaults and record the final
settings snapshot.

## Final HIL Verdict Rules

- `HIL not run`: no connected hardware or no transcript. The correct readiness
  statement is: not yet field-grade; code is hardening-complete, hardware
  validation pending.
- `Safe smoke passed`: safe matrix has transcript evidence on at least one
  board/sensor combination. This is a bring-up confidence signal, not full
  production proof.
- `Soak/fault passed`: timing and recovery evidence exists for defined fixtures.
- `Destructive passed`: only valid when explicit operator approval and command
  refusals/confirmations are present in the transcript.

