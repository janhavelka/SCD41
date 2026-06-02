# Prompt 05 — Latency, Blocking Boundaries, and State-Machine Cleanup


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

Address the audit’s latency and blocking concerns:

- `begin()` blocks for power-up delay,
- short helpers synchronously wait ~1 ms,
- `readSettings()` performs many live reads,
- due `tick()` can perform I2C completion work,
- public latency is not documented well enough.

This prompt should produce a clear bounded-latency contract and implement only high-confidence latency improvements.

## Start procedure

```bash
git status --short
git branch --show-current
git pull --ff-only || true
```

Proceed only on clean `hardening/scd41-industry-readiness`.

## Subagents to spawn

- `core-contracts-agent`: map every public API to I2C transaction count and blocking wait budget.
- `protocol-agent`: classify which commands genuinely require long waits or idle periods.
- `tests-agent`: design fake-clock and fake-transport latency tests.
- `docs-release-agent`: produce README/Doxygen latency table and blocking/nonblocking API classification.
- `integration-review-agent`: ensure API changes are not speculative or over-engineered.

## Implement this chunk

### 1. Public API latency inventory

Create a table in docs/README listing for each major API:

- I2C transaction count,
- synchronous wait duration, if any,
- whether it can block above 1-2 ms,
- whether it requires `tick()`,
- whether it is diagnostic/bulk only,
- worst-case latency as a function of `i2cTimeoutMs`, command delay, and measurement duration.

Include at least:

- `begin()` / optional async begin if added,
- `probe()` / `recover()`,
- `requestMeasurement()` / single-shot,
- `tick()`,
- `readMeasurement()`,
- `readSettings()`,
- setters such as offset/altitude/pressure/ASC,
- `persistSettings`, `factoryReset`, `performSelfTest`, `performForcedRecalibration`,
- raw helpers.

### 2. Decide which APIs remain explicitly blocking diagnostics

Not every API must be nonblocking. But it must be honest.

Acceptable outcome:

- fast setters/read helpers remain synchronous but bounded and documented,
- bulk `readSettings()` is labeled blocking diagnostic or refactored into staged reads,
- `begin()` is either clearly blocking or gets an async begin path,
- long commands remain tick-driven.

Avoid breaking public API unless there is a clear benefit.

### 3. Consider asynchronous begin/recover only if bounded

If `begin()` blocking 30 ms is the remaining major problem, choose one:

- keep `begin()` blocking and document it explicitly as startup-only with `powerUpDelayMs`, or
- add `beginAsync()` / `beginStep()` / `requestBegin()` if it can be implemented cleanly, or
- allow `powerUpDelayMs=0` for applications that have already waited externally.

Do not half-implement a complex state machine.

### 4. Bound `tick()` work

After Prompt 03, `tick()` should surface errors. Now ensure its work is bounded and documented.

If a due completion can perform multiple I2C transactions, document this. If it can be split cleanly into one transaction per tick, implement that and test it.

### 5. `readSettings()` cleanup

The audit notes it chains seven live reads. Choose one:

- keep `readSettings()` as explicit blocking diagnostic and document transaction count,
- split into individual setting reads only,
- add staged `requestSettingsSnapshot()` / `tick()` approach.

Prefer documentation plus tests unless a small clean refactor is obvious.

## Tests/checks for this chunk

Add tests for:

1. documented transaction counts for key APIs where fake transport can count operations,
2. begin behavior with `powerUpDelayMs=0` and nonzero delay,
3. `tick()` due completion transaction count,
4. `readSettings()` behavior and error propagation across chained reads,
5. no hidden framework timing in core.

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

Append:

```markdown
## Prompt 05 — Latency and Public API Bounds
```

Include:

- final blocking/nonblocking classification,
- latency table summary,
- any API changes,
- tests added,
- remaining unavoidable blocking operations,
- exact validation results.

## Commit and sync

```bash
git status --short
git add <changed files>
git commit -m "Document and bound SCD41 API latency"
git push
```
