# Register Map

SCD41 uses 16-bit Sensirion command words instead of readable/writable byte registers. Commands, unsigned 16-bit data words, and per-word CRC bytes are mapped to the command list in datasheet Table 9. Source: datasheet, pp. 7-8.

## Command Domains

| Domain | Commands | Notes | Source |
|---|---|---|---|
| Basic measurement | `start_periodic_measurement`, `read_measurement`, `stop_periodic_measurement` | Periodic update interval is 5 seconds; only a small set of commands is allowed while periodic measurement runs. | Datasheet, pp. 8-10 |
| Output compensation | temperature offset, sensor altitude, ambient pressure | Temperature offset and altitude require idle mode; ambient pressure can be set/read during periodic measurement and overrides altitude-based pressure compensation. | Datasheet, pp. 10-12 |
| Field calibration | FRC and ASC enable/get | FRC requires prior operation and a reference concentration; ASC is enabled by default. | Datasheet, pp. 13-14 |
| Low power | `start_low_power_periodic_measurement`, `get_data_ready_status` | Low-power periodic interval is about 30 seconds. | Datasheet, pp. 14-15 |
| Advanced | persist settings, serial number, self-test, factory reset, reinit | Persistent settings use EEPROM; self-test is a long command. | Datasheet, pp. 15-17 |
| SCD41 single-shot | measure single-shot, RH/T-only, power-down, wake-up, ASC periods | SCD41-only on-demand mode. | Datasheet, pp. 17-20 |

## Persistent Configuration Values

| Setting | Default / range | Persistence note | Source |
|---|---|---|---|
| Temperature offset | Default 4 degC; recommended 0 to 20 degC | Use `persist_settings` to save to EEPROM. | Datasheet, p. 10 |
| Sensor altitude | Default 0 m; valid 0 to 3000 m | Use `persist_settings` to save to EEPROM. | Datasheet, p. 11 |
| Ambient pressure | Default 101300 Pa; valid 70000 to 120000 Pa | Value is encoded as Pa / 100. Ambient pressure overrides altitude compensation. | Datasheet, p. 12 |
| ASC enabled | Default enabled | Use `persist_settings` to save enabled/disabled state. | Datasheet, p. 13 |
| ASC initial period | Default 44 h; multiples of 4 h | For single-shot ASC tuning; use `persist_settings` to save. | Datasheet, p. 19 |
| ASC standard period | Default 156 h; multiples of 4 h | For single-shot ASC tuning; use `persist_settings` to save. | Datasheet, p. 20 |

## Serial Number

`get_serial_number` returns three 16-bit words, each followed by CRC. The serial number is a 48-bit big-endian value assembled as `word0 << 32 | word1 << 16 | word2`. Source: datasheet, p. 16.
