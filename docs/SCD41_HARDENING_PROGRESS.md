# SCD41 Hardening Progress

## Current Chunk

- Prompt: `08_hil_matrix_final_report_and_release_gate`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `77a0e5f`
- Scope: hardware/HIL validation matrix, optional safe serial runner, final
  hardening report, release/merge gate language, and final local validation.
- Explicitly out of scope: inventing hardware results, running destructive
  flows without explicit operator approval, and tagging a release.

## Baseline State

- `git branch --show-current`: `hardening/scd41-industry-readiness`
- `git status --short`: no output before Prompt 08 edits
- `git rev-parse --short HEAD`: `77a0e5f`

## Subagent Reviews

- HIL design: recommended safe smoke, timing/soak, fault/recovery, and
  destructive opt-in matrices with pass/fail criteria and no hardware claims
  without transcript evidence.
- CLI HIL: confirmed Arduino and ESP-IDF CLIs expose the same command names and
  supplied exact operator sequences for safe smoke, periodic, single-shot,
  low-power periodic, power, recovery, and destructive confirmation flows.
- Python runner: found no existing HIL tooling and recommended an optional
  pyserial-based runner that defaults to safe tests, records raw transcripts,
  writes JSON/Markdown summaries, and refuses destructive commands without
  explicit gates.
- Docs/report: recommended final report structure mapping H/M/L findings to
  disposition and using honest HIL-pending release language.
- Integration review: flagged unsafe claims to avoid: no HIL pass claim without
  transcripts, no observed-CI claim without a GitHub Actions run, H-03 remains
  documented/partial because `begin()` and `readSettings()` are still explicit
  blocking paths, and M-08 is improved but still intentionally keeps narrow
  white-box tests.

## Findings Addressed In This Chunk

- H-05 remaining HIL gap: added `docs/SCD41_HARDWARE_VALIDATION.md` with safe
  smoke, timing/soak, fault/recovery, destructive opt-in, evidence, and verdict
  rules.
- H-05 remaining HIL gap: added optional `tools/scd41_hil_runner.py` for
  serial CLI evidence capture. It requires an explicit port, runs safe commands
  by default, writes transcript/JSON/Markdown artifacts, and refuses destructive
  flows unless `--include-destructive` and the exact confirmation phrase are
  supplied.
- Release discipline: added `docs/SCD41_HARDENING_FINAL_REPORT.md` mapping each
  exploration finding to disposition, evidence, merge gate, release gate, local
  validation status, CI status, and HIL status.
- Docs: README now links the hardware validation matrix and final hardening
  report.

## Files Changed

- `README.md`
- `docs/SCD41_HARDENING_FINAL_REPORT.md`
- `docs/SCD41_HARDWARE_VALIDATION.md`
- `docs/SCD41_HARDENING_PROGRESS.md`
- `tools/scd41_hil_runner.py`

## Checks Run

- `python scripts/generate_version.py check`
  - Result: passed; `include\SCD41\Version.h` is up to date.
- `python tools/check_core_timing_guard.py`
  - Result: passed; `Core timing guard PASSED`.
- `python tools/check_cli_contract.py`
  - Result: passed; `CLI contract PASSED`.
- `python tools/check_idf_example_contract.py`
  - Result: passed; `IDF example contract PASSED`.
- `python tools/scd41_hil_runner.py --help`
  - Result: passed; help text printed.
- `python -m platformio test -e native`
  - Result: passed; 82 test cases, 82 succeeded in `00:00:00.395`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:01.207`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:01.142`.
- `python -m platformio pkg pack`
  - Result: passed; wrote `SCD41-0.1.0.tar.gz`.
- `python tools/check_package_contents.py SCD41-*.tar.gz`
  - Result: passed; `Package content check PASSED: SCD41-0.1.0.tar.gz`.

## Checks Not Run

- Local native ESP-IDF `idf.py` builds: blocked because `idf.py` is not on PATH
  in this workspace (`CommandNotFoundException`). CI is configured to run the
  ESP-IDF v6.0.1 matrix, but the CI run was not observed locally.
- Hardware/HIL validation: not run because no serial port, board, SCD41, or
  operator-approved hardware context was provided. No HIL transcript exists.

## Previously Completed Chunks

- Prompt: `07_tests_ci_package_proof_guard_scripts_example_parity`
  - Commit: `77a0e5f`
  - Addressed test organization, CI guards, package content proof, generated
    `Version.h` package safety, ESP-IDF CI wiring, and validation docs.
- Prompt: `06_latency_boundaries_stale_samples_reset_epochs_probe_side_effects`
  - Commit: `9ff92fe`
  - Addressed public API latency documentation, reset/recovery sample epochs,
    stale sample semantics, and probe command-spacing side effects.
- Prompt: `05_variant_gating_offset_and_destructive_cli`
  - Commit: `8d53bac`
  - Addressed consistent SCD41-only variant gating, temperature-offset scale
    verification, and destructive/EEPROM CLI confirmations.
- Prompt: `04_protocol_safety_wake_crc_raw_truncation_no_data`
  - Commit: `3a8bd25`
  - Addressed wake-up expected-NACK policy, protocol/CRC telemetry, raw helper
    safety, exact-transfer transport contracts, and no-data context.
- Prompt: `03_surface_async_tick_completion_errors`
  - Commit: `59cbe93`
  - Addressed observable asynchronous `tick()` completion failures and async
    status telemetry.
- Prompt: `02_fix_timing_hooks_and_clock_model`
  - Commit: `dcff765`
  - Addressed required `nowMs`/`nowUs` hooks, Arduino timing defaults,
    one-clock scheduling, and divergent-clock tests.
- Prompt: `01_branch_agents_baseline_and_low_risk_contracts`
  - Commit: `e2f7776`
  - Addressed copy/move deletion, public thread/ISR/reentrant callback
    contracts, and project discipline in `AGENTS.md`.

## Remaining Findings For Later Prompts

- Run and store safe hardware/HIL smoke transcripts on real ESP32-S2/S3 plus
  SCD41 hardware.
- Run soak/fault/recovery hardware tests where practical.
- Run destructive/EEPROM/calibration hardware tests only with explicit operator
  approval on suitable hardware, or keep them marked not run.
- Observe final pushed CI status before merge.
