# SCD41 Hardening Progress

## Current Chunk

- Prompt: `07_tests_ci_package_proof_guard_scripts_example_parity`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `9ff92fe`
- Scope: test organization, CI gates, package-content proof, generated `Version.h`
  consumer safety, guard script hardening, and ESP-IDF example build wiring.
- Explicitly out of scope: hardware/HIL execution, destructive command hardware
  validation, and final release tagging.

## Baseline State

- `git branch --show-current`: `hardening/scd41-industry-readiness`
- `git status --short`: no output before Prompt 07 edits
- `git rev-parse --short HEAD`: `9ff92fe`

## Subagent Reviews

- Test architecture: recommended preserving the root native test app for this
  chunk, adding logical runner sections, and replacing obvious private-field
  assertions with public API checks where practical.
- CI: recommended separate jobs for guards, native tests, Arduino PlatformIO
  builds, package proof, and native ESP-IDF example builds.
- IDF build: recommended building `examples/idf/basic` for `esp32s3` and
  `esp32s2` with ESP-IDF v6.0.1.
- Package: recommended tracking generated `include/SCD41/Version.h`, running
  `generate_version.py check`, and inspecting package tarball contents.
- Guard scripts: recommended scanning stripped code for timing, framework,
  heap, and logging tokens, and expanding example-boundary checks.
- Docs: recommended updating README and IDF docs so local validation matches CI.

## Findings Addressed In This Chunk

- M-09: `include/SCD41/Version.h` is now trackable and included as package
  proof material, while remaining generated from `library.json` and marked
  "do not edit".
- M-09/L-06: `tools/check_package_contents.py` verifies required public/core
  files in the packed tarball and rejects repo/build artifacts.
- H-05: CI now includes an ESP-IDF build matrix for `examples/idf/basic` on
  ESP32-S3 and ESP32-S2 using `espressif/esp-idf-ci-action@v1`.
- H-05/L-04: CI now runs version, core guard, Arduino CLI contract, IDF example
  contract, native tests, Arduino PlatformIO builds, and package proof as
  separate jobs.
- Guard hardening: the core guard now rejects active framework includes,
  framework types, blocking timing calls, heap-backed containers/allocation, and
  logging tokens from `src/` and `include/`.
- Example parity: the IDF checker now scans all IDF example source/header files
  plus CMake dependencies and rejects Arduino facades, legacy IDF I2C includes,
  heap-backed parser regressions, and CLI parity drift.
- Example boundaries: the Arduino CLI checker now parses command handlers/help
  aliases, checks timing hook assignments structurally, and enforces that
  Arduino/example-common code does not leak into the core or IDF example.
- M-08: native tests now have logical runner sections and a `test/README.md`
  documenting the remaining narrow white-box policy.
- M-08: identity/variant tests now use public `readSensorVariant()` and
  `getIdentity()` assertions where private-field access was unnecessary.
- Docs: README and ESP-IDF docs now include package inspection and ESP-IDF
  build commands, while keeping hardware/HIL validation explicitly separate.

## Files Changed

- `.github/workflows/ci.yml`
- `.gitignore`
- `README.md`
- `docs/IDF_PORT.md`
- `docs/IDF_PORT_IMPLEMENTATION.md`
- `docs/SCD41_HARDENING_PROGRESS.md`
- `include/SCD41/Version.h`
- `test/README.md`
- `test/test_basic.cpp`
- `tools/check_cli_contract.py`
- `tools/check_core_timing_guard.py`
- `tools/check_idf_example_contract.py`
- `tools/check_package_contents.py`

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
  - Result: passed; 82 test cases, 82 succeeded in `00:00:00.925`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:01.272`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:01.252`.
- `python -m platformio pkg pack . -o dist`
  - Result: passed; wrote `dist\SCD41-0.1.0.tar.gz`.
- `python tools/check_package_contents.py dist/*.tar.gz`
  - Result: passed; `Package content check PASSED: SCD41-0.1.0.tar.gz`.

## Checks Not Run

- Local native ESP-IDF `idf.py` builds: blocked because `idf.py` is not on PATH
  in this workspace. CI wiring was added to run the ESP-IDF v6.0.1 matrix.
- Hardware/HIL validation: explicitly out of scope for this chunk and no
  hardware evidence was collected.

## Previously Completed Chunks

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

- Opt-in HIL command handling, hardware/HIL matrix, final release gate, and
  evidence-backed hardware validation records.
- Refresh or supersede resolved findings in
  `docs/SCD41_INDUSTRY_READINESS_EXPLORATION_REPORT.md` after the full
  hardening sequence is complete.
