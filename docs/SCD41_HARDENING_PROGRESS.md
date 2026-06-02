# SCD41 Hardening Progress

## Current Chunk

- Prompt: `05_variant_gating_offset_and_destructive_cli`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `3a8bd25`
- Scope: M-04 consistent SCD41-only variant gating, L-05 temperature-offset scale verification, and M-10 destructive/EEPROM CLI confirmations.
- Explicitly out of scope: broad HIL implementation and new family-device support.

## Baseline State

- `git branch --show-current`: `hardening/scd41-industry-readiness`
- `git status --short`: no output before Prompt 05 edits
- `git rev-parse --short HEAD`: `3a8bd25`

## Subagent Reviews

- Variant gating: confirmed `strictVariantCheck=false` lets `begin()` succeed on non-SCD41 variants, so known SCD41-only typed APIs and raw command words must return `UNSUPPORTED`.
- Conversion math: confirmed the source-like PDF extraction uses `2^16 - 1` (`65535`) for temperature-offset set and get; the compact summary doc was misleading where it said `65536`.
- CLI safety: confirmed Arduino and ESP-IDF `persist` and `factory_reset` executed immediately, and recommended checking confirmation before `clearSettingsCache()`. FRC was identified as field-sensitive because calibration history is stored automatically.
- Tests: recommended native variant-gating tests, hard-coded temperature-offset vectors, readback matrix coverage, and static CLI contract guards for confirmation forms.
- Integration review: recommended preserving diagnostic raw access for unknown/common commands, skipping unsupported SCD41-only fields in `readSettings()` without losing common live diagnostics, and guarding static CLI checks.

## Findings Addressed In This Chunk

- M-04: SCD41-only typed APIs now use a shared SCD41 variant guard: low-power periodic, single-shot mode/start APIs, power-down, wake-up, ASC target, and ASC initial/standard period getters/setters.
- M-04: known SCD41-only raw command words now return `UNSUPPORTED` on non-SCD41 variants while unknown diagnostic commands and common SCD4x commands remain available.
- M-04: `readSettings()` now tolerates `UNSUPPORTED` for SCD41-only live fields on non-SCD41 variants, preserves common live fields, and leaves `liveConfigValid=false` when not all live fields were read.
- L-05: temperature-offset docs now use the local source scale `65535 / 175`; public Doxygen documents nearest-integer rounding for encode/decode helpers.
- L-05: native tests now pin hard-coded offset vectors including default 4 C, datasheet examples, max 20 C, a 65535-vs-65536 discriminator, payload CRCs, decode vectors, and readback round trips.
- M-10: Arduino and ESP-IDF CLIs require `persist confirm`, `factory_reset confirm`, and `frc confirm <reference_ppm>`.
- M-10: refused factory-reset commands no longer clear the local settings cache; cache clearing happens only after the command is accepted/scheduled.
- M-10: CLI contract checkers now assert confirmation help strings, refusal strings, and handler ordering for destructive/field-sensitive commands.
- Docs: README, CHANGELOG, compact datasheet notes, ESP-IDF port notes, and Doxygen were updated for the new support boundary and operator-safety behavior.

## Files Changed

- `CHANGELOG.md`
- `README.md`
- `docs/IDF_PORT.md`
- `docs/SCD41_datasheet.md`
- `docs/SCD41_HARDENING_PROGRESS.md`
- `examples/01_basic_bringup_cli/main.cpp`
- `examples/idf/basic/main/main.cpp`
- `include/SCD41/SCD41.h`
- `src/SCD41.cpp`
- `test/test_basic.cpp`
- `tools/check_cli_contract.py`
- `tools/check_idf_example_contract.py`

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
  - Result: passed; 76 test cases, 76 succeeded in `00:00:00.495`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:06.300`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:04.284`.

## Checks Not Run

- Native ESP-IDF `idf.py` builds: not in this prompt's validation list and still require a configured ESP-IDF environment.
- Hardware/HIL validation: explicitly out of scope for this chunk and no hardware evidence was collected.

## Previously Completed Chunks

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

- Latency/state-machine cleanup, stale sample handling, and probe side effects.
- Tests, CI/package validation, ESP-IDF build automation, and example parity beyond this variant/operator-safety pass.
- Opt-in HIL command handling, hardware/HIL matrix, final release gate, and evidence-backed validation records.
- Refresh or supersede resolved findings in `docs/SCD41_INDUSTRY_READINESS_EXPLORATION_REPORT.md` after the full hardening sequence is complete.
