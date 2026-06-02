# Prompt 06 — Latency Boundaries, Stale Samples, Reset Epochs, and Probe Side Effects

Continue on `hardening/scd41-industry-readiness`. This chunk addresses:

- H-03: public API still has blocking and variable-latency paths above target.
- M-05: cached samples can survive reset/recovery epochs.
- L-02: `probe()` is health-clean but not completely side-effect-free because it can mutate command-spacing state.

This is a design-sensitive chunk. Keep compatibility where possible, but make production behavior explicit and testable.

## Required starting checks

```bash
git branch --show-current
git status --short
```

Stop if dirty with unknown user changes.

## Subagents

Spawn:

1. `latency-audit-subagent`: enumerate every public API, I2C transaction count, blocking waits, and worst-case latency.
2. `state-machine-subagent`: inspect `begin()`, pending commands, `readSettings()`, `tick()`, reset/reinit/factory/power-cycle paths, and cached sample flags.
3. `cache-epoch-subagent`: propose sample epoch/stale semantics.
4. `probe-subagent`: inspect command-spacing side effects and propose side-effect-free or documented behavior.
5. `test-subagent`: add fake-clock/fake-transport tests for latency class and stale sample behavior.
6. `integration-review-subagent`: verify changes are bounded and docs match implementation.

## H-03: public API latency contract and bounded alternatives

The report found:

- `begin()` blocks for default 30 ms power-up delay.
- short setters/read helpers wait about 1 ms.
- `readSettings()` chains seven live reads.
- due `tick()` can perform I2C completion reads.

Implement a production-grade latency model.

### Required documentation

Add a public API latency/transaction table to README and Doxygen covering at least:

- `begin()` / any new begin alternatives,
- `tick()` no-due and due paths,
- `requestMeasurement()`, `readMeasurement()`, `readDataReadyStatus()`,
- periodic/low-power start/stop,
- single-shot/RHT-only,
- powerDown/wakeUp,
- readSettings or refreshSettings,
- setters/getters for offset/altitude/pressure/ASC,
- self-test, FRC, persist, reinit, factory reset,
- raw helpers.

Each entry must state:

- I2C transaction count,
- built-in wait/deadline behavior,
- whether it is blocking, tick-driven, diagnostic-only, or destructive/opt-in,
- worst-case latency formula using `i2cTimeoutMs`, command delay, and conversion/settle time.

### Required code improvements

Implement the safest practical improvements:

1. Keep `tick()` no-due path bounded and cheap.
2. If due `tick()` performs I2C, document maximum transaction count per tick and ensure it does not process an unbounded queue in one call.
3. Make `readSettings()` either:
   - explicitly named/documented as a blocking diagnostic live refresh, or
   - split into a nonblocking request/step API and a cached snapshot API.
4. For `begin()` 30 ms delay, choose one:
   - add a nonblocking begin/startup path while preserving blocking `begin()` as compatibility wrapper, or
   - document `begin()` as explicitly blocking startup only and ensure it requires working timing hooks from prompt 02.
5. Do not fake nonblocking behavior by hiding waits in core loops.

If a larger state-machine rewrite is too risky for one chunk, implement the documentation and bounded behavior now, then record remaining work precisely. But do not leave the report’s H-03 unaddressed without an explicit design decision.

## M-05: stale cached samples across reset/recovery epochs

The report found cached samples can survive reinit/factory-reset/power-cycle completions.

Implement sample freshness/epoch handling:

1. Any operation that changes sensor epoch must clear or mark stale:
   - `reinit`,
   - `factoryReset`,
   - power cycle recovery,
   - manual recover if it can reset sensor state,
   - possibly `begin()`/`end()`/wake-up depending on actual semantics.
2. Add either:
   - `hasFreshSample()`/`sampleStale()` semantics, or
   - sample epoch counter in cached sample/snapshot.
3. Ensure cached values are not presented as fresh after reset-like operations.

Tests:

- Store sample, perform reinit completion, sample no longer fresh.
- Store sample, perform factory reset completion, sample no longer fresh.
- Store sample, perform power-cycle/recover completion, sample no longer fresh if sensor epoch changes.
- Normal successful periodic read produces fresh sample again.

## L-02: probe command-spacing side effects

The report found `probe()` is health-clean but still updates command-spacing state.

Choose one:

1. Make `probe()` use a raw path that does not mutate `_lastCommandUs` / command-spacing state; or
2. Document that probe is health-clean but command-spacing-affecting.

Preferred production behavior: make diagnostic `probe()` as side-effect-free as reasonably possible.

Tests:

- `probe()` does not change health.
- `probe()` command-spacing side effect matches documented behavior.

## Docs and progress

Update:

- README latency table.
- Doxygen on blocking/tick-driven/diagnostic/destructive APIs.
- `docs/SCD41_HARDENING_PROGRESS.md`.
- `ASSUMPTIONS.md` if startup/idle assumptions are clarified.

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
git add include/ src/ examples/ README.md docs/ test/ tools/ ASSUMPTIONS.md

git commit -m "Document and bound SCD41 latency and sample epochs"
git push -u origin hardening/scd41-industry-readiness
```

