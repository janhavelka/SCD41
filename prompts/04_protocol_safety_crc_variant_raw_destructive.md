# Prompt 04 — Protocol Safety: Wake-Up NACK, CRC, Raw Helpers, Variant Gating, Destructive Commands


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

Harden SCD41 protocol safety without broad architecture churn.

The audit identified medium-severity issues around wake-up expected-NACK masking, raw helper escape hatches, truncated-response responsibility, inconsistent SCD41-only command gating, broad `allowNoData`, destructive CLI commands without confirmation, and a temperature-offset scale mismatch to re-check.

## Start procedure

```bash
git status --short
git branch --show-current
git pull --ff-only || true
```

Proceed only on a clean `hardening/scd41-industry-readiness` branch.

## Subagents to spawn

- `protocol-agent`: verify all command availability, idle-only constraints, CRC shape, wake-up behavior, variant gating, and temperature-offset formulas against local docs and official Sensirion references already present or retrieved.
- `tests-agent`: design fake-transport tests for NACK classes, CRC, truncation, raw no-data context, and destructive CLI confirmation.
- `examples-ci-agent`: update Arduino and IDF CLI destructive command behavior and CLI contract scripts.
- `integration-review-agent`: verify raw helpers remain useful for diagnostics but cannot accidentally bypass safety silently.

## Implement this chunk

### 1. Tighten wake-up expected-NACK handling

Current behavior may map generic `I2C_ERROR` to success in wake-up. Fix policy:

- Only precise expected NACK statuses may be suppressed: address NACK and/or data NACK if the transport can identify them.
- `I2C_TIMEOUT`, `I2C_BUS`, and generic `I2C_ERROR` must remain failures unless the transport explicitly marks them as expected wake-up NACK equivalents.
- Document why wake-up can produce an expected NACK.
- Add tests for each error class.

### 2. Restrict or rename raw helper danger zones

Raw helpers are useful for diagnostics, but must not look like safe typed APIs.

Implement one clean policy:

- Keep `writeCommand()` / `readCommand()` but label them clearly as unsafe diagnostic/raw APIs in Doxygen and README; or
- Rename/add `unsafeReadCommandBytes()` and prefer typed CRC-checked helpers; or
- Add command whitelist/shape checks for known SCD41 word-returning commands.

Must-have behavior:

- CRC-checked typed word helpers are the recommended path.
- Raw byte reads must not be used by high-level driver logic where CRC words are expected.
- `allowNoData` / not-ready mapping must be restricted to valid contexts, not arbitrary raw command reads.

### 3. Strengthen truncated-response contract

Because the I2C callback API may not report actual byte count, document and test the transport contract:

- A transport must return an error if fewer bytes are read than requested.
- Example transports must enforce this.
- Fake transport tests should prove malformed/truncated data does not parse as valid.

If a callback API redesign is small and clearly justified, consider adding an actual byte-count capable transport path. Otherwise document the contract and add tests around example/fake transports.

### 4. Gate SCD41-only commands consistently

If `strictVariantCheck=false` allows SCD4x-family mode, then every SCD41-only API must be consistently gated.

Audit and fix at least:

- low-power periodic,
- single-shot CO2/T/RH,
- RHT-only single-shot,
- power-down,
- wake-up,
- ASC target,
- ASC initial/standard periods,
- any SCD41-only calibration/persistence command.

If this package is intended to be SCD41-only, consider simplifying the public contract instead: keep strict variant check and document non-SCD41 as unsupported. But do not leave a halfway family mode.

### 5. Add destructive-command confirmation

In Arduino and ESP-IDF CLIs, require confirmation tokens for destructive or EEPROM-wearing commands:

```text
persist confirm
factory_reset confirm
frc <ppm> confirm
asc ... confirm   # only for persistent/EEPROM-affecting variants if applicable
```

Requirements:

- Without confirmation, print a warning and do not execute.
- Stress/demo flows must never invoke destructive commands.
- CLI help must mark destructive commands visibly.
- CLI contract script must check confirmation requirement.

### 6. Re-check temperature-offset encoding scale

The audit found a possible 65535 vs 65536 offset conversion mismatch.

Verify against the official Sensirion datasheet/driver source or the local datasheet docs. Then:

- fix formula if wrong,
- keep formula if correct but document evidence,
- add known-vector tests for 0 °C, default 4 °C, 20 °C, and round-trip edges.

### 7. CRC/protocol fault policy

From Prompt 03, ensure CRC failures are not invisible. If not already done:

- add protocol error counter, or
- degrade health on CRC storm, or
- clearly document why CRC mismatch is a protocol fault but not an I2C health fault.

## Tests/checks for this chunk

Add/update native tests for:

1. wake-up expected NACK accepts precise NACK only.
2. wake-up timeout/bus/generic errors fail and are visible.
3. raw `allowNoData` rejected or context-limited for arbitrary commands.
4. typed reads reject CRC mismatch.
5. truncated response returns error, not stale/zero data.
6. SCD41-only APIs return `UNSUPPORTED` or equivalent for non-SCD41 variants.
7. destructive CLI commands reject missing confirmation.
8. temperature-offset known vectors.

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
## Prompt 04 — Protocol Safety and Destructive Command Guards
```

Include:

- wake-up expected-NACK policy,
- raw helper policy,
- variant policy,
- destructive command confirmations,
- temperature-offset formula decision,
- tests added and exact results.

## Commit and sync

```bash
git status --short
git add <changed files>
git commit -m "Harden SCD41 protocol safety"
git push
```
