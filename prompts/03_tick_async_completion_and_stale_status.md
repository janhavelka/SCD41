# Prompt 03 — Surface Asynchronous `tick()` Completion Errors

Continue on `hardening/scd41-industry-readiness`. This chunk fixes the highest-risk core finding:

- H-01: `tick()` silently drops fallible completion results.

The exploration report found that `tick()` is `void`, discards `_handlePendingCommand()` / `_completeMeasurement()` return values, and completion paths can return CRC/I2C/not-ready/command failures. This must be fixed so scheduled operation failures are visible.

## Required starting checks

```bash
git branch --show-current
git status --short
```

Stop if the tree is dirty with unknown user changes.

## Subagents

Spawn:

1. `async-api-design-subagent`: inspect `tick()`, pending command state, measurement completion, self-test, FRC, power-cycle, wake-up, stop-periodic, and reset completions. Recommend minimal public API design.
2. `compatibility-subagent`: identify whether changing `void tick(uint32_t)` to `Status tick(uint32_t)` breaks examples/tests and how to migrate cleanly.
3. `fault-injection-test-subagent`: design fake-transport tests that force failures during every scheduled completion class.
4. `docs-examples-subagent`: update README and examples so users actually check async status.
5. `integration-review-subagent`: verify no async failure remains silently consumed.

## Required behavior

Implement an observable async completion result channel. The preferred design is:

```cpp
Status tick(uint32_t nowMs);
Status lastAsyncStatus() const;
AsyncOperation lastAsyncOperation() const;   // or equivalent diagnostic enum
void clearLastAsyncStatus();                 // optional if useful
```

Alternative designs are acceptable only if they meet all these requirements:

1. Every fallible scheduled completion result is observable by the caller.
2. A failed completion cannot be overwritten silently before the caller has a way to inspect it, unless documented as last-event telemetry and tested.
3. Periodic measurement fetch failures, single-shot completion failures, self-test completion failures, FRC completion failures, wake-up/reinit/factory-reset/power-cycle failures, and stop-periodic settle failures all have defined visible status behavior.
4. The examples and README show users how to call `tick()` and react to non-OK statuses.
5. Existing explicit getters for self-test/FRC results remain coherent.

## Tick return semantics

Define semantics clearly. Suggested:

- `tick(nowMs)` returns `Status::Ok()` when no completion is due or all due work succeeded.
- If due work fails, it returns that failure status and stores it as the last async status.
- If multiple due operations can be processed in one tick, either process one bounded item per tick or define priority/order and report first failure.
- Do not let `MEASUREMENT_NOT_READY` from a normal periodic polling path spam errors if not actually a failure. Distinguish legitimate not-ready from failed due completion.

## Health interaction

Audit whether CRC failures should affect health now or in prompt 04. At minimum, this chunk must not hide them. If CRC health policy is deferred, record that in progress report.

## Tests required

Add native fake-transport tests for:

1. Single-shot completion with I2C timeout becomes visible through `tick()` / last async status.
2. Single-shot completion with CRC mismatch becomes visible.
3. Periodic measurement completion/fetch failure becomes visible without corrupting cached sample state.
4. Self-test completion failure is visible and stored in self-test result path.
5. FRC completion failure is visible and stored in FRC result path.
6. Wake-up/reinit/factory-reset/power-cycle scheduled failure is visible if those paths are fallible.
7. Success clears or supersedes async status exactly as documented.
8. No-due `tick()` remains cheap and returns OK.

Avoid relying only on `#define private public`; prefer black-box public API tests where possible.

## Examples and docs

Update Arduino and ESP-IDF examples:

- Capture `Status st = device.tick(nowMs());` or equivalent.
- Print/log non-OK async statuses in diagnostic CLI.
- Do not treat normal no-new-sample as an error.

Update README/Doxygen with:

- Async operation model.
- `tick()` return value/status channel.
- How to handle asynchronous errors.
- Migration note if `tick()` return type changed.

Update `docs/SCD41_HARDENING_PROGRESS.md`.

## Validation

Run:

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

## Commit and sync

```bash
git diff --check
git status --short
git add include/ src/ examples/ README.md docs/ test/ tools/
git commit -m "Expose SCD41 asynchronous tick completion status"
git push -u origin hardening/scd41-industry-readiness
```

