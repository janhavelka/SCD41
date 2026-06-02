# SCD41 Industry-Readiness Exploration Report

Date: 2026-06-01  
Repository: `C:\Users\Honza\Documents\Projects\SCD41`  
Branch: `audit/scd41-industry-readiness-exploration`  
Commit: `886ab60`  
Worktree status: clean before report creation; report-only change adds this file.

## Executive Summary

- Current readiness classification: strong pre-production driver, not yet industry-grade release-ready.
- Main strengths: framework-neutral core, injected I2C transport, typed `Status` returns, SCD41 command table, CRC-protected typed word reads, managed long-command scheduling, health tracking, Arduino and native ESP-IDF examples, native unit tests, and useful guard scripts.
- Main blockers: silent asynchronous errors from `tick()`, broken/default timing contract in Arduino-facing usage, public blocking paths above the 1-2 ms target, inconsistent timing clocks, no local or CI ESP-IDF build proof, and no hardware/HIL validation evidence.
- Ready for implementation hardening: yes.
- Ready to merge as-is as production-grade: no.
- Ready to release as industry-grade: no.

This repository is SCD41-only by public package/API contract (`library.json:2`, `README.md:1-3`, `ASSUMPTIONS.md:7-9`). It is SCD4x-family-aware internally through `SensorVariant` and variant bits (`include/SCD41/SCD41.h:23-30`, `include/SCD41/CommandTable.h:89-94`), but default `Config::strictVariantCheck = true` keeps the public contract SCD41-only (`include/SCD41/Config.h:95`, `src/SCD41.cpp:159-160`).

## Repository Map

- Public headers: `include/SCD41/CommandTable.h`, `Config.h`, `Status.h`, `SCD41.h`, and generated/ignored `Version.h`.
- Implementation: `src/SCD41.cpp` and private fallback timing shim `src/PlatformTime.h`.
- Arduino example: `examples/01_basic_bringup_cli/main.cpp`.
- Arduino example-only glue: `examples/common/*.h`.
- Native ESP-IDF example: `examples/idf/basic/`, including native `app_main`, `driver/i2c_master.h`, and `IdfI2cTransport.*`.
- Tests: one native Unity test file, `test/test_basic.cpp`, with Arduino/Wire stubs under `test/stubs/`.
- Docs: `README.md`, `ASSUMPTIONS.md`, `CHANGELOG.md`, `docs/SCD41_datasheet.md`, extracted datasheet notes under `docs/extracted-md/`, and IDF port notes.
- CI/build metadata: `platformio.ini`, `library.json`, root `CMakeLists.txt`, `idf_component.yml`, `.github/workflows/ci.yml`.
- Tools/scripts: `scripts/generate_version.py`, `tools/check_core_timing_guard.py`, `tools/check_cli_contract.py`, `tools/check_idf_example_contract.py`.

## Public API Surface

- Lifecycle: `begin(const Config&)`, `tick(uint32_t nowMs)`, `end()`, `probe()`, `recover()` (`include/SCD41/SCD41.h:130-150`).
- State/health: `DriverState`, online/offline helpers, timestamps, failure counters (`include/SCD41/SCD41.h:16-21`, `154-180`).
- Measurement: `requestMeasurement()`, pending/ready helpers, `readMeasurement()`, cached sample accessors, raw/fixed-point accessors, data-ready reads (`include/SCD41/SCD41.h:184-232`).
- Modes: periodic, low-power periodic, single-shot, RHT-only single-shot, power-down, wake-up, serial/variant identity (`include/SCD41/SCD41.h:236-263`).
- Configuration/calibration: temperature offset, altitude, ambient pressure, ASC enable/target/periods (`include/SCD41/SCD41.h:267-302`).
- Maintenance/destructive: persist settings, reinit, factory reset, self-test, forced recalibration (`include/SCD41/SCD41.h:306-333`).
- Raw helpers: `writeCommand`, `writeCommandWithData`, `readCommand`, `readWordCommand`, `readWordsCommand` (`include/SCD41/SCD41.h:339-351`).
- Error model: `Err` covers OK, config, init, I2C fault classes, CRC, not ready, busy, in-progress, command failed, unsupported (`include/SCD41/Status.h:10-30`).

## Architecture Assessment

- Framework neutrality: core `include/` and `src/` do not include Arduino, Wire, ESP-IDF, FreeRTOS, `String`, logging, or heap-heavy STL. `python tools/check_core_timing_guard.py` passed.
- Transport injection: `Config` carries write/read callbacks, user context, optional bus reset and power cycle hooks, time hooks, and transport capabilities (`include/SCD41/Config.h:31-96`).
- I2C ownership: library never owns the bus; examples configure the bus outside the core.
- Timeouts: transport timeout is propagated to callbacks (`src/SCD41.cpp:1385-1403`), but driver-side timing uses busy waits and optional clocks.
- Memory allocation: no core heap allocation found in `src/` or `include/`.
- Copy/move semantics: `SCD41` does not delete copy/move constructors or assignment despite owning live sensor state and callback context (`include/SCD41/SCD41.h:123-505`). This can duplicate a driver instance accidentally.
- Thread/ISR safety: README documents single-thread/no-ISR usage (`README.md:51-56`), but public Doxygen on the class does not carry that contract.

## SCD41 Protocol Correctness

Strong typed-path coverage:

- Fixed address `0x62` and CRC constants are defined in `CommandTable.h` (`include/SCD41/CommandTable.h:16-18`).
- Commands are 16-bit MSB-first in `_writeCommand()` and `_writeCommandWithData()` (`src/SCD41.cpp:1459-1499`).
- Written payload words append CRC (`src/SCD41.cpp:1485-1491`).
- Typed returned words are CRC-checked in `_readWords()` and `_readWordsOnly()` (`src/SCD41.cpp:1565-1573`, `1597-1605`).
- `begin()` reads serial and rejects non-SCD41 when strict variant checking is on (`src/SCD41.cpp:145-160`).
- `get_data_ready_status` uses `(raw & 0x07FF) != 0` (`include/SCD41/SCD41.h:354-356`, `include/SCD41/CommandTable.h:84`).
- Measurements convert CO2 directly, temperature as `-45 + 175 * raw / 65535`, and RH as `100 * raw / 65535` (`src/SCD41.cpp:1284-1297`).
- Stop-periodic schedules a 500 ms settle (`src/SCD41.cpp:591-598`).
- Wake-up uses an expected-NACK path (`src/SCD41.cpp:617-632`).

Protocol risks:

- Raw `readCommand()` returns bytes without CRC validation and raw `writeCommand()` can send non-managed two-byte commands that may not match valid transaction shape (`src/SCD41.cpp:1212-1259`).
- Short/malformed response length is delegated to transport callbacks because the core callback API has no actual byte-count return (`include/SCD41/Config.h:35-41`, `src/SCD41.cpp:1518-1539`).
- Wake-up expected-NACK handling maps generic `I2C_ERROR` to success, not only precise NACK (`src/SCD41.cpp:1414-1428`).
- CRC failures return `CRC_MISMATCH` but do not affect health counters or last error (`src/SCD41.cpp:1565-1573`, `1633-1665`, `1965-1968`).

## Data Conversion and Units

- CO2: raw word directly cached as ppm (`src/SCD41.cpp:404-414`, `1828-1835`).
- Temperature float: correct contract formula (`src/SCD41.cpp:1284-1286`).
- Humidity float: correct contract formula (`src/SCD41.cpp:1288-1290`).
- Fixed-point helpers: match required formulas (`src/SCD41.cpp:1292-1297`).
- Temperature offset: range 0..20000 mdegC, encoded/decoded by helper functions (`src/SCD41.cpp:710-727`, `1300-1327`). Local extracted docs mention a 65536-style offset scale; code uses 65535, which is a small mismatch to review against the official source (`docs/SCD41_datasheet.md:368-375`).
- Altitude: 0..3000 m (`include/SCD41/CommandTable.h:100-101`, `src/SCD41.cpp:748-764`).
- Ambient pressure: 70000..120000 Pa, encoded as Pa / 100 (`include/SCD41/CommandTable.h:102-106`, `src/SCD41.cpp:785-817`, `1330-1336`).
- FRC: reference ppm range 1..40000; correction decoded as `word - 0x8000`, with `0xFFFF` failure sentinel (`src/SCD41.cpp:1061-1083`, `1915-1934`).
- ASC periods: multiples of 4 h enforced (`src/SCD41.cpp:892-939`).

## Timing and Latency Model

- `begin()`: synchronously waits `powerUpDelayMs` (default 30 ms), then serial read (`src/SCD41.cpp:145-151`).
- Short setters: write command and synchronously wait 1 ms, for example temperature offset (`src/SCD41.cpp:722-727`).
- Read helpers: write command, wait 1 ms, then read (`src/SCD41.cpp:1504-1515`).
- Long operations: scheduled through `PendingCommand` and completed by `tick()` (`include/SCD41/SCD41.h:41-54`, `src/SCD41.cpp:1840-1958`).
- `readSettings()` can issue seven live read commands in idle mode (`src/SCD41.cpp:1156-1210`).
- `tick()` is not constant-time when a completion is due; it may perform I2C reads for measurement, self-test, or FRC (`src/SCD41.cpp:169-181`, `1898-1958`).
- Deadlines are scheduled with `_nowMs()` but compared with caller-supplied `tick(nowMs)` (`src/SCD41.cpp:169-181`, `1785-1788`, `1998-2004`). If those clocks differ, completion timing can drift.
- Fallback time returns constant zero and fallback yield is empty (`src/PlatformTime.h:10-21`). With default timing hooks and nonzero waits, waits exit through timeout/stall guards rather than actual time passing.

## Health, Recovery, and Fault Behavior

- Health model has READY/DEGRADED/OFFLINE with consecutive failures and lifetime counters (`include/SCD41/SCD41.h:16-21`, `src/SCD41.cpp:1633-1665`).
- Tracked wrappers are the only normal path that calls `_updateHealth()` (`src/SCD41.cpp:1406-1450`).
- `probe()` uses raw access and avoids health updates, but it still mutates command-spacing state through command helpers (`src/SCD41.cpp:201-233`, `1459-1476`).
- `recover()` allows I2C while offline and may schedule a power-cycle settle (`src/SCD41.cpp:236-304`).
- OFFLINE is latched for normal public I2C operations (`src/SCD41.cpp:1676-1681`), but reset/recovery exception paths can record a successful write before post-reset verification.
- `tick()` discards completion `Status`, so asynchronous CRC/I2C/not-ready failures can be lost to the application (`src/SCD41.cpp:169-181`).
- Self-test and FRC store result status for explicit getters, but `tick()` still does not return the completion failure (`src/SCD41.cpp:1027-1103`, `1898-1934`).
- Cached samples can survive reinit/factory-reset/power-cycle completions because those paths clear pending state but not cached readiness/sample epoch (`src/SCD41.cpp:1840-1878`).

## Tests and CI Coverage

Existing:

- Native Unity test file with 44 tests (`test/test_basic.cpp:1137-1180`).
- Tests cover config defaults, CRC helper vector, conversions, begin variant check, single-shot, periodic, low-power periodic guard, ambient pressure during periodic, command delay stall, wake-up expected NACK, raw helper restrictions, health transitions, self-test, FRC, settings snapshot, and example transport zero-byte read behavior.
- CI builds Arduino ESP32-S3/S2 via PlatformIO, runs native tests, runs core timing guard, CLI contract, and package pack (`.github/workflows/ci.yml:15-92`).
- Guard scripts exist for core framework/timing usage and Arduino/IDF CLI contract (`tools/check_core_timing_guard.py`, `tools/check_cli_contract.py`, `tools/check_idf_example_contract.py`).

Missing or weak:

- No CI ESP-IDF `idf.py` build despite `library.json` declaring `espidf` (`library.json:43-46`).
- `tools/check_idf_example_contract.py` is documented in README validation (`README.md:301-311`) but is not wired into CI (`.github/workflows/ci.yml:69-92`).
- No local ESP-IDF validation because `idf.py` is not installed.
- No hardware/HIL test results.
- No dedicated CRC mismatch read test that proves `CRC_MISMATCH` behavior through public typed reads; tests currently cover the CRC helper and status enum only (`test/test_basic.cpp:219-231`).
- No truncated-response test for custom transport behavior.
- No test proving Arduino bring-up config installs time hooks; it currently does not.
- No copy/move prevention test.
- No black-box tests for `tick()` error surfacing because `tick()` is `void`.
- No tests for default omitted timing hooks, data-ready false/high-bits-only cases, wrap-around timing, `recoverBackoffMs`, `busReset`, `powerCycle`, negative wake/stop preconditions, or direct valid/invalid coverage for altitude and ASC APIs.

## Documentation and Examples

Strong:

- README accurately states the intended architecture and important SCD41 rules (`README.md:7-68`).
- Datasheet-derived implementation reference exists locally (`docs/SCD41_datasheet.md`).
- EEPROM warnings are present in README and datasheet notes (`README.md:68`, `docs/SCD41_datasheet.md:439-450`, `577-580`).
- IDF port docs explicitly say hardware and IDF build validation remain pending (`docs/IDF_PORT.md:220-247`, `docs/IDF_PORT_IMPLEMENTATION.md:65-75`).
- CLI commands cover diagnostics, mode control, settings, maintenance, raw commands, stress, and watch flows.

Weak:

- README quick start does not set `Config::nowMs`, `nowUs`, or `cooperativeYield` before `begin()` (`README.md:213-223`, `228`).
- Arduino CLI example also does not set timing hooks before `device.begin(gConfig)` (`examples/01_basic_bringup_cli/main.cpp:2064-2071`).
- Public Doxygen header does not carry thread/ISR safety or copy/move contract.
- README validation commands omit `pio pkg pack` and native `idf.py` builds (`README.md:301-311`).
- Destructive CLI commands are explicit, but a full opt-in hardware test matrix is not yet present as executable docs.
- CLI `persist` and `factory_reset` commands execute directly in both Arduino and IDF examples without a confirmation token (`examples/01_basic_bringup_cli/main.cpp:1868-1888`, `examples/idf/basic/main/main.cpp:2426-2446`).
- `include/SCD41/Version.h` is ignored/untracked (`.gitignore:41`) while public `SCD41.h` includes it (`include/SCD41/SCD41.h:11`); local generation passed, but CI does not explicitly run `generate_version.py check` or inspect package contents.

## Findings

### High Severity

H-01 - `tick()` silently drops fallible completion results  
Evidence: `tick()` is `void` (`include/SCD41/SCD41.h:136`) and discards `_handlePendingCommand()` / `_completeMeasurement()` return values (`src/SCD41.cpp:169-181`). Completion paths can return CRC/I2C errors (`src/SCD41.cpp:1898-1958`).  
Impact: asynchronous measurement/self-test/FRC/read failures can disappear unless they happen to affect health or a later getter retains status. This violates the "silent failure is unacceptable" rule.  
Recommended fix direction: expose a completion status through `tick()` return value, a `lastOperationStatus`, a pending-completion event, or explicit per-command result state for all scheduled operations.  
Tests needed: inject CRC/I2C errors during single-shot completion, self-test completion, FRC completion, and periodic fetch from `tick()` and assert visible status.

H-02 - Default timing contract breaks Arduino-facing usage  
Evidence: timing hooks default null (`include/SCD41/Config.h:77-79`), fallback time is constant zero (`src/PlatformTime.h:10-21`), `begin()` waits default 30 ms (`include/SCD41/Config.h:90`, `src/SCD41.cpp:145`), README quick start omits hooks (`README.md:213-223`), and Arduino CLI setup omits hooks (`examples/01_basic_bringup_cli/main.cpp:2064-2071`).  
Impact: default Arduino quick start and CLI can fail `begin()` via `TIMEOUT`/`Delay stalled` instead of initializing the sensor. This is field-visible and directly undermines the primary Arduino target.  
Recommended fix direction: require timing hooks in `Config` validation, or provide Arduino example hooks, or implement a documented platform adapter outside the core.  
Tests needed: native test for default timing hooks with nonzero `powerUpDelayMs`; example/contract test requiring hooks in Arduino config.

H-03 - Public API still has blocking and variable-latency paths above the target  
Evidence: `begin()` blocks for `powerUpDelayMs` (`src/SCD41.cpp:145`), `readSettings()` chains multiple live reads (`src/SCD41.cpp:1156-1210`), and due `tick()` calls can perform I2C completion reads (`src/SCD41.cpp:1840-1958`). Short setters/read helpers also contain 1 ms waits (`src/SCD41.cpp:727`, `1504-1515`); those are borderline with the stated 1-2 ms target, while the larger issue is the 30 ms begin path and chained/bulk transactions.  
Impact: the core does not fully satisfy the repository rule that waits over about 1-2 ms are split into tick-driven phases, and worst-case public latency is not fully documented.  
Recommended fix direction: make `begin()` and bulk readback state-machine driven or document them as explicit blocking diagnostics; separate "snapshot" from "live refresh"; make completion work bounded and report progress.  
Tests needed: public API latency contract tests with fake clocks and transport delays.

H-04 - Scheduled deadlines can use inconsistent clocks  
Evidence: deadlines are scheduled using `_nowMs()` (`src/SCD41.cpp:1785-1788`) while `tick(nowMs)` compares against the caller argument (`src/SCD41.cpp:169-181`).  
Impact: if `Config::nowMs` and the application's `tick()` argument differ, long-command completion can be early, late, or never. This affects single-shot, stop-periodic settle, wake-up, self-test, FRC, and power-cycle timing.  
Recommended fix direction: use one time source consistently; either make `tick()` own all scheduling time via its argument or require all scheduling to use injected `nowMs` and make `tick()` parameterless.  
Tests needed: fake-clock tests with deliberately divergent `Config::nowMs` and `tick(nowMs)`.

H-05 - ESP-IDF and hardware validation are not proven  
Evidence: package declares `espidf` (`library.json:43-46`) and has IDF component/example files, but CI has no native ESP-IDF `idf.py` build job (`.github/workflows/ci.yml:15-92`). The IDF parity checker exists and is listed in README validation (`README.md:301-311`) but is not in CI (`.github/workflows/ci.yml:69-92`). Local `idf.py --version` failed because `idf.py` is not installed. Docs still mark IDF build and hardware validation pending (`docs/IDF_PORT.md:220-247`, `docs/IDF_PORT_IMPLEMENTATION.md:65-75`).  
Impact: ESP-IDF compatibility and real-device timing/recovery behavior are claims without build/HIL evidence.  
Recommended fix direction: add ESP-IDF CI for `examples/idf/basic` on ESP32-S2/S3 and a documented hardware validation matrix with opt-in destructive tests.  
Tests needed: `idf.py` builds, smoke hardware, fault injection, unplug/replug, power cycle, wake-up, stop-periodic, and destructive opt-in tests.

### Medium Severity

M-01 - Wake-up expected-NACK handling masks generic I2C errors  
Evidence: `wakeUp()` passes `allowExpectedNack=true` (`src/SCD41.cpp:628`), and `_i2cWriteTrackedAllowExpectedNack()` converts `I2C_NACK_ADDR`, `I2C_NACK_DATA`, and generic `I2C_ERROR` to success (`src/SCD41.cpp:1414-1428`).  
Impact: transports that report many failures as `I2C_ERROR` can hide real bus faults and reset health to READY.  
Recommended fix direction: only accept precise NACK classes as expected; require a transport capability before suppressing data/address NACK if needed.  
Tests needed: wake-up with NACK succeeds; wake-up with timeout/bus/generic error fails and updates health.

M-02 - Raw helpers can bypass command shape and CRC policy  
Evidence: `writeCommand()` and `readCommand()` allow arbitrary non-managed commands; `readCommand()` returns raw bytes without CRC validation (`src/SCD41.cpp:1212-1259`). Docs require CRC validation for returned words (`docs/SCD41_datasheet.md:544-548`).  
Impact: diagnostic users can accidentally perform invalid command shapes or consume unchecked word responses.  
Recommended fix direction: make raw helpers clearly unsafe diagnostics, add typed raw-word variants as preferred APIs, and optionally require an explicit unsafe flag for unvalidated byte reads.  
Tests needed: raw command shape restrictions for known word commands and CRC-checked raw-word reads.

M-03 - Short/truncated response detection is a transport-only responsibility  
Evidence: `I2cWriteReadFn` has no actual byte-count return (`include/SCD41/Config.h:35-41`) and `_readOnly()` trusts `Status::Ok()` as complete (`src/SCD41.cpp:1518-1539`). Arduino example transport checks short reads (`examples/common/I2cTransport.h:76-85`), but custom transports might not.  
Impact: incomplete buffers can be parsed as stale/zero data if a user transport returns OK incorrectly.  
Recommended fix direction: strengthen transport contract docs, add adapter tests, or redesign callback to return count.  
Tests needed: fake transport returning OK with partial-fill behavior should be impossible or detected.

M-04 - `strictVariantCheck=false` exposes inconsistent SCD4x-family behavior  
Evidence: low-power, single-shot, and ASC target are gated (`src/SCD41.cpp:564`, `1760`, `859`), but power-down/wake-up and ASC period commands are not (`src/SCD41.cpp:601-632`, `892-951`). Local docs identify those as SCD41-only (`docs/extracted-md/08_variant_differences_and_open_questions.md:20`).  
Impact: users disabling strict checks can issue unsupported commands to SCD40/SCD42/SCD43.  
Recommended fix direction: either remove/privatize family mode or gate every SCD41-only command consistently.  
Tests needed: non-SCD41 variant tests for every SCD41-only API.

M-05 - Cached samples can survive reset/recovery epochs  
Evidence: `_storeSample()` sets `_hasSample` and `_measurementReady` (`src/SCD41.cpp:1821-1836`), while reinit/factory-reset/power-cycle completion only clears pending and sets idle (`src/SCD41.cpp:1869-1878`).  
Impact: an old sample can remain available after a sensor epoch change unless the application checks age.  
Recommended fix direction: clear ready/sample freshness on reset/power-cycle/factory-reset, or add a sample epoch/stale flag.  
Tests needed: cached sample before reinit/factory-reset/power-cycle becomes stale or unavailable after completion.

M-06 - Implicit copy/move constructors can duplicate live driver state  
Evidence: `class SCD41` owns transport config and runtime state (`include/SCD41/SCD41.h:123-505`) and does not delete copy/move operations.  
Impact: accidental copies can drive the same physical sensor with duplicated pending/health/cache state.  
Recommended fix direction: delete copy/move by default; add explicit `reset`/`begin` semantics for new instances.  
Tests needed: compile-time static assertions that the driver is non-copyable/non-movable.

M-07 - Busy/not-ready mapping is exposed too broadly through raw API  
Evidence: public `readCommand(..., allowNoData=true)` can map read-header NACK to `MEASUREMENT_NOT_READY` for arbitrary commands before health update (`include/SCD41/SCD41.h:340`, `src/SCD41.cpp:1439-1450`).  
Impact: weakens the rule that not-ready is only returned when command context proves that interpretation.  
Recommended fix direction: restrict `allowNoData` to `read_measurement` and data-ready-related managed contexts, or require a command whitelist.  
Tests needed: arbitrary raw read-header NACK remains an I2C failure; valid measurement no-data maps to not-ready.

M-08 - Test coverage is broad but still too monolithic and white-box  
Evidence: all native unit tests live in one file and use `#define private public` (`test/test_basic.cpp:16-19`).  
Impact: useful implementation coverage exists, but public-contract regressions may be hidden by white-box access and lack of black-box transport tests.  
Recommended fix direction: split protocol, state-machine, health, transport, and public API tests; minimize private access.  
Tests needed: black-box public API suites plus targeted private helper tests only where justified.

M-09 - Generated `Version.h` is a package-consumer risk  
Evidence: public `SCD41.h` includes `SCD41/Version.h` (`include/SCD41/SCD41.h:11`), but `.gitignore` excludes `include/SCD41/Version.h` (`.gitignore:41`) and `git status --ignored -- include/SCD41/Version.h` reports it as ignored. CI builds run PlatformIO extra scripts, and CI runs `pio pkg pack`, but it does not explicitly run `generate_version.py check` or inspect package contents.  
Impact: clean consumers or non-PlatformIO package flows may fail if the generated header is missing from the package or not generated before compile.  
Recommended fix direction: either commit generated `Version.h`, include a generated-header check/package-content inspection in CI, or make all supported build systems generate it before compile.  
Tests needed: build/package from a clean clone without preexisting ignored `Version.h`, then compile a consumer including `SCD41/SCD41.h`.

M-10 - Destructive/EEPROM CLI commands lack operator confirmation  
Evidence: README warns that `persist_settings` writes EEPROM (`README.md:68`), but Arduino CLI `persist` and `factory_reset` immediately call the driver (`examples/01_basic_bringup_cli/main.cpp:1868-1888`), and the IDF CLI mirrors that behavior (`examples/idf/basic/main/main.cpp:2426-2446`).  
Impact: examples can accidentally wear EEPROM or factory-reset calibration state during manual exploration.  
Recommended fix direction: require a confirmation token such as `persist confirm` / `factory_reset confirm`, and keep destructive commands out of automated demo/stress flows.  
Tests needed: CLI contract tests proving destructive commands reject missing confirmation.

### Low Severity

L-01 - CRC mismatch does not degrade health  
Evidence: CRC mismatch returns `CRC_MISMATCH` (`src/SCD41.cpp:1565-1573`, `1597-1605`), but `_isI2cFailure()` excludes CRC (`src/SCD41.cpp:1965-1968`).  
Impact: repeated CRC failures do not move driver health to degraded/offline. This may be intentional, but needs a documented policy.  
Recommended fix direction: document CRC as protocol-not-health or add separate protocol fault counters.  
Tests needed: CRC storm behavior and telemetry.

L-02 - `probe()` is health-clean but not completely side-effect-free  
Evidence: `probe()` uses raw reads (`src/SCD41.cpp:201-233`), but command helpers still set `_lastCommandUs` / `_lastCommandValid` (`src/SCD41.cpp:1459-1476`).  
Impact: diagnostic probe affects later command-spacing behavior.  
Recommended fix direction: either document this minor side effect or add raw probe helpers that do not update command-spacing state.  
Tests needed: probe does not alter health; command spacing after probe is defined.

L-03 - Public header lacks thread/ISR safety Doxygen  
Evidence: README documents the contract (`README.md:51-56`), but `include/SCD41/SCD41.h` class comments do not.  
Impact: generated API docs may omit an important integration constraint.  
Recommended fix direction: add Doxygen note to `SCD41` class and I2C-backed APIs.  
Tests needed: doc/tooling check if desired.

L-04 - README validation commands are incomplete  
Evidence: README lists guard scripts and PlatformIO builds/tests (`README.md:301-311`) but omits `pio pkg pack` and `idf.py` builds.  
Impact: contributors can miss package and ESP-IDF validation.  
Recommended fix direction: update validation docs after CI support exists.  
Tests needed: docs consistency check.

L-05 - Temperature-offset scale should be rechecked  
Evidence: local docs describe offset conversion with 65536-style scaling (`docs/SCD41_datasheet.md:368-375`), while code uses 65535 (`src/SCD41.cpp:1309-1327`).  
Impact: very small numeric difference, but protocol math should match the official datasheet exactly.  
Recommended fix direction: confirm against official Sensirion source and add boundary/vector tests.  
Tests needed: known vectors for 0, default 4 C, 20 C, and readback round-trip.

L-06 - Core guard script is useful but not exhaustive  
Evidence: `tools/check_core_timing_guard.py` scans `src`/`include` for selected framework includes and timing calls, but does not cover every policy class such as `delay()`, heap calls, logging tokens, or macro constants.  
Impact: future regressions could pass the guard while still violating AGENTS policy.  
Recommended fix direction: expand guard coverage or add separate policy checks for heap/logging/macros.  
Tests needed: negative fixtures or deliberate-policy regression tests for the guard script.

## Industry-Grade Exit Criteria

- `tick()` or an equivalent result channel exposes every asynchronous completion failure.
- Arduino quick start and Arduino CLI initialize timing hooks or `Config` rejects missing required hooks.
- No public API hides waits above the documented latency class; long/bulk operations are tick-driven or explicitly blocking diagnostics.
- One clock model controls scheduling and completion.
- SCD41-only APIs are consistently gated when strict variant checking is disabled, or family mode is removed.
- Expected wake-up NACK only suppresses precise, documented NACKs.
- Raw helpers cannot accidentally bypass CRC/shape expectations without explicit unsafe naming.
- Native tests include CRC mismatch, truncation, not-ready context, reset stale-sample, copy/move prevention, and tick error-surfacing cases.
- ESP-IDF example builds in CI for ESP32-S2 and ESP32-S3.
- IDF CLI contract checker runs in CI.
- Generated `Version.h` is proven present in package/consumer builds.
- Destructive CLI commands require confirmation and destructive/HIL tests remain opt-in.
- Hardware/HIL validation is documented with pass/fail evidence.
- Destructive/EEPROM tests are opt-in and visibly separated.
- README, Doxygen, and examples match the final API and timing contract.

## Recommended Implementation Phases

Phase 1: Core safety and protocol correctness

- Fix `tick()` error surfacing.
- Delete driver copy/move operations.
- Tighten wake-up expected-NACK behavior.
- Decide and document CRC health policy.
- Fix or document timing hook requirements and update Arduino example/README.

Phase 2: Timing/state-machine correctness

- Make power-up settle and long waits fully tick-driven or explicitly blocking.
- Unify clock source for scheduling and `tick()`.
- Define bounded latency for `readSettings()` and raw helpers.
- Clear or mark stale cached samples across reset/recovery epochs.

Phase 3: Tests and fault injection

- Split native tests by area.
- Add CRC mismatch, truncated response, raw no-data, clock mismatch, stale sample, copy/move, and tick completion failure tests.
- Add black-box public API tests alongside private helper tests.

Phase 4: ESP-IDF/Arduino examples and CI

- Add required timing hooks to Arduino example and README.
- Add ESP-IDF CI builds for `examples/idf/basic` on ESP32-S2 and ESP32-S3.
- Wire `tools/check_idf_example_contract.py` and `scripts/generate_version.py check` into CI.
- Add package-content or clean-consumer validation for generated headers.
- Add confirmation gates for `persist` and `factory_reset` in Arduino and IDF examples.
- Keep CLI contract parity checker.
- Add `pio pkg pack` to README validation list.

Phase 5: Hardware/HIL validation

- Run safe smoke matrix on real ESP32-S2/S3 plus SCD41.
- Add fault/recovery tests with fake transport and selected hardware cases.
- Keep destructive EEPROM/factory/FRC tests opt-in only.

Phase 6: Documentation and release readiness

- Update README and Doxygen for final timing, threading, recovery, raw helper, and destructive-command contracts.
- Update ASSUMPTIONS if `begin()` idle-state assumption remains.
- Update CHANGELOG before release.

## Hardware Validation Matrix To Recommend

Safe smoke:

- Scan bus for `0x62`.
- Get serial number.
- Get sensor variant.
- Start periodic.
- Wait data ready.
- Read measurement.
- Stop periodic and confirm 500 ms settle.
- Single-shot CO2/T/RH.
- RHT-only single-shot.
- Low-power periodic short run.
- Power down / wake up / serial verify.

Fault and recovery:

- Missing device / address NACK.
- Data NACK.
- Bad CRC injection with fake transport.
- Truncated response.
- Unplug/replug.
- Stuck bus if safe.
- Brownout/power-cycle.
- Manual `recover()` and `reinit()` behavior.
- Sleep/wake recovery.

Destructive / opt-in only:

- `persist_settings`.
- `factory_reset`.
- Forced recalibration.
- ASC configuration persistence.
- Temperature offset persistence.

## Commands Run

```text
Get-Location
Result: C:\Users\Honza\Documents\Projects\SCD41

Get-ChildItem -Name
Result: .github, .pio, .vscode, docs, examples, include, scripts, src, test, tools, .gitignore, AGENTS.md, ASSUMPTIONS.md, CHANGELOG.md, CMakeLists.txt, CODEOWNERS, CONTRIBUTING.md, Doxyfile, idf_component.yml, library.json, LICENSE, platformio.ini, README.md, SECURITY.md

Get-ChildItem -Path .. -Directory -Recurse -Depth 3 | Where-Object { $_.Name -match '(?i)SCD41|SCD4x' } | Select-Object -ExpandProperty FullName
Result:
C:\Users\Honza\Documents\Projects\SCD41
C:\Users\Honza\Documents\Projects\SCD41\include\SCD41

git rev-parse --show-toplevel; git branch --show-current; git status --short
Result:
C:/Users/Honza/Documents/Projects/SCD41
main
[no status output]

git branch --list audit/scd41-industry-readiness-exploration
Result: [no output]

git rev-parse --short HEAD
Result: 886ab60

git checkout -b audit/scd41-industry-readiness-exploration
Result: Switched to a new branch 'audit/scd41-industry-readiness-exploration'

git status --short
Result: [no output before report creation]

git branch --show-current
Result: audit/scd41-industry-readiness-exploration

rg --files | Sort-Object | Select-Object -First 400
Result: repository inventory listed public headers, src, examples, tests, docs, tools, scripts, and package files; CI metadata was inspected separately.

python --version
Result: Python 3.12.10

python -m platformio --version
Result: PlatformIO Core, version 6.1.18

idf.py --version
Result: failed, `idf.py` is not recognized as a command.

python scripts/generate_version.py check
Result: Up to date: C:\Users\Honza\Documents\Projects\SCD41\include\SCD41\Version.h

python tools/check_core_timing_guard.py
Result: Core timing guard PASSED

python tools/check_cli_contract.py
Result: CLI contract PASSED

python tools/check_idf_example_contract.py
Result: IDF example contract PASSED

python -m platformio test -e native
Result: exit 0; native test PASSED; 44 test cases, 44 succeeded in 00:00:03.788. PlatformIO printed an obsolete-core warning and installed Unity 2.6.1.

python -m platformio run -e esp32s3dev
Result: exit 0; esp32s3dev SUCCESS in 00:00:06.458; RAM 6.9% (22680/327680), Flash 30.9% (404718/1310720).

python -m platformio run -e esp32s2dev
Result: exit 0; esp32s2dev SUCCESS in 00:00:06.010; RAM 11.1% (36496/327680), Flash 28.7% (376137/1310720).

python -m platformio pkg pack
Result: exit 0; wrote C:\Users\Honza\Documents\Projects\SCD41\SCD41-0.1.0.tar.gz. The generated tarball was removed afterward to keep the audit branch report-only.

bash -lc "pwd && ls && find .. -maxdepth 4 ..."
Result: failed, `bash` is not recognized as a command on this machine.
```

## Commands Not Run

```text
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic set-target esp32s2 build
Reason: `idf.py --version` failed because `idf.py` is not installed/on PATH.

Hardware/HIL tests
Reason: no hardware was requested or made available during this audit.
```

## Final Verdict

- Ready to implement hardening: yes.
- Ready to merge as-is: no, not as an industry-grade release.
- Ready to release as industry-grade: no.

The core is substantially better than a prototype: the typed SCD41 protocol paths, transport abstraction, health model, examples, docs, and PlatformIO/native validation are already strong. The remaining blockers are concentrated in asynchronous error visibility, timing contract correctness, example timing setup, ESP-IDF build evidence, and hardware validation.
