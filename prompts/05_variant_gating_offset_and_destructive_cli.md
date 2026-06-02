# Prompt 05 — Variant Gating, Offset Math, and Destructive Command Confirmation

Continue on `hardening/scd41-industry-readiness`. This chunk fixes device-specific correctness and operator-safety findings:

- M-04: `strictVariantCheck=false` exposes inconsistent SCD4x-family behavior.
- L-05: temperature-offset scale should be rechecked and vector-tested.
- M-10: destructive/EEPROM CLI commands lack confirmation.

Do not do broad HIL implementation here. Only code/docs/tests for these findings.

## Required starting checks

```bash
git branch --show-current
git status --short
```

Stop if dirty with unknown user changes.

## Subagents

Spawn:

1. `variant-gating-subagent`: inspect `SensorVariant`, command table variant bits, every public SCD41/SCD4x-only API, and local docs/extracted variant notes.
2. `conversion-math-subagent`: inspect temperature-offset encode/decode math against local docs and existing tests.
3. `cli-safety-subagent`: inspect Arduino and IDF CLI destructive commands and contract checker.
4. `test-subagent`: add variant, offset-vector, and CLI confirmation tests/guards.
5. `integration-review-subagent`: verify no accidental removal of useful diagnostics.

## M-04: consistent SCD41-only variant gating

The exploration report says this repository is publicly SCD41-only, while internally it has `SensorVariant` and SCD4x-family awareness. Some SCD41-only commands are gated; others such as power-down/wake-up and ASC periods may not be.

Implement one consistent policy:

### Preferred policy for this repository

Since package/API contract is SCD41-only, keep SCD41 as the default and production-supported target. If `strictVariantCheck=false` permits non-SCD41 operation, every SCD41-only command must return `UNSUPPORTED` or equivalent for non-SCD41 variants unless local docs prove support.

Audit and gate at least:

- low-power periodic,
- single-shot CO2/T/RH,
- RHT-only single-shot,
- power-down,
- wake-up,
- ASC target,
- ASC periods,
- any other command marked variant-specific in `CommandTable` or local docs.

Do not invent SCD40/SCD42/SCD43 support. If local docs are insufficient, choose the safer unsupported result.

Tests:

- Non-SCD41 variant with strict check disabled: every SCD41-only public API returns unsupported or documented status.
- SCD41 variant: supported APIs still work.
- README documents public support boundary clearly.

## L-05: temperature-offset scale verification

The report notes local docs mention 65536-style offset scaling, while code uses 65535.

Do this carefully:

1. Inspect `docs/SCD41_datasheet.md`, `docs/extracted-md/`, and any local vendor driver notes.
2. Determine the intended exact formula from local repo docs. Do not use guesses.
3. If the code is wrong, fix encode/decode.
4. If the code is correct and docs are misleading, update docs.
5. Add vector tests for:
   - 0 °C offset,
   - default 4 °C offset if supported/documented,
   - 20 °C offset,
   - max accepted offset,
   - readback round trip.
6. Use integer-safe math and document rounding behavior.

## M-10: destructive/EEPROM CLI confirmation

The report found `persist` and `factory_reset` execute directly in Arduino and IDF CLI examples.

Implement confirmation tokens in both Arduino and IDF examples:

Required behavior:

```text
persist
# must refuse and print: use 'persist confirm' to write EEPROM
persist confirm
# actually executes

factory_reset
# must refuse and print: use 'factory_reset confirm' to erase/reset settings
factory_reset confirm
# actually executes
```

Also consider confirmation for forced recalibration or any command that changes persistent calibration/state, if it is destructive or field-sensitive.

Update CLI help and README.

Update `tools/check_cli_contract.py` so destructive commands require documented confirmation forms.

Tests:

- Contract checker verifies confirmation strings exist.
- If CLI parser is host-testable, add tests that missing confirmation refuses.
- Native public driver tests should not execute destructive operations except clearly named opt-in simulated tests.

## Docs and progress

Update:

- README variant-support section.
- README destructive-command section.
- Doxygen for variant-specific APIs.
- `docs/SCD41_HARDENING_PROGRESS.md`.
- CHANGELOG if public API/CLI behavior changed.

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
git add include/ src/ examples/ README.md docs/ test/ tools/ CHANGELOG.md
git commit -m "Gate SCD41 variant APIs and protect destructive CLI commands"
git push -u origin hardening/scd41-industry-readiness
```

