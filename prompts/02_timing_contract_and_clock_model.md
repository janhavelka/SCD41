# Prompt 02 — Fix Timing Hooks, Arduino Defaults, and One-Clock Scheduling Model

Continue on `hardening/scd41-industry-readiness`. This chunk directly addresses:

- H-02: default timing contract breaks Arduino-facing usage.
- H-04: scheduled deadlines can use inconsistent clocks.
- Related docs/tests around timing hooks, fallback timing, quick start, and CLI example setup.

Do not implement async `tick()` error surfacing in this prompt; that is prompt 03. Do not do HIL or CI expansion here.

## Required starting checks

```bash
git branch --show-current
git status --short
```

Stop if the tree is dirty with unknown user changes.

## Subagents

Spawn:

1. `timing-contract-subagent`: inspect `Config`, `PlatformTime`, `_nowMs()`, `_nowUs()`, `_waitMs()`, command scheduling, and `tick(nowMs)`.
2. `arduino-example-subagent`: inspect README quick start and `examples/01_basic_bringup_cli/main.cpp` timing hook setup.
3. `native-test-subagent`: design fake-clock tests for missing hooks, real hooks, wraparound, and deliberately divergent clocks.
4. `compatibility-subagent`: identify public API break risk and recommend the least surprising migration path.

## Fix H-02: timing hooks must not silently degrade Arduino usage

The exploration report found that `Config::nowMs`, `nowUs`, and `cooperativeYield` default null, fallback time returns constant zero, `begin()` waits 30 ms, README quick start omits hooks, and Arduino CLI omits hooks before `device.begin(gConfig)`.

Implement a strict, production-safe contract:

1. Define exactly which timing hooks are required for which API classes:
   - `nowMs` required for public APIs that perform millisecond waits or schedule millisecond deadlines.
   - `nowUs` required for SCD41 command-spacing enforcement if the implementation uses microsecond spacing.
   - `cooperativeYield` optional, but examples should supply it where available.
2. `begin()` must not rely on constant-zero fallback time for a nonzero `powerUpDelayMs`.
3. If required timing hooks are missing for a path that needs them, return `INVALID_CONFIG` or a precise existing status before starting I2C side effects where practical.
4. Alternatively, if the project design chooses to make hooks mandatory in `begin()`, document that as an intentional breaking/production-safe change.
5. Avoid reintroducing Arduino/ESP-IDF timing calls into core.

Preferred direction:

- Keep core framework-neutral.
- Require injected time hooks in `Config` validation for normal `begin()` unless the user explicitly sets all timing waits to zero and accepts diagnostic behavior.
- Update examples so normal users always provide hooks.

## Fix Arduino example and README quick start

In README quick start and Arduino CLI setup, install timing hooks before `begin()`:

- `nowMs` from `millis()`.
- `nowUs` from `micros()`.
- `cooperativeYield` from `yield()` or a safe no-op if appropriate.

Make it clear these are example-layer hooks, not core dependencies.

## Fix H-04: one coherent clock model

The report found deadlines scheduled with `_nowMs()` but compared with caller-provided `tick(nowMs)`. Fix this so one time source controls scheduled command deadlines.

Choose and implement one model consistently:

### Preferred model A — tick argument owns scheduling time

- All scheduled deadlines are based on the same `nowMs` supplied to the public method that starts/schedules the command, or on a stored last-tick time only if well-defined.
- `tick(nowMs)` compares deadlines created in the same time domain.
- Do not mix `_nowMs()` deadlines with unrelated caller-provided `tick(nowMs)`.

### Acceptable model B — injected clock owns scheduling time

- Long-command scheduling always uses `Config::nowMs`.
- Change or overload `tick()` so it no longer accepts a separate time domain, or make `tick(nowMs)` ignore/validate the argument consistently.

Pick the least disruptive design and document the compatibility impact.

## Tests required

Add native tests for:

1. Default timing hooks missing with nonzero `powerUpDelayMs` returns the documented status and does not silently spin/stall.
2. Arduino/example-style hooks allow `begin()` to succeed with a fake monotonic clock.
3. Scheduling and `tick()` use one time domain.
4. A deliberately divergent `Config::nowMs` and `tick(nowMs)` cannot make a command complete early/late silently; either impossible by API design or detected/documented.
5. Wraparound-safe deadline comparison if existing code supports wraparound.
6. `cooperativeYield` optional behavior is deterministic.

Update or add guard/CLI contract tests if needed to require timing hooks in examples.

## Documentation required

Update README, Doxygen, and `docs/SCD41_HARDENING_PROGRESS.md` with:

- Required timing hooks.
- Which APIs require a clock.
- How Arduino and ESP-IDF examples provide time.
- One-clock scheduling model.
- Migration note if `begin()` behavior changed.

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
git commit -m "Fix SCD41 timing hooks and clock model"
git push -u origin hardening/scd41-industry-readiness
```

