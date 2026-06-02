# Prompt 03 — Async Completion Status, `tick()` Error Visibility, and Stale Sample Epochs


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

Fix the highest-severity issue from the audit: `tick()` silently discards fallible completion results. Also fix stale cached samples surviving reset/recovery epochs.

This is the core state-machine correctness prompt.

## Start procedure

```bash
git status --short
git branch --show-current
git pull --ff-only || true
```

Continue only if the tree is clean and the current branch is `hardening/scd41-industry-readiness`.

## Subagents to spawn

- `core-contracts-agent`: propose public result-channel shape for async completions.
- `protocol-agent`: enumerate every pending command completion path and expected result semantics.
- `tests-agent`: build fake transport scenarios for CRC/I2C/not-ready failures inside `tick()`.
- `docs-release-agent`: update README/Doxygen for async event/result model.
- `integration-review-agent`: verify no completion path still silently drops a failure.

## Implement this chunk

### 1. Inventory all async completion paths

Audit every path that schedules `PendingCommand` and every branch in `_handlePendingCommand()` / `_completeMeasurement()` or equivalent.

At minimum cover:

- periodic measurement fetch,
- low-power periodic fetch,
- single-shot measurement completion,
- RHT-only single-shot completion,
- stop-periodic settle,
- wake-up settle,
- power-down/wake-up transitions,
- self-test completion,
- forced recalibration completion,
- reinit/factory reset completion,
- power-cycle recovery completion if present.

### 2. Add a visible async result channel

Choose a clean API. Examples:

```cpp
Status tick(uint32_t nowMs);
Status lastOperationStatus() const;
bool takeLastEvent(OperationEvent& out);
PendingResult lastPendingResult() const;
```

Preferred behavior:

- `tick()` returns `OK` if nothing due or due work completed successfully.
- `tick()` returns the actual completion error if due work failed.
- The last async status is stored for later inspection until overwritten or explicitly consumed.
- Self-test/FRC explicit getters keep their detailed command result state.
- Measurement completion failures are visible without requiring a later unrelated call.

Do not break applications unnecessarily. If changing `tick()` return type is acceptable, do it and document migration. If not, add an explicit result/event API and keep `tick()` compatible.

### 3. Health policy for async failures

Ensure I2C/timeout/bus errors during async completions update health consistently.

Decide and document CRC policy:

- Option A: CRC mismatch degrades health or a protocol-fault counter.
- Option B: CRC mismatch does not degrade bus health but increments a protocol error counter and is visible through async status.

Do not leave repeated CRC failures invisible.

### 4. Clear or stale cached samples across reset epochs

The audit found samples can survive reinit/factory-reset/power-cycle completion.

Implement one clean model:

- clear cached sample and readiness on reset/reinit/factory-reset/power-cycle, or
- add a sample epoch/stale flag that invalidates old samples after sensor epoch changes.

Prefer simple invalidation unless there is a strong reason to preserve data.

### 5. Preserve command-specific results

For self-test and FRC:

- completion failure must be returned/exposed through the general async result channel,
- command-specific result APIs must still expose final self-test/FRC result,
- `0xFFFF` FRC failure sentinel remains correctly handled.

## Tests/checks for this chunk

Add native tests for:

1. `tick()` surfaces I2C error during measurement completion.
2. `tick()` surfaces CRC mismatch during measurement completion.
3. `tick()` surfaces self-test read failure.
4. `tick()` surfaces FRC read failure and FRC sentinel behavior remains correct.
5. Periodic fetch failures are visible and affect health/protocol counters as designed.
6. No-data/not-ready completion is visible and classified correctly.
7. Cached sample is invalidated after reinit/factory reset/power-cycle completion.
8. Last async status/event persists until consumed or overwritten, according to chosen API.

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
## Prompt 03 — Async Completion Status and Stale State
```

Include:

- final async result API,
- migration notes,
- health/CRC policy,
- stale sample behavior,
- tests added,
- exact validation results.

## Commit and sync

```bash
git status --short
git add <changed files>
git commit -m "Expose SCD41 async completion results"
git push
```
