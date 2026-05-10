# Document Inventory

Compact notes in `docs/extracted-md/` summarize SCD41 command, timing, conversion, calibration, and electrical facts. Raw page-oriented text remains in `docs/pdf-extracted-md/` for exact Sensirion wording and package figures.

| Source PDF | Raw extract | Role | Version / date | Pages | Notes |
|---|---|---|---|---:|---|
| `docs/SCD41_datasheet.pdf` | `docs/pdf-extracted-md/SCD41_datasheet.md` | Primary datasheet | Version 1.5, July 2023 | 27 | Performance, electrical data, I2C commands, calibration, low-power modes, single-shot mode, package and handling. |

## Compact Note Set

| File | Purpose |
|---|---|
| `01_chip_overview.md` | Sensor purpose, SCD41-specific performance, and outputs. |
| `02_pinout_and_signals.md` | LGA pads, supplies, I2C pins, DNC handling, and power-supply notes. |
| `03_electrical_and_timing.md` | Supply, current, timing, absolute ratings, and performance anchors. |
| `04_protocol_commands_and_transactions.md` | I2C framing, CRC, command sequences, conversions, and command table. |
| `05_register_map.md` | Command map used in place of memory-mapped registers. |
| `06_modes_interrupts_status_and_faults.md` | Periodic, low-power periodic, single-shot, data-ready, ASC/FRC, self-test. |
| `07_initialization_reset_and_operational_notes.md` | Startup, measurement flows, persistence, reset, and calibration procedure notes. |
| `08_variant_differences_and_open_questions.md` | SCD40/SCD41 differences, revision notes, and unresolved items. |

## Scope Notes

- The compact files are not raw extraction archives and do not preserve legal boilerplate or OCR noise.
- Page citations refer to `docs/SCD41_datasheet.pdf`; use the raw extract or PDF for exact command examples and figures.
- Units are normalized to ASCII (`degC`, `+/-`, `kOhm`) where practical.
