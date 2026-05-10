# Protocol Commands And Transactions

SCD41 uses 16-bit I2C command words and 16-bit data words. Each data word is followed by an 8-bit CRC; command words are not followed by CRC. Source: datasheet, pp. 7, 21.

## Framing

| Item | Behavior | Source |
|---|---|---|
| I2C address | `0x62` | Datasheet, p. 7 |
| Byte order | 16-bit commands and words transmit most-significant byte first. | Datasheet, p. 7 |
| CRC | CRC-8, polynomial `0x31`, init `0xff`, no reflection, final XOR `0x00`; example CRC of `0xbeef` is `0x92`. | Datasheet, p. 21 |
| Write data CRC | Mandatory. | Datasheet, p. 7 |
| Read data CRC | Master may choose whether to process it. | Datasheet, p. 7 |
| Command overlap | Do not send a command while a previous command is still being processed. | Datasheet, p. 7 |

## Measurement Data Conversion

| Word | Conversion | Source |
|---|---|---|
| `word[0]` | CO2 in ppm directly. | Datasheet, p. 9 |
| `word[1]` | `T_degC = -45 + 175 * word / (2^16 - 1)`. | Datasheet, p. 9 |
| `word[2]` | `RH_percent = 100 * word / (2^16 - 1)`. | Datasheet, p. 9 |

## Command Overview

| Command | Hex | Type | Time | During periodic measurement? | Source |
|---|---:|---|---:|---|---|
| `start_periodic_measurement` | `0x21b1` | send command | n/a | no | Datasheet, pp. 8-9 |
| `read_measurement` | `0xec05` | read | 1 ms | yes | Datasheet, pp. 8-9 |
| `stop_periodic_measurement` | `0x3f86` | send command | 500 ms | yes | Datasheet, pp. 8, 10 |
| `set_temperature_offset` / `get_temperature_offset` | `0x241d` / `0x2318` | write/read | 1 ms | no | Datasheet, pp. 8, 10-11 |
| `set_sensor_altitude` / `get_sensor_altitude` | `0x2427` / `0x2322` | write/read | 1 ms | no | Datasheet, pp. 8, 11 |
| `set_ambient_pressure` / `get_ambient_pressure` | `0xe000` / `0xe000` | write/read | 1 ms | yes | Datasheet, pp. 8, 12 |
| `perform_forced_recalibration` | `0x362f` | send command and fetch result | 400 ms | no | Datasheet, pp. 8, 13 |
| `set_automatic_self_calibration_enabled` / `get_automatic_self_calibration_enabled` | `0x2416` / `0x2313` | write/read | 1 ms | no | Datasheet, pp. 8, 13-14 |
| `start_low_power_periodic_measurement` | `0x21ac` | send command | n/a | no | Datasheet, pp. 8, 14 |
| `get_data_ready_status` | `0xe4b8` | read | 1 ms | yes | Datasheet, pp. 8, 15 |
| `persist_settings` | `0x3615` | send command | 800 ms | no | Datasheet, pp. 8, 15 |
| `get_serial_number` | `0x3682` | read | 1 ms | no | Datasheet, pp. 8, 16 |
| `perform_self_test` | `0x3639` | read | 10000 ms | no | Datasheet, pp. 8, 16 |
| `perform_factory_reset` | `0x3632` | send command | 1200 ms | no | Datasheet, pp. 8, 17 |
| `reinit` | `0x3646` | send command | 30 ms | no | Datasheet, pp. 8, 17 |
| `measure_single_shot` | `0x219d` | send command | 5000 ms | no | Datasheet, pp. 8, 18 |
| `measure_single_shot_rht_only` | `0x2196` | send command | 50 ms | no | Datasheet, pp. 8, 18 |
| `power_down` / `wake_up` | `0x36e0` / `0x36f6` | send command | 1 ms / 30 ms | no | Datasheet, pp. 8, 18-19 |

## Response Lengths And Encodings

| Command | Response bytes | Encoding | Source |
|---|---:|---|---|
| `read_measurement` (`0xec05`) | 9 | 3 words: CO2 ppm, temperature raw, RH raw; each word followed by CRC. | Datasheet, p. 9 |
| `get_temperature_offset` (`0x2318`) | 3 | `Toffset_degC = word * 175 / 65535`. | Datasheet, p. 11 |
| `get_sensor_altitude` (`0x2322`) | 3 | `word` is altitude in meters. | Datasheet, p. 11 |
| `get_ambient_pressure` (`0xe000`) | 3 | `ambient_pressure_Pa = word * 100`. | Datasheet, p. 12 |
| `perform_forced_recalibration` (`0x362f`) | 3 | `correction_ppm = word - 0x8000`; `0xffff` means failed FRC. | Datasheet, p. 13 |
| `get_automatic_self_calibration_enabled` (`0x2313`) | 3 | `0` disabled, `1` enabled. | Datasheet, p. 14 |
| `get_data_ready_status` (`0xe4b8`) | 3 | Ready when `word & 0x07ff` is nonzero. | Datasheet, p. 15 |
| `get_serial_number` (`0x3682`) | 9 | 48-bit serial number: `word0 << 32 | word1 << 16 | word2`. | Datasheet, p. 16 |
| `perform_self_test` (`0x3639`) | 3 | `0` means no malfunction; nonzero means malfunction detected. | Datasheet, p. 16 |
| ASC period getters (`0x2340`, `0x234b`) | 3 | Period in hours; valid settings are integer multiples of 4 h. | Datasheet, pp. 19-20 |

Write commands with one 16-bit input send 5 bytes after the I2C address: command MSB, command LSB, data MSB, data LSB, data CRC. Read commands send the 2-byte command, wait the listed execution time, then read the response bytes. Source: datasheet, pp. 7-8.
