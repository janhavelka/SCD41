# SCD41 Documentation

This folder is intentionally small. The root README and examples remain the
primary user documentation; files here hold stable reference material that is
too detailed for the README.

## Structure

| File | Purpose |
| --- | --- |
| `reference/scd41-protocol.md` | Datasheet-derived SCD41 protocol, timing, conversion, and command notes used by the driver. |
| `reference/vendor/SCD41_datasheet.pdf` | Local copy of the Sensirion SCD4x datasheet used as the reference source. |
| `porting/esp-idf.md` | ESP-IDF component, transport adapter, and build guidance. |
| `integration/external-i2c-owner.md` | Guidance for applications where an external I2C task owns bounded driver progress. |
| `validation/hardware-hil.md` | Optional hardware/HIL evidence rules and smoke-test matrix. |
| `reports/` | Dated validation reports. Reports must distinguish real hardware evidence from `NOT RUN` entries. |

## Documentation Policy

- Keep generated extracts, audit prompts, progress logs, branch reports, and
  one-off investigation notes out of `docs/`. Use git history for that material.
- Keep only evidence-bearing validation reports, or explicit `NOT RUN` reports
  requested for release gates, under `docs/reports/`.
- Keep board-specific setup in examples or validation transcripts, not in the
  core reference docs.
- Do not claim ESP-IDF, HIL, or hardware validation without command output or
  a recorded transcript.
- EEPROM-writing and calibration-changing flows must remain opt-in and clearly
  documented.
