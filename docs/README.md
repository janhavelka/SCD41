# SCD41 Documentation

This folder is intentionally small. The root README and examples remain the
primary user documentation; files here hold stable reference material that is
too detailed for the README.

## Structure

| File | Purpose |
| --- | --- |
| [Assumptions](../ASSUMPTIONS.md) | Device facts, host-clock requirements, and application-policy boundaries. |
| [SCD41 protocol reference](reference/scd41-protocol.md) | Datasheet-derived protocol, timing, conversion, and command notes used by the driver. |
| [Vendor datasheet](reference/vendor/SCD41_datasheet.pdf) | Local Sensirion SCD4x datasheet used as the reference source. |
| [ESP-IDF porting](porting/esp-idf.md) | ESP-IDF component, transport adapter, and build guidance. |
| [External I2C owner integration](integration/external-i2c-owner.md) | Applications where one external I2C task owns bounded driver progress. |
| [Hardware/HIL validation](validation/hardware-hil.md) | Hardware evidence rules and smoke-test matrix. |

## Generated API Reference

Public API Doxygen lives beside the declarations in `include/SCD41/`. Build the
reference from the repository root:

```bash
python scripts/generate_version.py check
doxygen Doxyfile
```

Warnings, undocumented public symbols, and missing parameter documentation are
build failures. Generated HTML is written to `.doxygen/html/index.html` and
must not be committed. `library.json` is the version source for the generated
header, ESP-IDF component metadata, and the Doxygen project number.

## Documentation Policy

- Keep completed task prompts, closed audits, generated extracts, progress logs,
  and branch notes out of `docs/`. Use git history for that material.
- Retain a hardware report only when it records an actual run needed as release
  evidence. A file containing only `NOT RUN` entries is not evidence.
- Release packages exclude dated reports. Stable contracts and procedures
  belong in the reference, integration, porting, and validation guides.
- Keep public declarations documented in place. Put owner integration rules in
  `integration/`, device facts in `reference/`, and uncertain facts or product
  boundaries in `ASSUMPTIONS.md`.
- Keep board-specific setup in examples or validation transcripts, not in the
  core reference docs.
- Do not claim ESP-IDF, HIL, or hardware validation without command output or
  a recorded transcript.
- EEPROM-writing and calibration-changing flows must remain opt-in and clearly
  documented.
