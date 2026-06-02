# SCD41 Hardening Progress

## Current Chunk

- Prompt: `03_surface_async_tick_completion_errors`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `dcff765`
- Scope: H-01 observable asynchronous `tick()` completion failures.
- Explicitly out of scope: HIL, CI expansion, and CRC health-policy changes beyond surfacing CRC failures.

## Baseline State

- `git branch --show-current`: `hardening/scd41-industry-readiness`
- `git status --short`: no output
- `git rev-parse --short HEAD`: `dcff765`

## Subagent Reviews

- Async API design: confirmed `tick()` discarded due pending-command and measurement-completion statuses; recommended a small async result channel and suppressing non-terminal `MEASUREMENT_NOT_READY` retries.
- Compatibility: confirmed changing `void tick(uint32_t)` to `Status tick(uint32_t)` is source-compatible for normal call sites that ignore the return value, with edge risk for pointer-to-member code or stale forward declarations.
- Fault injection tests: recommended single-shot timeout/CRC, periodic fetch failure, self-test/FRC failures, settle-only completion success, success superseding old failure, and no-due tick tests.
- Docs/examples: identified README, Arduino CLI, and ESP-IDF loop updates so users capture and report non-OK async statuses.
- Integration review: enumerated drop points in `tick()`, examples, and existing tests.

## Findings Addressed In This Chunk

- H-01: `tick(uint32_t)` now returns `Status` and no longer discards due `_handlePendingCommand()` or `_completeMeasurement()` failures.
- H-01: `lastAsyncStatus()`, `lastAsyncOperation()`, and `clearLastAsyncStatus()` expose persistent async completion telemetry.
- H-01: non-terminal `MEASUREMENT_NOT_READY` completion attempts reschedule and return OK so normal periodic/not-ready retries do not spam errors.
- H-01: terminal periodic fetch failures now clear the pending request and preserve the cached last sample.
- H-01: self-test and forced-recalibration completion failures remain coherent with their dedicated result getters and are also visible immediately through `tick()`.
- H-01: settle-only async completions such as stop-periodic, wake-up, reinit, factory reset, persist settings, power-down, and power-cycle publish successful async operations.
- Examples: Arduino and ESP-IDF CLI loops capture `tick()` status and print non-OK async completion failures.
- Docs: README and Doxygen now document returned async status, last async telemetry, no-due behavior, and the source-compatible migration.
- Health policy: CRC completion failures are visible through async status; whether CRC mismatches should affect transport health remains deferred to the later protocol/health hardening prompt.

## Files Changed

- `README.md`
- `docs/IDF_PORT.md`
- `docs/SCD41_HARDENING_PROGRESS.md`
- `examples/01_basic_bringup_cli/main.cpp`
- `examples/idf/basic/main/main.cpp`
- `include/SCD41/SCD41.h`
- `src/SCD41.cpp`
- `test/test_basic.cpp`
- `tools/check_cli_contract.py`
- `tools/check_idf_example_contract.py`
- `AGENTS.md`

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
  - Result: passed; 59 test cases, 59 succeeded in `00:00:00.484`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:03.516`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:03.504`.

## Checks Not Run

- Native ESP-IDF `idf.py` builds: not in this prompt's validation list and still require a configured ESP-IDF environment.
- Hardware/HIL validation: explicitly out of scope for this chunk and no hardware evidence was collected.

## Previously Completed Chunks

- Prompt: `02_fix_timing_hooks_and_clock_model`
  - Commit: `dcff765`
  - Addressed required `nowMs`/`nowUs` hooks, Arduino timing defaults, one-clock scheduling, and divergent-clock tests.
- Prompt: `01_branch_agents_baseline_and_low_risk_contracts`
  - Branch created from `main` at `392b58e`.
  - Addressed copy/move deletion, public thread/ISR/reentrant callback contracts, and project discipline in `AGENTS.md`.

## Remaining Findings For Later Prompts

- Raw command, CRC, and transport edge hardening.
- CRC/validation health policy, including whether CRC mismatches should affect health counters.
- Variant gating, temperature-offset scale review, destructive CLI confirmations, and opt-in HIL command handling.
- Latency/state-machine cleanup, stale sample handling, and probe side effects.
- Tests, CI/package validation, ESP-IDF build automation, and example parity beyond this async status guard.
- Hardware/HIL matrix, final release gate, and evidence-backed validation records.
- Refresh or supersede resolved findings in `docs/SCD41_INDUSTRY_READINESS_EXPLORATION_REPORT.md` after the full hardening sequence is complete.
