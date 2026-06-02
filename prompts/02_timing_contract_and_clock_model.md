# Prompt 02 — Timing Contract, Arduino Hooks, and Unified Clock Model


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

Fix the timing-contract problems identified by the audit:

- default timing hooks can break Arduino-facing usage,
- fallback time returns constant zero,
- scheduling deadlines use `_nowMs()` but `tick(nowMs)` compares with a caller-provided clock,
- README/Arduino examples omit timing hooks.

This prompt should make timing behavior explicit, consistent, and test-covered.

## Start procedure

```bash
git status --short
git branch --show-current
```

Confirm you are on `hardening/scd41-industry-readiness`. If not, stop and report.

Pull/rebase if needed, but do not overwrite local work.

## Subagents to spawn

- `core-contracts-agent`: propose the cleanest single clock model.
- `tests-agent`: design fake-clock tests including wraparound and deliberately divergent clock inputs.
- `examples-ci-agent`: inspect Arduino CLI and README quick start for timing hook setup.
- `integration-review-agent`: verify that the chosen timing model is coherent and not half-migrated.

## Design decision required

Choose one timing model and apply it consistently. Do not leave two competing time sources.

Preferred options:

### Option A — `tick(nowMs)` owns scheduling time

- All scheduled deadlines are created from the same `nowMs` domain passed to `tick()` / request calls.
- APIs that schedule long work either take `nowMs` explicitly or require the driver to have a stored current time from `tick()`.
- This is good for deterministic embedded schedulers.

### Option B — injected `Config::nowMs` owns scheduling time

- `tick()` becomes parameterless or ignores its argument after API migration.
- All scheduling uses `_nowMs()` from the configured hook.
- This is simpler for Arduino, but requires a valid hook for any timing-sensitive API.

Pick the least-breaking clean option. If a public API break is required, document it clearly.

## Implement this chunk

### 1. Enforce/document required time hooks

The audit found that null `nowMs`/`nowUs` plus fallback zero time can make waits exit by stall/timeout behavior instead of real time. Fix this by either:

- requiring timing hooks in `Config` validation when APIs need them, or
- installing proper Arduino hooks in examples and providing a clear documented default, or
- both.

The Arduino README quick start and Arduino CLI must set appropriate hooks before `begin()`:

- `nowMs` from `millis()`,
- `nowUs` from `micros()` if command spacing uses microseconds,
- `cooperativeYield` from `yield()` or an equivalent safe no-op if no wait can spin.

Keep these hooks in example/adapter code, not core framework code.

### 2. Remove misleading fallback behavior

Review `PlatformTime.h` and `_nowMs()` / `_nowUs()` behavior.

If hooks are required, missing hooks should produce `INVALID_CONFIG` or a clearly documented deterministic behavior. Avoid a constant-zero fallback that makes the API look time-aware when it is not.

### 3. Fix clock mismatch

The audit found scheduled deadlines are created using `_nowMs()` while `tick(nowMs)` compares with the external `nowMs` argument. Make scheduling and comparison use one consistent clock domain.

Add tests where:

- fake `Config::nowMs` differs from `tick(nowMs)`,
- wraparound occurs near `UINT32_MAX`,
- a long command becomes ready at the correct time,
- a command is not completed early.

### 4. Update docs

Document clearly:

- which APIs require a monotonic millisecond clock,
- whether microsecond timing is required for command spacing,
- whether `tick()` must be called periodically,
- what happens if timing hooks are omitted,
- expected safe scheduler cadence.

## Tests/checks for this chunk

Add/update native tests for:

1. missing timing hooks behavior,
2. Arduino/example config contract if testable by static/contract script,
3. unified clock model,
4. wraparound scheduling,
5. command-spacing timing with fake microsecond clock,
6. no hidden framework timing in core.

Run:

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
git diff --check
git status --short
```

## Report update

Append to `docs/SCD41_HARDENING_PROGRESS_REPORT.md`:

```markdown
## Prompt 02 — Timing Contract and Unified Clock Model
```

Include:

- chosen timing model,
- public API changes/migration notes,
- examples updated,
- tests added,
- exact validation results,
- risks left for Prompt 03.

## Commit and sync

```bash
git status --short
git add <changed files>
git commit -m "Fix SCD41 timing contract"
git push
```
