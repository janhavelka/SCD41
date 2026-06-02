# Prompt 01 — Branch, AGENTS, Baseline Capture, and Low-Risk Contracts


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

Start the implementation branch cleanly, preserve the audit report, update `AGENTS.md`, capture baseline validation, and implement low-risk safety contracts that should not require major architecture decisions.

This is **not** the prompt to fix all timing/state-machine issues. Keep this chunk bounded.

## Start procedure

1. Inspect current state:

```bash
git status --short
git branch --show-current
git rev-parse --short HEAD
```

2. If the worktree is dirty with user changes unrelated to the audit report, stop and report them.

3. Create or switch to the implementation branch:

```bash
git checkout -b hardening/scd41-industry-readiness
```

If this branch already exists, do not delete it. Switch to it only if it is clearly the intended branch and the worktree is clean.

4. If the audit report exists only on an audit branch, preserve it in `docs/SCD41_INDUSTRY_READINESS_REPORT.md` or the existing equivalent name.

## Subagents to spawn

- `core-contracts-agent`: inspect class copy/move, public Doxygen, thread/ISR contract, begin/end/recover lifecycle, generated version include.
- `docs-release-agent`: inspect README, Doxygen, validation command list, generated `Version.h` handling, release-claim wording.
- `integration-review-agent`: review only this chunk’s small contract changes before commit.

## Implement this chunk

### 1. Update `AGENTS.md`

Add SCD41-specific hardening rules:

- Core must remain framework-neutral.
- I2C must remain injected and externally owned.
- Public fallible APIs must expose `Status` or an explicit result channel.
- `tick()` and scheduled operations must never silently hide failures once later chunks are implemented.
- Public APIs are not ISR-safe unless explicitly documented and proven.
- Instances are not thread-safe; external serialization is required.
- Destructive/EEPROM operations must be opt-in in examples and tests.
- Hardware and ESP-IDF validation claims must be evidence-based.

### 2. Delete copy/move operations

The audit notes that `SCD41` owns live state, callback/context, health counters, cached samples, and pending commands. Add explicit deleted copy/move constructor and assignment operators to the public class.

Add compile-time tests or static assertions where practical:

```cpp
static_assert(!std::is_copy_constructible<SCD41::SCD41>::value, "...");
static_assert(!std::is_move_constructible<SCD41::SCD41>::value, "...");
```

Use the real namespace/class names.

### 3. Add public Doxygen safety contracts

Add class-level and relevant API comments documenting:

- not thread-safe,
- not ISR-safe,
- I2C callbacks must not recursively call back into the same driver instance,
- external serialization required for multi-task use,
- core does not own bus reset/power-cycle policy.

### 4. Version header/package risk triage

Audit the `Version.h` generation path. The audit found that public `SCD41.h` includes `SCD41/Version.h`, while `Version.h` may be ignored/generated.

Do **not** make a risky packaging change blindly. Choose one clean path:

- commit/generated `Version.h` if that is the repository convention and safe, or
- guarantee every supported build path generates it before compile, and add a validation check, or
- remove the hard public dependency if a fallback version macro can be safely provided.

At minimum, add a CI/check script or package-content validation plan so a clean consumer including `SCD41/SCD41.h` cannot fail due to missing `Version.h`.

### 5. README validation command correction

Update README validation commands to include the existing checks that the audit says are missing:

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

Mark `idf.py` builds as required where available, but do not claim they pass unless actually run.

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
git diff --check
git status --short
```

If `pio pkg pack` creates an artifact, remove it afterward unless the repository intentionally tracks it.

## Report update

Create or update:

```text
docs/SCD41_HARDENING_PROGRESS_REPORT.md
```

Add a section:

```markdown
## Prompt 01 — Branch, AGENTS, Baseline, Low-Risk Contracts
```

Include:

- branch name,
- baseline commit,
- files changed,
- copy/move decision,
- Version.h/package decision,
- checks run and exact results,
- checks not run and why,
- remaining risks for the next prompt.

## Commit and sync

After the integration reviewer approves:

```bash
git status --short
git add AGENTS.md README.md include src test tools docs .github platformio.ini library.json CMakeLists.txt idf_component.yml
# adjust file list to actual changed files only
git commit -m "Harden SCD41 baseline contracts"
git push -u origin hardening/scd41-industry-readiness
```

If push fails, report the exact reason and keep the local commit.
