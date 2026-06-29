# SCD41 HIL Validation Report - COM8 - 2026-06-29

## Summary

This is a pre-HIL audit report. Live hardware validation was intentionally not
run because no board with an SCD41 sensor is currently attached.

| Result | Count |
| --- | ---: |
| PASS | 0 |
| FAIL | 0 |
| UNKNOWN | 0 |
| NOT RUN | 28 |

## Run Metadata

- Date/time: 2026-06-29, Europe/Prague
- Repository path: `c:\Users\Honza\Documents\Projects\SCD41`
- Branch: `hardening/scd41-industry-readiness`
- Commit: `7d891ca`
- Dirty status summary: source changes present from this pre-HIL pass; no
  generated package archive retained
- Serial port requested by prompt: `COM8`
- Baud rate planned: `115200`
- Firmware/application environment planned: PlatformIO Arduino `esp32s3dev` bring-up CLI
- Operating system observed: Microsoft Windows 11 Education
- Python observed: 3.12.10
- PlatformIO observed: 6.1.18
- Hardware setup and wiring: NOT RUN, no SCD41 fixture attached
- Detected device identity/address: NOT RUN
- Electrical limits and safety assumptions: no electrical tests performed

## Commands

Commands that can be run without hardware:

```powershell
python tools/scd41_hil_runner.py --parser-self-test
python tools/scd41_hil_runner.py --dry-run --port COM8 --output-dir hil-results
python tools/test_scd41_hil_runner.py
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack . -o dist/SCD41-package.tar.gz
python tools/check_package_contents.py dist/*.tar.gz
python tools/check_clean_consumer_compile.py dist/*.tar.gz
```

Live HIL command planned but not run:

```powershell
python tools/scd41_hil_runner.py --port COM8 --baud 115200 --timeout-s 5 --verbose --output-dir hil-results
```

Flash command planned but not run:

```powershell
python -m platformio run -e esp32s3dev -t upload --upload-port COM8
```

## Test Matrix

| ID | Feature Area | Command or Step | Expected Result | Observed Result | Elapsed | Status | Notes |
| --- | --- | --- | --- | --- | ---: | --- | --- |
| HIL-01 | boot | serial boot transcript | CLI prompt visible | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-02 | identity | `version` | firmware/library version | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-03 | bus | `scan` | device at `0x62` | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-04 | lifecycle | `begin` | initialized/OK | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-05 | probe | `probe` | raw diagnostic success without health mutation | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-06 | identity | `serial`, `variant` | SCD41 serial and variant | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-07 | settings | `cfg`, `settings` | config and settings snapshot | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-08 | health | `drv`, `status`, `diag` | READY, zero diagnostic failures | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-09 | readiness | `dataready` | bounded status output | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-10 | periodic | `periodic on`, `read`, `sample`, `periodic off` | 5 s cadence sample, clean stop | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-11 | raw/fixed output | `raw`, `comp` | cached raw and fixed-point data | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-12 | single shot | `single full`, `single_start full`, `read` | full one-shot sample after 5 s | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-13 | RHT shot | `single rht`, `single_start rht`, `read` | RHT sample after 50 ms, CO2 invalid | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-14 | low power | `periodic lp`, `read`, `periodic off` | 30 s cadence sample, clean stop | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-15 | power | `sleep`, `wake`, `serial` | wake completes and serial still valid | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-16 | recovery | `recover`, `drv` | bounded recovery or clear failure | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-17 | stress | `stress 10` | bounded summary with zero errors | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-18 | invalid input | invalid CLI command | visible error, no crash | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-19 | raw diagnostics | `command read_word 0xE4B8` | CRC-checked data-ready word | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-20 | self-test | `selftest`, `selftest_result` | bounded self-test result | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-21 | persistence guard | bare `persist` | refusal without confirmation | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-22 | FRC guard | bare `frc` | refusal without confirmation | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-23 | factory guard | bare `factory_reset` | refusal without confirmation | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-24 | fault fixture | missing-device behavior | precise visible error | no fixture | 0 s | NOT RUN | Fixture unavailable |
| HIL-25 | timing benchmark | sample-rate benchmark | bounded latency summary | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-26 | soak | 8-hour safe soak | full soak metrics | no board attached | 0 s | NOT RUN | Hardware unavailable |
| HIL-27 | destructive persistence | `persist confirm` | explicit operator-approved write | not attempted | 0 s | NOT RUN | Destructive and no hardware |
| HIL-28 | destructive calibration/reset | `frc confirm`, `factory_reset confirm` | explicit operator-approved operation | not attempted | 0 s | NOT RUN | Destructive and no hardware |

## Timing And Sampling

No timing, sampling, latency, or soak measurements were collected. All such
fields remain `NOT RUN` until a physical fixture is attached and a transcript is
captured.

## Failures And Anomalies

- HIL hardware is unavailable, so no hardware failures can be classified.
- Pre-HIL tooling gap identified: the runner needed no-hardware entry points and
  stronger report classification before it could be used cleanly in CI or during
  a future live session.

## Fixes Implemented During This Pre-HIL Pass

- Added HIL runner parser self-test and dry-run modes.
- Added bounded idle timeout, global per-step timeout override, verbose output,
  bounded stress-count selection, result counts, and `NOT RUN` report rows.
- Added runner metadata fields for board, fixture, and operator.
- Updated docs/CI validation commands to use an explicit package tarball path.
- Marked validation/HIL tooling as source-checkout-only.
- Made `poll()` use the configured driver clock for millisecond scheduling, so
  `poll()` and `tick()` use one coherent clock source.
- Made wake-up completion verify sensor identity before reporting async success.
- Reused strict recovery verification for direct and post-power-cycle recovery,
  keeping non-SCD41 identity failures offline.
- Updated direct no-data reads to refresh command spacing when the transport can
  prove a read-header/no-data NACK.
- Added native Wire-stub coverage for write NACK, bus, timeout, and short-write
  adapter failures.

## Local Verification Results

These checks do not use live hardware:

| Command | Result |
| --- | --- |
| `python scripts/generate_version.py check` | PASS |
| `python tools/check_core_timing_guard.py` | PASS |
| `python tools/check_cli_contract.py` | PASS |
| `python tools/check_idf_example_contract.py` | PASS |
| `python tools/test_scd41_hil_runner.py` | PASS |
| `python tools/scd41_hil_runner.py --parser-self-test` | PASS |
| `python tools/scd41_hil_runner.py --dry-run --port COM8 --output-dir hil-results-prehil --stress-count 1` | PASS, generated NOT-RUN summary then removed |
| `python -m platformio test -e native` | PASS, 108/108 |
| `python -m platformio run -e esp32s3dev` | PASS |
| `python -m platformio run -e esp32s2dev` | PASS |
| `python -m platformio pkg pack . -o dist/SCD41-package.tar.gz` | PASS, generated archive then removed |
| `python tools/check_package_contents.py dist/*.tar.gz` | PASS |
| `python tools/check_clean_consumer_compile.py dist/*.tar.gz` | PASS |
| `git diff --check` | PASS, CRLF warnings only |

## Remaining HIL Work

- Attach a supported ESP32-S2 or ESP32-S3 board with an SCD41 at I2C address
  `0x62`.
- Record wiring, pullups, supply voltage, module type, and operator.
- Build and flash the CLI firmware.
- Run the safe HIL runner sequence and capture transcript, JSON, and Markdown.
- Run longer stress/soak only after the safe smoke sequence passes.
- Do not run destructive commands unless explicitly approved for that fixture.
