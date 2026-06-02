# Prompt 04 — Protocol Safety: Wake-up NACK, CRC Policy, Raw Helpers, Truncation, No-Data Context

Continue on `hardening/scd41-industry-readiness`. This chunk fixes protocol-safety findings:

- M-01: wake-up expected-NACK handling masks generic I2C errors.
- L-01: CRC mismatch does not degrade health or have a documented policy.
- M-02: raw helpers can bypass command shape and CRC policy.
- M-03: short/truncated response detection is transport-only.
- M-07: busy/not-ready mapping is too broad through raw `allowNoData`.

Do not implement variant gating/destructive CLI in this prompt; that is prompt 05.

## Required starting checks

```bash
git branch --show-current
git status --short
```

Stop if dirty with unknown user changes.

## Subagents

Spawn:

1. `protocol-correctness-subagent`: inspect command table, raw helpers, read/write helpers, CRC functions, data-ready/read-measurement behavior, and local datasheet notes.
2. `transport-contract-subagent`: inspect `I2cWriteFn`/`I2cWriteReadFn` contract, example transports, timeout/NACK/detail mappings, zero-byte/short-read behavior.
3. `health-policy-subagent`: propose documented CRC health behavior and tests.
4. `test-subagent`: add focused native tests for NACK/generic errors, CRC mismatch, truncated responses, and no-data context.
5. `integration-review-subagent`: verify API names/docs make unsafe paths obvious.

## M-01: tighten wake-up expected-NACK handling

The report found `_i2cWriteTrackedAllowExpectedNack()` converts `I2C_NACK_ADDR`, `I2C_NACK_DATA`, and generic `I2C_ERROR` to success during wake-up. This can hide real bus faults.

Implement:

1. Only suppress precise expected NACK statuses that the transport can identify.
2. Do **not** suppress `I2C_TIMEOUT`, `I2C_BUS`, or generic `I2C_ERROR` unless there is an explicit capability/config flag documenting that the transport cannot distinguish wake-up NACK from generic error.
3. Preserve health updates for real failures.
4. Document wake-up expected-NACK behavior in README/Doxygen.

Tests:

- Wake-up with precise address/data NACK succeeds if that is the documented expected behavior.
- Wake-up with timeout fails and updates health.
- Wake-up with bus error fails and updates health.
- Wake-up with generic `I2C_ERROR` fails by default.
- If a compatibility flag is added for generic expected NACK, it is opt-in and tested.

## L-01: CRC health policy

The report found CRC mismatch returns `CRC_MISMATCH` but does not affect health.

Choose and implement a clear policy:

Preferred production policy:

- CRC mismatch increments protocol failure counters and degrades health similarly to I2C failures after threshold, or
- CRC mismatch remains separate from I2C health but has explicit `protocolErrorCount` / `crcErrorCount` telemetry.

Do not leave repeated CRC failures invisible in health/diagnostics.

Tests:

- One CRC mismatch returns `CRC_MISMATCH` and updates documented telemetry.
- Repeated CRC mismatches move state/degraded/protocol counter exactly as documented.
- Successful read after CRC failures recovers counters/state according to the documented policy.

## M-02: raw helper safety and CRC policy

The report found `writeCommand()` and `readCommand()` allow arbitrary commands and raw `readCommand()` returns bytes without CRC validation.

Implement safer semantics without removing useful diagnostics:

1. Rename or document raw byte helpers as unsafe diagnostics if public API compatibility prevents renaming.
2. Prefer CRC-checked typed raw-word helpers for commands returning SCD41 words.
3. Make README and Doxygen explicit: raw byte reads do **not** validate CRC and should not be used in production measurement paths.
4. If feasible, add a diagnostic unsafe marker/flag for unvalidated raw byte reads.
5. Reject known word-returning commands through unvalidated byte helper unless the caller explicitly opts into unsafe raw bytes.

Tests:

- CRC-checked raw-word read catches bad CRC.
- Unvalidated raw byte read is clearly unsafe and allowed only via documented path.
- Known word-returning command through safe helper validates CRC.

## M-03: truncated response contract

The current callback API has no actual byte-count return, so the core cannot know whether a buggy transport partially filled the buffer while returning OK.

Implement the strongest bounded fix available:

1. Strengthen the transport contract in `Config.h` Doxygen: an OK read **must** mean the full requested length was filled.
2. Ensure all bundled Arduino and ESP-IDF example transports enforce full-length reads and return an error on short read.
3. Add adapter tests or native fake tests proving example transport-style short read maps to a non-OK status.
4. If reasonable without a major API break, add a future-safe callback variant that returns actual length; otherwise document as future work.

## M-07: restrict broad `allowNoData` / not-ready mapping

The report found arbitrary raw reads can map read-header NACK to `MEASUREMENT_NOT_READY` when `allowNoData=true`.

Implement:

1. Not-ready mapping should only apply in managed contexts where SCD41 protocol says no data / measurement not ready is valid.
2. Arbitrary raw command NACK should remain an I2C failure unless command is whitelisted/documented.
3. Update tests for valid read-measurement no-data versus arbitrary raw no-data.

## Docs and progress

Update:

- README protocol/raw-helper section.
- Doxygen for raw helpers and wake-up.
- `docs/SCD41_HARDENING_PROGRESS.md`.
- Existing docs/assumptions if needed.

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
git commit -m "Harden SCD41 protocol error and raw helper contracts"
git push -u origin hardening/scd41-industry-readiness
```

