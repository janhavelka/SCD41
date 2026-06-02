# Prompt 06 — Tests, CI, Package Validation, and Example Parity


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

Turn the implementation into a regression-resistant library by expanding tests, wiring missing checks into CI, validating package behavior, and keeping Arduino/ESP-IDF examples honest and aligned.

The audit found strong but monolithic tests, missing ESP-IDF CI proof, missing IDF contract checker in CI, incomplete README validation commands, and package risk around generated `Version.h`.

## Start procedure

```bash
git status --short
git branch --show-current
git pull --ff-only || true
```

Proceed only on clean `hardening/scd41-industry-readiness`.

## Subagents to spawn

- `tests-agent`: split or expand native tests into protocol/state/transport/public suites.
- `examples-ci-agent`: update CI, package validation, Arduino and IDF example parity, CLI contract checks.
- `docs-release-agent`: synchronize README validation section and example labels.
- `integration-review-agent`: check no generated artifacts or `.pio` files are committed.

## Implement this chunk

### 1. Expand/split native tests

The audit notes that tests are broad but monolithic and use `#define private public`.

Do not rewrite tests just for style, but add black-box public API coverage where practical. If splitting files is low risk, split into:

- `test_protocol.cpp`,
- `test_timing.cpp`,
- `test_health.cpp`,
- `test_async.cpp`,
- `test_public_api.cpp`,
- or another clear structure.

Minimize private access in new tests.

Required coverage after previous prompts:

- missing timing hooks,
- clock wraparound,
- async completion status,
- CRC mismatch public path,
- truncated response / short read contract,
- wake-up expected-NACK classes,
- raw `allowNoData` context restriction,
- stale sample invalidation,
- copy/move prevention,
- destructive CLI confirmation contract,
- variant gating for all SCD41-only APIs,
- package/version header behavior if testable.

### 2. Wire missing checks into CI

Update `.github/workflows/ci.yml` to run, where appropriate:

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

### 3. Add pure ESP-IDF build proof if feasible

If the repository advertises ESP-IDF support, CI should prove the native IDF example builds.

Preferred CI coverage:

```bash
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic set-target esp32s2 build
```

Use an official or trustworthy ESP-IDF CI container/action if available in the repo’s existing CI style.

If implementing CI IDF builds is too risky or not possible in this environment, document exact commands and leave a clearly marked pending item. Do not claim it passes.

### 4. Package validation

Ensure `python -m platformio pkg pack` succeeds from a clean tree and the package contains what consumers need.

Check package content for:

- `include/SCD41/SCD41.h`,
- generated or committed `include/SCD41/Version.h`, or an alternative that compiles without it,
- `src/SCD41.cpp`,
- `library.json`,
- no unwanted `.pio`, tarballs, logs, temporary files.

If possible add a script such as:

```text
tools/check_package_contents.py
```

### 5. Example parity and labels

Ensure Arduino and IDF examples both:

- install timing hooks correctly,
- print version and config clearly,
- mark destructive commands visibly and require confirmation,
- include CLI help consistent with contract checker,
- avoid claiming production shared-bus manager status unless true,
- document that examples are bring-up/diagnostic unless designed otherwise.

### 6. README and docs sync

Synchronize README validation commands with CI. Add a small table:

- local PlatformIO/native checks,
- package check,
- pure ESP-IDF checks,
- hardware/HIL checks,
- destructive opt-in checks.

## Tests/checks for this chunk

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

If `idf.py` is unavailable, record exact failure and ensure CI/docs include pending/proven status honestly.

Remove generated tarballs/build artifacts unless intentionally tracked.

## Report update

Append:

```markdown
## Prompt 06 — Tests, CI, Package, Examples
```

Include:

- tests added/split,
- CI changes,
- package-validation decision,
- ESP-IDF status,
- example updates,
- exact local results,
- exact commands not run and why.

## Commit and sync

```bash
git status --short
git add <changed files>
git commit -m "Expand SCD41 tests and CI validation"
git push
```
