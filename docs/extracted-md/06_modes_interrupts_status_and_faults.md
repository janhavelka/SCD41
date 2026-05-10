# Modes, Interrupts, Status, And Faults

## Modes

| Mode | Start command | Read command | Update / wait | Stop or exit | Source |
|---|---:|---:|---|---:|---|
| Periodic | `0x21b1` | `0xec05` | 5 s update interval | `0x3f86`, then wait 500 ms | Datasheet, pp. 9-10 |
| Low-power periodic | `0x21ac` | `0xec05` | approx. 30 s update interval | `0x3f86`, then wait 500 ms | Datasheet, pp. 14-15 |
| Single-shot CO2/RH/T | `0x219d` | `0xec05` | wait up to 5000 ms | idle after read flow | Datasheet, pp. 17-18 |
| Single-shot RH/T only | `0x2196` | `0xec05` | wait up to 50 ms; CO2 output is 0 ppm | idle after read flow | Datasheet, p. 18 |
| Sleep/power-down | `0x36e0` | n/a | 1 ms command time | `0x36f6`, wait 30 ms | Datasheet, pp. 18-19 |

## Status And Fault Reporting

- There is no dedicated interrupt pin in the datasheet; polling uses I2C. Source: datasheet, pp. 5, 7-8.
- `read_measurement` NACKs if no buffered measurement is available; `get_data_ready_status` can avoid that NACK. Source: datasheet, pp. 9, 15.
- `get_data_ready_status` reports ready when the least significant 11 bits of the returned word are nonzero. Source: datasheet, p. 15.
- `perform_self_test` returns `0` for no malfunction and a nonzero word for malfunction detected. Source: datasheet, p. 16.
- `perform_forced_recalibration` returns `0xffff` when FRC failed; otherwise the correction is `word - 0x8000` in ppm CO2. Source: datasheet, p. 13.

## Calibration Behavior

- ASC is enabled by default and assumes the sensor is exposed to 400 ppm CO2 at least once per week. Source: datasheet, pp. 3, 13.
- FRC sequence: operate the SCD41 for at least 3 minutes in the later application mode, at the target supply voltage, in homogeneous stable CO2; stop periodic measurement; wait 500 ms; send `0x362f` with target ppm; wait 400 ms; read the correction word. Source: datasheet, p. 13.
- In power-cycled single-shot operation, ASC is not available. Source: datasheet, p. 17.
