# SCD41 Hardening Final Report

Date: 2026-06-02

## Summary Verdict

Current branch: `hardening/scd41-industry-readiness`

Code hardening status: hardening-complete for the findings identified in the
industry-readiness exploration, except for physical hardware/HIL evidence.

Merge verdict: recommended for merge as a hardened pre-production library once
CI passes on the pushed branch.

Release verdict: not yet field-grade; code is hardening-complete, hardware
validation pending. Do not tag a production/industry-grade release until at
least the safe hardware smoke matrix has recorded transcript evidence.

## Commit List

Hardening commits on this branch:

| Commit | Subject |
| --- | --- |
| `e2f7776` | Harden SCD41 core contracts and agent rules |
| `dcff765` | Fix SCD41 timing hooks and clock model |
| `59cbe93` | Expose SCD41 asynchronous tick completion status |
| `3a8bd25` | Harden SCD41 protocol error and raw helper contracts |
| `8d53bac` | Gate SCD41 variant APIs and protect destructive CLI commands |
| `9ff92fe` | Document and bound SCD41 latency and sample epochs |
| `77a0e5f` | Expand SCD41 tests CI guards and package validation |
| this commit | Document SCD41 HIL matrix and final hardening report |

## Finding Disposition

| Finding | Status | Evidence |
| --- | --- | --- |
| H-01 tick silent async failures | fixed | `tick()` now returns `Status`; async failures are retained through `lastAsyncStatus()` / `lastAsyncOperation()`; native tests cover single-shot timeout/CRC, self-test failure, FRC failure, and async success superseding failure. Evidence: `include/SCD41/SCD41.h`, `src/SCD41.cpp`, `test/test_basic.cpp`, README async status section. |
| H-02 timing hooks | fixed | `Config::nowMs` and `Config::nowUs` are required; README quick start and both examples wire timing/yield hooks; tests cover missing hooks and Arduino-style hooks. Evidence: `include/SCD41/Config.h`, `src/SCD41.cpp`, `examples/01_basic_bringup_cli/main.cpp`, `examples/idf/basic/main/main.cpp`, `test/test_basic.cpp`. |
| H-03 latency | documented / partial | Long commands are tick-driven; `tick()` processes at most one due completion; README documents public API latency classes and explicitly marks `begin()` and `readSettings()` as blocking startup/diagnostic APIs. Those two compatibility/diagnostic paths remain intentionally blocking. Evidence: README public API latency contract, `src/SCD41.cpp`, native timing tests. |
| H-04 clock model | fixed | Scheduling uses the injected clock domain and `tick()` samples configured time; divergent-clock and wraparound tests cover the contract. Evidence: README timing model, `src/SCD41.cpp`, `test/test_basic.cpp`. |
| H-05 ESP-IDF/HIL proof | CI fixed, HIL pending | CI now has ESP-IDF build matrix for `examples/idf/basic` on `esp32s3` and `esp32s2`; hardware/HIL matrix and optional runner are documented, but no hardware transcript exists in this repo. Evidence: `.github/workflows/ci.yml`, `docs/SCD41_HARDWARE_VALIDATION.md`, `tools/scd41_hil_runner.py`. |
| M-01 wake-up NACK | fixed | Wake-up accepts only precise address/data NACK as expected behavior; generic timeout/bus/error paths fail and track health. Evidence: README wake-up rule, `src/SCD41.cpp`, wake-up native tests. |
| M-02 raw helpers | fixed / documented | CRC-checked raw word helpers are public; unsafe byte reads are explicitly named and documented; known wrong command shapes are rejected. Evidence: `include/SCD41/SCD41.h`, README raw helper section, native raw helper tests. |
| M-03 truncation | fixed / documented | Transport exact-transfer contract is documented; Arduino example transport maps zero/short reads to non-OK status; native tests cover example short/zero reads. Evidence: README transport contract, `examples/common/I2cTransport.h`, `test/test_basic.cpp`. |
| M-04 variant gating | fixed | `strictVariantCheck=false` remains diagnostic only; known SCD41-only APIs and raw commands are gated on non-SCD41 variants. Evidence: README variant boundary, `src/SCD41.cpp`, native non-SCD41 tests. |
| M-05 stale samples | fixed | Driver tracks `sensorEpoch`/`sampleEpoch`; reinit, factory reset, and power-cycle recovery mark retained samples stale; cache APIs expose freshness. Evidence: `include/SCD41/SCD41.h`, `src/SCD41.cpp`, README cache freshness section, native stale-sample tests. |
| M-06 copy/move | fixed | `SCD41` copy/move constructors and assignments are deleted; native static assertions cover non-copyable/non-movable behavior. Evidence: `include/SCD41/SCD41.h`, `test/test_basic.cpp`. |
| M-07 no-data mapping | fixed | Raw byte reads no longer map arbitrary read-header NACKs to `MEASUREMENT_NOT_READY`; not-ready mapping is reserved for managed measurement contexts. Evidence: README raw read section, `src/SCD41.cpp`, native no-data tests. |
| M-08 tests | improved | Native suite is organized by runner sections, documents remaining narrow white-box use, and retains 82 passing tests. Evidence: `test/test_basic.cpp`, `test/README.md`. |
| M-09 Version.h/package | fixed | Generated `include/SCD41/Version.h` is tracked; CI runs version check and package-content proof requiring the header. Evidence: `.gitignore`, `include/SCD41/Version.h`, `scripts/generate_version.py`, `tools/check_package_contents.py`, `.github/workflows/ci.yml`. |
| M-10 destructive CLI | fixed | Arduino and IDF CLIs require `persist confirm`, `factory_reset confirm`, and `frc confirm <reference_ppm>`; contract scripts enforce parity and confirmation ordering. Evidence: both CLI examples, `tools/check_cli_contract.py`, `tools/check_idf_example_contract.py`, README CLI section. |
| L-01 CRC health | fixed / documented | CRC/protocol failures update separate protocol telemetry and do not poison I2C health; README documents the policy and tests cover CRC counters. Evidence: README raw/protocol telemetry section, `src/SCD41.cpp`, native CRC tests. |
| L-02 probe side effect | fixed / documented | `probe()` remains health-clean and restores command-spacing state while draining physical post-probe spacing; tests cover health and spacing behavior. Evidence: `src/SCD41.cpp`, README latency table, native probe tests. |
| L-03 Doxygen safety | fixed | Public header documents single-thread/no-ISR integration constraints and non-copyable semantics. Evidence: `include/SCD41/SCD41.h`. |
| L-04 validation docs | fixed | README validation now includes version check, guards, native tests, PlatformIO builds, package proof, and ESP-IDF local commands; HIL remains explicitly separate. Evidence: README Build And Validation section. |
| L-05 offset scale | fixed / verified | Temperature offset conversion uses datasheet scale with native vector tests for representative values and round-trip readback. Evidence: `src/SCD41.cpp`, `test/test_basic.cpp`. |
| L-06 guard script | improved | Core guard rejects framework includes/tokens, timing calls, heap allocation, and logging in `src/`/`include`; CLI/IDF guards enforce example boundaries and parity. Evidence: `tools/check_core_timing_guard.py`, `tools/check_cli_contract.py`, `tools/check_idf_example_contract.py`, CI guard job. |

## Public API Changes And Migration Notes

- `tick(uint32_t nowMs)` returns `Status`. Existing code that ignored the
  return value still compiles in normal C++, but applications should now inspect
  non-OK statuses except intentional `MEASUREMENT_NOT_READY` polling cases.
- `Config::nowMs` and `Config::nowUs` are required for initialized operation.
  Applications must provide monotonic callbacks from the same clock domain.
- `SCD41` is non-copyable and non-movable. Store one driver instance per
  physical sensor and do not pass it by value.
- `getLastMeasurement()`, raw sample getters, and compensated sample getters
  can expose retained historical samples; check `hasFreshSample()` or
  `sampleStale()` before using cached data as current sensor output.
- Raw byte reads are explicitly unsafe diagnostics. Prefer `readWordCommand()`
  and `readWordsCommand()` for CRC-checked word responses.

## Examples Changed

- Arduino bring-up CLI wires timing hooks and reports returned `tick()` status.
- Native ESP-IDF CLI mirrors the Arduino command contract with native IDF APIs.
- Both CLIs require confirmation for EEPROM/destructive/calibration commands.
- Both CLIs expose the safe command set used by the hardware validation matrix:
  `scan`, `begin`, `serial`, `variant`, `dataready`, `periodic`, `read`,
  `fetch`, `sample`, `stress`, `single`, `single_start`, `sleep`, `wake`,
  `recover`, and `drv`.

## Tests Added Or Improved

- Native test count: 82 passing test cases in the latest local run before this
  report chunk.
- New coverage across the hardening branch includes async completion errors,
  timing hook requirements, divergent-clock behavior, wake-up NACK policy,
  CRC/protocol telemetry, raw helper shape and CRC restrictions, variant gating,
  destructive CLI contract checks, stale sample epochs, probe side effects,
  package content proof, and guard-script policy checks.
- Test organization now has named runner sections and `test/README.md` explains
  why narrow white-box access remains.

## CI Status

CI configuration now includes:

- `guards`: version check, core guard, Arduino CLI contract, ESP-IDF example contract.
- `native-tests`: `python -m platformio test -e native`.
- `platformio-build`: `esp32s3dev` and `esp32s2dev`.
- `package`: package tarball plus `tools/check_package_contents.py`.
- `esp-idf-build`: `examples/idf/basic` for `esp32s3` and `esp32s2` using
  `espressif/esp-idf-ci-action@v1` with ESP-IDF `v6.0.1`.

CI pass status has not been observed inside this local session after the final
Prompt 08 commit. Treat the pushed GitHub workflow as the source of CI truth.

## Local Validation

Prompt 08 local validation results:

| Command | Result |
| --- | --- |
| `python scripts/generate_version.py check` | passed; `include\SCD41\Version.h` is up to date |
| `python tools/check_core_timing_guard.py` | passed; `Core timing guard PASSED` |
| `python tools/check_cli_contract.py` | passed; `CLI contract PASSED` |
| `python tools/check_idf_example_contract.py` | passed; `IDF example contract PASSED` |
| `python tools/scd41_hil_runner.py --help` | passed; help text printed |
| `python -m platformio test -e native` | passed; 82 test cases, 82 succeeded in `00:00:00.395` |
| `python -m platformio run -e esp32s3dev` | passed; `esp32s3dev` success in `00:00:01.207` |
| `python -m platformio run -e esp32s2dev` | passed; `esp32s2dev` success in `00:00:01.142` |
| `python -m platformio pkg pack` | passed; wrote `SCD41-0.1.0.tar.gz` |
| `python tools/check_package_contents.py SCD41-*.tar.gz` | passed; `Package content check PASSED: SCD41-0.1.0.tar.gz` |

Local ESP-IDF `idf.py` status: not run because `idf.py` is not on PATH in this
workspace (`CommandNotFoundException`).

## Hardware/HIL Status

No hardware/HIL commands were run in this session. No serial port, board, SCD41,
or operator-approved destructive test setup was provided. There is no raw
hardware transcript path to link.

Hardware validation is documented in `docs/SCD41_HARDWARE_VALIDATION.md`.
Optional evidence collection is available through `tools/scd41_hil_runner.py`.

## Remaining Future Work

- Run safe HIL smoke on at least one ESP32-S2 and one ESP32-S3 board with an
  SCD41 and save transcripts.
- Run longer soak and selected fault/recovery fixture tests before claiming
  field-grade readiness.
- Run destructive EEPROM/calibration/factory-reset tests only on approved
  hardware with explicit operator confirmation.
- Observe GitHub Actions pass after the final branch push.

## Final Release Gate

Minimum merge gate:

- local validation commands pass;
- CI passes on the pushed branch;
- no dirty worktree;
- final report and HIL matrix are committed.

Minimum production release gate:

- merge gate satisfied;
- safe HIL smoke transcript recorded on target hardware;
- at least one soak/recovery run recorded;
- destructive commands remain opt-in and are not run by default;
- CHANGELOG/README/ASSUMPTIONS updated for any release-scope behavior changes;
- `library.json` version updated and `include/SCD41/Version.h` regenerated;
- tag created only after evidence is reviewed.
