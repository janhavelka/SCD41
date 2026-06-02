# Prompt 08 — Hardware/HIL Matrix, Final Report, and Release Gate

Continue on `hardening/scd41-industry-readiness`. This final chunk addresses the remaining part of H-05 and release discipline:

- No hardware/HIL validation evidence.
- Destructive/EEPROM tests must be opt-in only.
- Final comprehensive report is required for industry-standard readiness assessment.

This prompt may add scripts/docs and may run hardware tests only if hardware is available. Do not invent hardware results.

## Required starting checks

```bash
git branch --show-current
git status --short
```

Stop if dirty with unknown user changes.

## Subagents

Spawn:

1. `hil-design-subagent`: design safe smoke, fault/recovery, soak, and destructive opt-in matrices for SCD41.
2. `cli-hil-subagent`: inspect current CLI command names and create exact operator command sequences.
3. `python-runner-subagent`: if existing HIL tooling exists, extend it; otherwise create a documented optional runner skeleton that does not fake results.
4. `docs-report-subagent`: prepare final report structure and release gate language.
5. `integration-review-subagent`: compare all prompts/findings to final diff and report missing coverage.

## Hardware/HIL documentation matrix

Create or update:

```text
docs/SCD41_HARDWARE_VALIDATION.md
```

Include these sections with exact commands and pass/fail criteria.

### Safe smoke matrix

- Identify/scan SCD41 at `0x62`.
- Read serial number.
- Read/verify sensor variant.
- Read data-ready status false/true behavior.
- Start periodic measurement.
- Wait data-ready.
- Read measurement and validate plausible CO2/temp/RH ranges.
- Continue periodic for N samples.
- Stop periodic and verify 500 ms settle behavior.
- Single-shot CO2/T/RH.
- RHT-only single-shot.
- Low-power periodic short run.
- Power down / wake up / serial verify.
- Recover after manual stop/start sequence.

### Timing and soak matrix

- 5-minute periodic smoke.
- 30-minute or longer periodic soak if practical.
- Low-power periodic soak.
- Repeated single-shot loop.
- Ensure no stale samples across reinit/recover/power cycle.
- Record success/failure counts and async status counters.

### Fault/recovery matrix

- Missing device/address NACK.
- Data NACK if practical.
- Bad CRC injection with fake transport or HIL shim.
- Truncated response with fake transport.
- Unplug/replug if safe.
- Brownout/power-cycle if hardware supports safe power switching.
- Bus reset hook behavior.
- Power-cycle hook behavior.
- Recover from OFFLINE.

### Destructive/opt-in only matrix

Clearly separate and require explicit flags/tokens:

- `persist_settings` / `persist confirm`.
- `factory_reset confirm`.
- forced recalibration.
- ASC setting persistence.
- temperature offset persistence.

Warn about EEPROM wear and calibration loss.

## Optional HIL runner

If the repo already has a Python HIL runner, extend it. If not, create a conservative optional runner under `tools/` or `scripts/`, for example:

```text
tools/scd41_hil_runner.py
```

Required behavior if implemented:

- Serial port configurable.
- Safe tests default only.
- Destructive tests require `--include-destructive` plus command-level confirmation.
- Bounded timeouts.
- Records raw transcript.
- Produces JSON/Markdown summary.
- Does not mask failures.
- Does not run destructive flows by default.

If no runner is implemented, document manual HIL commands in detail.

## Final report

Create/update:

```text
docs/SCD41_HARDENING_FINAL_REPORT.md
```

The final report must map every exploration finding to disposition:

| Finding | Status | Evidence |
| --- | --- | --- |
| H-01 tick silent async failures | fixed / partial / not fixed | files/tests |
| H-02 timing hooks | fixed / partial / not fixed | files/tests |
| H-03 latency | fixed / documented / partial | files/tests/docs |
| H-04 clock model | fixed | files/tests |
| H-05 ESP-IDF/HIL proof | CI fixed, HIL pending/run | exact evidence |
| M-01 wake-up NACK | fixed | tests |
| M-02 raw helpers | fixed/documented | tests/docs |
| M-03 truncation | fixed/documented | tests/docs |
| M-04 variant gating | fixed | tests |
| M-05 stale samples | fixed | tests |
| M-06 copy/move | fixed | tests |
| M-07 no-data mapping | fixed | tests |
| M-08 tests | improved | test summary |
| M-09 Version.h/package | fixed | CI/package proof |
| M-10 destructive CLI | fixed | contract/tests |
| L-01 CRC health | fixed/documented | tests/docs |
| L-02 probe side effect | fixed/documented | tests |
| L-03 Doxygen safety | fixed | docs |
| L-04 validation docs | fixed | README |
| L-05 offset scale | fixed/verified | vectors |
| L-06 guard script | improved | guard results |

Also include:

- Branch name and final commit list.
- Public API changes and migration notes.
- Examples changed.
- Tests added and exact counts.
- Local checks run with exact pass/fail output.
- CI changes and whether CI was actually observed passing.
- Hardware commands run and raw transcript link/path if any.
- Hardware not run and why.
- Remaining future work.
- Merge verdict.
- Release verdict.

Use honest language. If HIL was not run, say: “not yet field-grade; code is hardening-complete, hardware validation pending.”

## Final validation

Run all available checks:

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

If `idf.py` is local:

```bash
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic set-target esp32s2 build
```

If hardware is connected, run the safe smoke sequence from `docs/SCD41_HARDWARE_VALIDATION.md` and save transcript. Do not run destructive commands unless explicitly requested.

Cleanup generated tarballs/artifacts unless intentionally tracked.

## Commit and sync

```bash
git diff --stat
git diff --check
git status --short
git add docs/ tools/ scripts/ examples/ README.md test/ .github/ include/ src/
git commit -m "Document SCD41 HIL matrix and final hardening report"
git push -u origin hardening/scd41-industry-readiness
```

If there are no code changes, commit docs/report changes. If push fails, report exactly why.

## Final answer expected from coding agent

The coding agent must end with a concise summary:

- Current branch.
- Commits made.
- Findings fixed.
- Findings partially fixed and why.
- Tests/checks run with exact results.
- ESP-IDF CI/local status.
- HIL status.
- Merge recommendation.
- Release recommendation.

