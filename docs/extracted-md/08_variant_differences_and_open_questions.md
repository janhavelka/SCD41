# Variants And Open Questions

## SCD40 vs SCD41

| Topic | SCD40 | SCD41 | Source |
|---|---|---|---|
| Product positioning | Base accuracy | High accuracy, low-power modes supported | Datasheet, p. 1 |
| Specified CO2 range | 400 to 2000 ppm | 400 to 5000 ppm | Datasheet, p. 1 |
| CO2 accuracy | 400-2000 ppm: +/-(50 ppm + 5% reading) | See SCD41 multi-range accuracy in `01_chip_overview.md`. | Datasheet, p. 3 |
| Single-shot mode | Not listed as SCD40 feature | SCD41 only | Datasheet, pp. 17-18 |

## Revision Notes

- Source datasheet is Version 1.5, July 2023. Revision history notes updated SCD41 product numbers, ASC clarification for power-cycled single-shot operation, and added scaling notes for ASC period parameters. Source: datasheet, pp. 25-26.

## Open Questions For Implementation

- The compact notes do not include package outline/land pattern dimensions because they are figure-driven; inspect the PDF before layout work. Source: datasheet, pp. 22-23.
- Ambient pressure uses command `0xe000` for both directions: the set sequence writes one 16-bit word encoded as `Pa / 100`, while the get sequence reads one word decoded as `word * 100 Pa`. Source: datasheet, p. 12.
- SCD41-only commands in this source set are `measure_single_shot` (`0x219d`), `measure_single_shot_rht_only` (`0x2196`), `power_down` (`0x36e0`), `wake_up` (`0x36f6`), and ASC period commands `0x2445`/`0x2340`/`0x244e`/`0x234b`. Source: datasheet, pp. 17-20.
