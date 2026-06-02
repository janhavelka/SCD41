# SCD41 Hardening Progress

## Current Chunk

- Prompt: `06_latency_boundaries_stale_samples_reset_epochs_probe_side_effects`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `8d53bac`
- Scope: H-03 public API latency contract, M-05 reset/recovery sample freshness, and L-02 probe side effects.
- Explicitly out of scope: a larger nonblocking startup/read-settings state-machine rewrite, HIL automation, and final release gating.

## Baseline State

- `git branch --show-current`: `hardening/scd41-industry-readiness`
- `git status --short`: no output before Prompt 06 edits
- `git rev-parse --short HEAD`: `8d53bac`

## Subagent Reviews

- Latency audit: enumerated public API I2C callback counts, blocking classes, and formulas using `i2cTimeoutMs`, `commandDelayMs`, and command execution windows.
- State machine: confirmed `tick()` processes at most one due completion per call, identified reset-like sample epoch points, and found `readSettings()` could return pre-refresh health fields after live I2C changed health.
- Cache epoch: recommended additive `hasFreshSample()` / `sampleStale()` / epoch semantics while preserving `hasSample()` as historical cache presence.
- Probe: confirmed raw probe access is health-clean, then recommended draining the physical post-probe command-spacing window before restoring command-spacing state.
- Tests: recommended native tests for probe health/spacing and stale samples after reinit, factory reset, and power-cycle recovery.
- Integration review: flagged unregistered new tests, stale README cache semantics, missing latency table, and stale progress docs.

## Findings Addressed In This Chunk

- H-03: README now documents the public API latency contract with I2C callback counts, blocking/tick-driven/diagnostic/destructive classes, built-in waits, and worst-case formula terms.
- H-03: `begin()` is explicitly documented as a blocking startup compatibility API; `readSettings()` is explicitly documented as a blocking diagnostic live refresh.
- H-03: `tick()` remains bounded to at most one due completion per call, with no-due calls performing no I2C.
- H-03: `readSettings()` now refreshes local snapshot fields after live reads so returned health/protocol state reflects the diagnostic I2C it just performed while preserving live configuration values.
- M-05: the driver now tracks `sensorEpoch` and `sampleEpoch`; fresh means a cached sample exists and both epochs match.
- M-05: `hasFreshSample()`, `sampleStale()`, `sensorEpoch()`, and `sampleEpoch()` were added, and `SettingsSnapshot` exposes the same freshness fields.
- M-05: successful `startReinit()`, `startFactoryReset()`, and power-cycle recovery advance the sensor epoch immediately and mark retained cached samples stale before the settle window completes.
- M-05: `measurementReady()` and `getMeasurement()` expose only fresh unconsumed samples; retained stale samples remain available through cached diagnostic helpers with explicit stale/epoch indicators.
- L-02: `probe()` remains raw and health-clean, restores `_lastCommandUs` / `_lastCommandValid`, and drains the required post-probe command-spacing guard before returning success.
- Tests: native coverage now verifies probe health cleanliness and spacing restoration, stale samples after reset-like epochs, and fresh samples after a later successful periodic read.

## Files Changed

- `ASSUMPTIONS.md`
- `README.md`
- `docs/SCD41_HARDENING_PROGRESS.md`
- `include/SCD41/SCD41.h`
- `src/SCD41.cpp`
- `test/test_basic.cpp`

## Checks Run

- `python scripts/generate_version.py check`
  - Result: passed; `include\SCD41\Version.h` is up to date.
- `python tools/check_core_timing_guard.py`
  - Result: passed; `Core timing guard PASSED`.
- `python tools/check_cli_contract.py`
  - Result: passed; `CLI contract PASSED`.
- `python tools/check_idf_example_contract.py`
  - Result: passed; `IDF example contract PASSED`.
- `python -m platformio test -e native`
  - Result: passed; 82 test cases, 82 succeeded in `00:00:00.427`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:03.327`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:03.045`.

## Checks Not Run

- Native ESP-IDF `idf.py` builds: not in this prompt's validation list and still require a configured ESP-IDF environment.
- Hardware/HIL validation: explicitly out of scope for this chunk and no hardware evidence was collected.

## Previously Completed Chunks

- Prompt: `05_variant_gating_offset_and_destructive_cli`
  - Commit: `8d53bac`
  - Addressed consistent SCD41-only variant gating, temperature-offset scale verification, and destructive/EEPROM CLI confirmations.
- Prompt: `04_protocol_safety_wake_crc_raw_truncation_no_data`
  - Commit: `3a8bd25`
  - Addressed wake-up expected-NACK policy, protocol/CRC telemetry, raw helper safety, exact-transfer transport contracts, and no-data context.
- Prompt: `03_surface_async_tick_completion_errors`
  - Commit: `59cbe93`
  - Addressed observable asynchronous `tick()` completion failures and async status telemetry.
- Prompt: `02_fix_timing_hooks_and_clock_model`
  - Commit: `dcff765`
  - Addressed required `nowMs`/`nowUs` hooks, Arduino timing defaults, one-clock scheduling, and divergent-clock tests.
- Prompt: `01_branch_agents_baseline_and_low_risk_contracts`
  - Commit: `e2f7776`
  - Addressed copy/move deletion, public thread/ISR/reentrant callback contracts, and project discipline in `AGENTS.md`.

## Remaining Findings For Later Prompts

- CI/package validation, ESP-IDF build automation, and example parity beyond the completed CLI contract checks.
- Opt-in HIL command handling, hardware/HIL matrix, final release gate, and evidence-backed validation records.
- Refresh or supersede resolved findings in `docs/SCD41_INDUSTRY_READINESS_EXPLORATION_REPORT.md` after the full hardening sequence is complete.
