# Electrical And Timing

## Electrical Specifications

| Parameter | Value | Source |
|---|---:|---|
| `VDD` operating range | 2.4 V to 5.5 V; typical 3.3 V or 5.0 V | Datasheet, p. 4 |
| Unloaded supply ripple | 30 mV peak-to-peak max | Datasheet, p. 4 |
| Peak current at 3.3 V | typ. 175 mA, max 205 mA | Datasheet, p. 4 |
| Peak current at 5 V | typ. 115 mA, max 137 mA | Datasheet, p. 4 |
| Avg. periodic current, 5 s update, 3.3 V | typ. 15 mA, max 18 mA | Datasheet, p. 4 |
| Avg. low-power periodic current, 30 s update, 3.3 V | typ. 3.2 mA, max 3.5 mA | Datasheet, p. 4 |
| Avg. single-shot current, 5 min interval, 3.3 V | typ. 0.45 mA, max 0.5 mA | Datasheet, p. 4 |
| `VIH` | 0.65 x `VDD` to `VDD` | Datasheet, p. 4 |
| `VIL` | max 0.3 x `VDD` | Datasheet, p. 4 |
| `VOL` | max 0.66 V at 3 mA sink | Datasheet, p. 4 |

## Timing

| Parameter | Value | Source |
|---|---:|---|
| Power-up time | max 30 ms after hard reset with `VDD >= 2.25 V` | Datasheet, p. 6 |
| Soft reset / `reinit` time | max 30 ms | Datasheet, p. 6 |
| SCL frequency | 0 to 400 kHz | Datasheet, p. 6 |
| `stop_periodic_measurement` delay | 500 ms before other commands respond | Datasheet, p. 10 |
| `read_measurement` execution time | 1 ms | Datasheet, p. 9 |
| `perform_self_test` execution time | 10000 ms | Datasheet, p. 16 |

## Absolute Ratings And Environmental Limits

- Operating conditions: -10 to 60 degC and 0 to 95 %RH non-condensing. Source: datasheet, p. 4.
- DC supply absolute range: -0.3 V to 6.0 V. Source: datasheet, p. 4.
- Max voltage on `SDA`, `SCL`, and `GND`: -0.3 V to `VDD + 0.3 V`. Source: datasheet, p. 4.
- MSL level is 3; floor life out of bag is 168 hours at <=30 degC and 60 %RH. Source: datasheet, pp. 4, 23.
