# SCD41 Hardening Progress

## Current Chunk

- Prompt: `01_branch_agents_baseline_and_low_risk_contracts`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `392b58e`
- Scope: low-risk core contracts and project discipline before timing, async, protocol, CI, or HIL hardening.

## Baseline State

- `git branch --show-current`: `main`
- `git status --short`: no output
- `git rev-parse --short HEAD`: `392b58e`
- `git pull --ff-only`: already up to date
- Created branch: `hardening/scd41-industry-readiness`

## Findings Addressed In This Chunk

- M-06: `SCD41` driver instances were implicitly copyable/movable despite owning live transport configuration and runtime state.
- L-03: public Doxygen did not carry the full thread/ISR/reentrant-transport contract.
- M-09: generated `Version.h` package risk is now tracked in this progress file; full CI/package hardening remains for a later prompt.
- Project discipline: `AGENTS.md` now states hardening rules for framework neutrality, injected I2C, observable async results, clock coherence, threading/ISR safety, destructive command confirmation, and evidence-backed validation claims.

## Files Changed

- `AGENTS.md`
- `README.md`
- `include/SCD41/Config.h`
- `include/SCD41/SCD41.h`
- `test/test_basic.cpp`
- `docs/SCD41_HARDENING_PROGRESS.md`

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
  - Result: passed; 44 test cases, 44 succeeded in `00:00:02.496`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:05.114`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:04.756`.
- Post-review rerun after README ISR wording fix:
  - `python tools/check_core_timing_guard.py`: passed; `Core timing guard PASSED`.
  - `python tools/check_cli_contract.py`: passed; `CLI contract PASSED`.
  - `python tools/check_idf_example_contract.py`: passed; `IDF example contract PASSED`.
  - `git diff --check`: passed with Git line-ending warnings only.

## Checks Not Run

- Native ESP-IDF `idf.py` builds: not in this prompt's validation list and still require a configured ESP-IDF environment.
- Hardware/HIL validation: not requested for this chunk and no hardware evidence was collected.
- `python -m platformio pkg pack`: tracked as part of later package/generated-header hardening, not required by this prompt's validation list.

## Remaining Findings For Later Prompts

- Timing contract and coherent clock model.
- Observable `tick()` and async completion failures.
- Raw command, CRC, and transport edge hardening.
- Variant gating, temperature-offset scale review, destructive CLI confirmations, and opt-in HIL command handling.
- Latency/state-machine cleanup, stale sample handling, and probe side effects.
- Tests, CI/package validation, ESP-IDF build automation, and example parity.
- Hardware/HIL matrix, final release gate, and evidence-backed validation records.
- Refresh or supersede resolved findings in `docs/SCD41_INDUSTRY_READINESS_EXPLORATION_REPORT.md` after the full hardening sequence is complete.
