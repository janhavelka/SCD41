# SCD41 Hardening Progress

## Current Chunk

- Prompt: `02_fix_timing_hooks_and_clock_model`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `e2f7776`
- Scope: H-02 default timing hook contract and H-04 coherent clock model.
- Explicitly out of scope: async `tick()` status, HIL, and CI expansion.

## Baseline State

- `git branch --show-current`: `hardening/scd41-industry-readiness`
- `git status --short`: no output
- `git rev-parse --short HEAD`: `e2f7776`

## Subagent Reviews

- Timing contract: confirmed optional hooks plus inert fallback clocks caused timeout-style failures instead of `INVALID_CONFIG`, and confirmed deadlines were scheduled with `_nowMs()` but completed against `tick(nowMs)`.
- Arduino example: identified README quick-start and Arduino CLI setup as missing `nowMs`, `nowUs`, and `cooperativeYield` wiring.
- Native tests: recommended missing-hook, Arduino-style hook, divergent-clock, and wraparound deadline regressions.
- Compatibility: confirmed `tick(uint32_t)` should remain for source compatibility and that stricter timing hooks are a behavioral migration.

## Findings Addressed In This Chunk

- H-02: `begin()` now rejects missing `Config::nowMs` or `Config::nowUs` with `INVALID_CONFIG` before any I2C or bounded wait.
- H-02: Arduino README quick start and the Arduino bringup CLI now install timing hooks from `millis()`, `micros()`, and `yield()` before `begin()`.
- H-02: `cooperativeYield` remains optional; bounded waits are deterministic when the injected clocks advance.
- H-04: `tick(uint32_t)` now uses the configured `Config::nowMs` clock internally and keeps its argument only as source-compatible API surface.
- H-04: single-shot paired deadlines are scheduled from one captured injected-clock sample.
- Guard coverage: CLI contract checking now requires Arduino CLI timing-hook assignments.
- Test coverage: native regressions cover missing required hooks before I2C, Arduino-style timing hooks, optional yield with advancing clocks, divergent `tick()` arguments, and uint32 wraparound deadlines.

## Files Changed

- `README.md`
- `docs/IDF_PORT.md`
- `docs/SCD41_HARDENING_PROGRESS.md`
- `examples/01_basic_bringup_cli/main.cpp`
- `include/SCD41/Config.h`
- `include/SCD41/SCD41.h`
- `src/PlatformTime.h`
- `src/SCD41.cpp`
- `test/test_basic.cpp`
- `tools/check_cli_contract.py`

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
  - Result: passed; 52 test cases, 52 succeeded in `00:00:01.734`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:05.265`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:04.748`.

## Checks Not Run

- Native ESP-IDF `idf.py` builds: not in this prompt's validation list and still require a configured ESP-IDF environment.
- Hardware/HIL validation: explicitly out of scope for this chunk and no hardware evidence was collected.

## Previously Completed Chunk

- Prompt: `01_branch_agents_baseline_and_low_risk_contracts`
- Branch created from `main` at `392b58e`.
- Addressed copy/move deletion, public thread/ISR/reentrant callback contracts, and project discipline in `AGENTS.md`.
- Validation passed for version generation, timing guard, CLI contract, IDF example contract, native tests, and both Arduino PlatformIO environments.

## Remaining Findings For Later Prompts

- Observable `tick()` and async completion failures.
- Raw command, CRC, and transport edge hardening.
- Variant gating, temperature-offset scale review, destructive CLI confirmations, and opt-in HIL command handling.
- Latency/state-machine cleanup, stale sample handling, and probe side effects.
- Tests, CI/package validation, ESP-IDF build automation, and example parity beyond this timing-hook guard.
- Hardware/HIL matrix, final release gate, and evidence-backed validation records.
- Refresh or supersede resolved findings in `docs/SCD41_INDUSTRY_READINESS_EXPLORATION_REPORT.md` after the full hardening sequence is complete.
