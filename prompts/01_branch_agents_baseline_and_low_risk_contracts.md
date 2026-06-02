# Prompt 01 — Branch, AGENTS, Baseline, and Low-Risk Core Contracts

You are working in the SCD41 repository. This is not another exploration-only pass. Implement the first hardening chunk based on the existing SCD41 industry-readiness exploration report.

## Scope of this prompt

Address these report findings first because they are low-risk and establish project discipline:

- Setup hardening branch and cumulative progress report.
- Update `AGENTS.md` with SCD41-specific rules.
- Disable accidental copy/move for the live `SCD41` driver object (M-06).
- Add public Doxygen/thread/ISR safety contract (L-03).
- Start package/generated `Version.h` risk tracking (M-09), but do not fully solve CI/package in this chunk unless trivial.
- Record baseline checks before larger changes.

Do not implement timing, `tick()`, protocol, CI, or HIL fixes in this prompt except where required for compilation after copy/move changes.

## Branch and safety

1. Check current state:

```bash
git branch --show-current
git status --short
git rev-parse --short HEAD
```

2. If the worktree has uncommitted user changes, stop and report them. Do not overwrite them.
3. If not already on `hardening/scd41-industry-readiness`, create or switch to it:

```bash
git checkout main
git pull --ff-only || true
git checkout -b hardening/scd41-industry-readiness
```

If the branch already exists, switch to it only if it is clearly the intended hardening branch.

## Subagents

Spawn focused subagents and require factual findings, not speculation:

1. `core-contract-subagent`: inspect `include/SCD41/SCD41.h`, `include/SCD41/Config.h`, `src/SCD41.cpp` for copy/move, ownership, thread/ISR docs, and public API implications.
2. `docs-contract-subagent`: inspect README, Doxygen comments, AGENTS, and existing reports for consistency.
3. `test-build-subagent`: inspect native tests for a clean way to add compile-time non-copyable/non-movable assertions.
4. `integration-review-subagent`: after implementation, review diff for accidental broad edits.

## AGENTS.md update

Update `AGENTS.md` with SCD41-specific hardening rules. Include at least:

- Core `include/` and `src/` must remain framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, logging framework, dynamic framework strings, or global bus ownership.
- SCD41 core must use injected I2C transport and must not own the bus.
- Public APIs that can fail must expose `Status` or documented status/result channels.
- Long SCD41 command completions must never silently disappear; async failures must be observable.
- Timing must use one coherent clock model.
- Public APIs are not ISR-safe.
- Driver instances are not internally thread-safe unless explicitly changed and tested.
- Transport callbacks must not recursively call into the same `SCD41` instance.
- EEPROM/destructive commands must be opt-in and clearly confirmed in examples/HIL scripts.
- Do not claim ESP-IDF or hardware validation without real evidence.

## Copy/move hardening — M-06

The report notes that `SCD41` owns live transport config and runtime state but does not delete copy/move operations.

Implement:

```cpp
SCD41(const SCD41&) = delete;
SCD41& operator=(const SCD41&) = delete;
SCD41(SCD41&&) = delete;
SCD41& operator=(SCD41&&) = delete;
```

Add native compile-time checks, for example:

```cpp
static_assert(!std::is_copy_constructible<SCD41::SCD41>::value, "...");
static_assert(!std::is_copy_assignable<SCD41::SCD41>::value, "...");
static_assert(!std::is_move_constructible<SCD41::SCD41>::value, "...");
static_assert(!std::is_move_assignable<SCD41::SCD41>::value, "...");
```

Adapt namespace/class names to actual code.

## Thread/ISR/public safety docs — L-03

Add Doxygen near the `SCD41` class and relevant public APIs:

- Not thread-safe.
- Not ISR-safe.
- Use external serialization if called from multiple tasks.
- Public APIs may perform I2C and/or blocking waits unless explicitly documented otherwise.
- Transport callbacks must not call back into the same driver instance.

Mirror this in README if not already complete.

## Baseline and progress report

Create/update:

```text
docs/SCD41_HARDENING_PROGRESS.md
```

Include:

- Branch name.
- Starting commit.
- Summary of findings being addressed in this chunk.
- Files changed.
- Checks run and exact results.
- Checks not run and why.
- Remaining findings to be handled by later prompts.

## Validation for this chunk

Run all available:

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

If any fail because later chunks are needed, fix only regressions introduced by this chunk.

## Commit and sync

End with:

```bash
git diff --check
git status --short
git add AGENTS.md README.md include/ src/ test/ docs/
git commit -m "Harden SCD41 core contracts and agent rules"
git push -u origin hardening/scd41-industry-readiness
```

If push is unavailable, leave the commit local and report why.

