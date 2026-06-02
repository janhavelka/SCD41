# Prompt 07 — Hardware/HIL Matrix, Final Report, and Release Readiness Verdict


# Common instructions for every SCD41 hardening prompt

You are working in the SCD41 repository. You will receive a sequence of prompts one by one. Do **not** try to complete the whole industry-readiness effort in one pass. Each prompt is a logically bounded implementation chunk. Finish the current chunk, run the requested checks, write/update the report section, commit, and sync before waiting for the next prompt.

Use subagents where available. Subagents must return factual, code-grounded findings, not guesses. Good subagent roles for this repository:

- `core-contracts-agent`: public API, timing, tick, health, copy/move, stale state, thread/ISR contracts.
- `protocol-agent`: SCD41/SCD4x command rules, CRC, wake-up expected-NACK, idle-only commands, variant gating, destructive commands.
- `tests-agent`: fake transport, fault injection, black-box public tests, regression tests.
- `examples-ci-agent`: Arduino CLI, ESP-IDF example, CI, package generation, validation docs.
- `docs-release-agent`: README, Doxygen, final reports, hardware/HIL matrix, release claims.
- `integration-review-agent`: final diff review before each commit.

Hard rules:

1. Do not invent validation results. If hardware, ESP-IDF, Docker, or CI cannot be run locally, say exactly that.
2. Keep the core framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, logging framework, heap-heavy framework types, or global bus ownership in `include/` or `src/`.
3. Driver core must not own the I2C bus. I2C ownership, locking, reset, power control, and timeout policy belong to the injected transport or application bus manager.
4. Public fallible APIs must expose `Status` or an explicit result channel. Silent async errors are not acceptable.
5. Long or destructive commands must be explicit, documented, and testable. EEPROM/destructive hardware commands must require clear opt-in/confirmation in examples.
6. Prefer bounded, reviewable changes. Do not perform broad speculative rewrites.
7. Every prompt must end with a commit and sync attempt. If sync is impossible, document why.


## Goal

Prepare the repository for merge review by adding an honest hardware/HIL validation matrix, optional safe HIL runner guidance, final hardening report, and release-readiness verdict.

This prompt should not invent hardware success. If hardware is not available, produce a ready-to-run HIL procedure and mark it pending.

## Start procedure

```bash
git status --short
git branch --show-current
git pull --ff-only || true
```

Proceed only on clean `hardening/scd41-industry-readiness`.

## Subagents to spawn

- `protocol-agent`: classify safe vs destructive SCD41 hardware commands.
- `tests-agent`: design fake-transport fault cases still missing from hardware.
- `examples-ci-agent`: check CLI command coverage and serial/HIL feasibility.
- `docs-release-agent`: write final report and release notes.
- `integration-review-agent`: final full diff review before the final commit.

## Implement this chunk

### 1. Add hardware/HIL validation documentation

Create or update:

```text
docs/SCD41_HARDWARE_VALIDATION.md
```

Include a clear matrix with columns:

- test name,
- safe/destructive/EEPROM-wearing,
- required hardware,
- command sequence,
- expected result,
- pass/fail evidence field,
- status: pending / passed / failed / not run.

### 2. Safe smoke matrix

Document safe tests:

```text
version
scan
probe
serial
variant
settings
start_periodic
watch 2        # or equivalent bounded wait/read loop
stop_periodic
single_shot
single_shot_rht
low_power_periodic short run
power_down
wake_up
serial
self_test      # if safe/non-destructive and timing acceptable
recover
```

Use actual CLI names from the repository. Do not invent command names; map these concepts to existing commands.

### 3. Fault/recovery matrix

Document safe or bench-controlled fault tests:

- missing device / address NACK,
- data NACK with fake transport,
- timeout with fake transport,
- bus error with fake transport,
- CRC mismatch with fake transport,
- truncated response with fake transport,
- unplug/replug if safe,
- brownout/power-cycle if safe,
- bus reset hook if wired,
- power-cycle hook if wired,
- OFFLINE latch and `recover()` behavior.

### 4. Destructive / opt-in-only matrix

Document destructive/EEPROM-affecting tests separately:

- `persist_settings`,
- `factory_reset`,
- forced recalibration,
- ASC persistence/configuration persistence,
- any command that affects calibration or EEPROM endurance.

Requirements:

- CLI examples must require confirmation token before these commands.
- Automated safe smoke must not run them.
- HIL runner, if added, must require flags like `--include-destructive` or equivalent.

### 5. Optional HIL runner scaffold

If the repo already has a Python serial runner style from other libraries, add a bounded safe SCD41 HIL runner or document a ready-to-run command sequence.

If implementing runner, include:

- port selection,
- baud selection,
- command timeout per command,
- safe default suite,
- opt-in destructive suite disabled by default,
- machine-readable summary,
- log capture into `docs/hil_logs/` or an ignored output folder.

Do not require this if it becomes too large for this prompt. Documentation is acceptable if HIL code would be speculative.

### 6. Final hardening report

Create:

```text
docs/SCD41_HARDENING_FINAL_REPORT.md
```

Include:

1. Branch name and commit range.
2. Original audit summary.
3. What was implemented across all prompts.
4. Public API changes and migration notes.
5. Timing model and latency contract.
6. Async completion/error model.
7. Protocol safety changes.
8. Tests added and final test count.
9. CI/build/package coverage.
10. ESP-IDF local/CI status.
11. Hardware/HIL validation status.
12. Destructive command policy.
13. Remaining limitations.
14. Merge readiness verdict.
15. Release readiness verdict.

### 7. CHANGELOG and release notes

Update `CHANGELOG.md` with an unreleased section listing:

- breaking changes,
- added APIs,
- fixed bugs,
- documentation/CI changes,
- remaining validation limits.

Use conservative wording. Avoid "fully industry-grade" unless hardware/HIL and IDF proof are actually complete.

## Final validation

Run all available:

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic set-target esp32s2 build
git diff --check
git status --short
```

If hardware is available, run the safe smoke matrix and capture the log. If not, mark hardware validation pending.

Remove generated tarballs/build artifacts unless intentionally tracked.

## Final integration review

Before committing, have `integration-review-agent` inspect:

```bash
git diff --stat
git diff --check
git status --short
```

It must verify:

- no broad unrelated refactor,
- no generated build artifacts committed,
- no false validation claims,
- docs match code behavior,
- examples match CLI contract,
- final report lists pending items honestly.

## Commit and sync

```bash
git status --short
git add <changed files>
git commit -m "Document SCD41 hardware validation and final hardening status"
git push
```

## Final answer expected from coding agent

Return a concise summary with:

- branch name,
- commits created,
- high-level changes per prompt,
- final test results,
- ESP-IDF status,
- hardware/HIL status,
- merge readiness,
- release readiness,
- remaining future work.
