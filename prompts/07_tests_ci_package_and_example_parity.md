# Prompt 07 — Tests, CI, Package Proof, Guard Scripts, and Example Parity

Continue on `hardening/scd41-industry-readiness`. This chunk addresses validation infrastructure findings:

- H-05: ESP-IDF and hardware validation are not proven — this chunk fixes build/CI proof; hardware/HIL is prompt 08.
- M-08: tests are broad but monolithic/white-box.
- M-09: generated `Version.h` is a package-consumer risk.
- L-04: README validation commands are incomplete.
- L-06: core guard script is useful but not exhaustive.
- Existing report note: `tools/check_idf_example_contract.py` is documented but not wired into CI.

## Required starting checks

```bash
git branch --show-current
git status --short
```

Stop if dirty with unknown user changes.

## Subagents

Spawn:

1. `test-architecture-subagent`: split/organize tests into logical suites without losing coverage.
2. `ci-subagent`: update GitHub Actions for guard scripts, PlatformIO, package, generated version, and ESP-IDF builds.
3. `idf-build-subagent`: inspect `examples/idf/basic`, `CMakeLists.txt`, `idf_component.yml`, and recommend exact `idf.py` CI commands.
4. `package-subagent`: verify clean package contains `Version.h` or generates it before consumer build.
5. `guard-subagent`: expand core guard checks for framework, timing, heap/logging, and example boundaries where practical.
6. `docs-subagent`: sync README validation commands with actual CI.
7. `integration-review-subagent`: verify no build artifacts are committed.

## M-08: improve test organization and reduce white-box dependence

Existing tests are useful but monolithic and use private access.

Implement practical improvements:

1. Split tests into files or sections by area if the project can support it cleanly:
   - config/status/basic lifecycle,
   - protocol/CRC/conversions,
   - timing/state machine,
   - health/recovery,
   - raw helpers/transport faults,
   - async tick completion,
   - CLI/contract if host-testable.
2. Keep private access only where necessary for internal helpers. Prefer black-box public API tests for behavior fixed in prompts 02-06.
3. Ensure all tests added in previous chunks remain included.
4. Add test names that make failures actionable.

## H-05: ESP-IDF build proof in CI

Add CI jobs for pure ESP-IDF builds of `examples/idf/basic` for ESP32-S2 and ESP32-S3.

Use the project’s existing path names, likely:

```bash
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic set-target esp32s2 build
```

If the exact path differs, use the actual repo path. Use an ESP-IDF GitHub Action/container appropriate to the repo. Do not claim local success unless actually run locally.

Also wire into CI:

```bash
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio pkg pack
```

## M-09: generated `Version.h` package-consumer proof

The report found public `SCD41.h` includes `SCD41/Version.h`, but the generated file is ignored.

Implement one robust solution:

### Option A — commit generated `Version.h`

- Stop ignoring it if appropriate.
- Commit it and keep `generate_version.py check` in CI.

### Option B — ensure every supported build generates it before compile/package

- Prove PlatformIO, ESP-IDF, and package flows all generate it.
- Add package-content inspection after `pio pkg pack` that confirms `include/SCD41/Version.h` is present in the tarball.
- Add a clean-consumer build or include-check if feasible.

Choose the least surprising approach for this repo. The result must be safe for package consumers.

## L-06: expand guard scripts

Improve guard coverage where practical:

- core framework includes: Arduino, Wire, ESP-IDF, FreeRTOS,
- forbidden timing calls in core: `delay`, `millis`, `micros`, `vTaskDelay`, `esp_timer_get_time`, etc.,
- heap/logging tokens in core if against AGENTS policy,
- examples/idf must not include Arduino/Wire,
- examples/Arduino-only files must not leak into core.

Do not make brittle false-positive guards. Document what each guard enforces.

## L-04: validation docs

Update README validation section to exactly match available local and CI checks:

- `generate_version.py check`,
- core timing/contract guards,
- native tests,
- Arduino ESP32-S2/S3 builds,
- package pack and package content check,
- `idf.py` build commands,
- hardware/HIL commands pending prompt 08.

## Validation

Run locally all available:

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
```

If `idf.py` is installed locally, run:

```bash
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic set-target esp32s2 build
```

If not installed, record exactly that and rely on CI configuration without claiming local proof.

Remove generated package tarballs/build artifacts from the worktree after validation unless the repo intentionally tracks them.

## Docs and progress

Update:

- README validation section.
- `docs/SCD41_HARDENING_PROGRESS.md`.
- CI comments if useful.

## Commit and sync

```bash
git diff --check
git status --short
git add .github/ tools/ scripts/ platformio.ini library.json CMakeLists.txt idf_component.yml examples/ README.md docs/ test/ include/

git commit -m "Expand SCD41 tests CI guards and package validation"
git push -u origin hardening/scd41-industry-readiness
```

