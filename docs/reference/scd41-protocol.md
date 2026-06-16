# SCD41 Protocol Reference

Source: Sensirion SCD4x datasheet, version 1.5, July 2023, plus this
repository's command table. The local vendor PDF is
`docs/reference/vendor/SCD41_datasheet.pdf`.

This file is the compact device-behavior reference for implementation work. It
is not a replacement for the vendor datasheet.

## Device Facts

| Item | Value |
| --- | --- |
| Sensor | Sensirion SCD41 photoacoustic NDIR CO2, temperature, humidity |
| I2C address | Fixed 7-bit address `0x62` |
| I2C speed | Standard/Fast mode up to 400 kHz |
| Supply voltage | 2.4 V to 5.5 V |
| I2C levels | Follow VDD |
| Operating range | -10 C to 60 C, 0 %RH to 95 %RH non-condensing |
| CO2 output range | 0 ppm to 40000 ppm |
| Variant ID | Serial-number word 0 bits `[15:12]`; SCD41 is `0x1` |

Recommended board design:

- Place 100 nF near VDD/GND.
- Add bulk capacitance on the sensor rail; the measurement pulse can exceed
  175 mA.
- Keep pullups, pin choice, clock speed, rail switching, and reset circuitry
  owned by the application or board layer.

## I2C Framing

Commands are 16-bit command words, sent MSB first. Payload and response words
are 16-bit words followed by one CRC byte per word.

Command-only write:

```text
S 0x62-W ACK cmd_msb ACK cmd_lsb ACK P
```

Command plus one data word:

```text
S 0x62-W ACK cmd_msb ACK cmd_lsb ACK data_msb ACK data_lsb ACK crc ACK P
```

Read after command execution:

```text
S 0x62-R ACK data_msb ACK data_lsb ACK crc ACK ... P
```

Many reads are split by design: write the command, wait the command execution
time, then perform a read with no repeated command bytes. Do not collapse those
into a combined transaction unless the driver explicitly asks for one.

## CRC

Every returned 16-bit word must be CRC-checked. Every written 16-bit payload
word must carry the matching CRC byte.

| Parameter | Value |
| --- | --- |
| Width | 8 bits |
| Polynomial | `0x31` |
| Initial value | `0xFF` |
| Reflect input/output | No |
| Final XOR | `0x00` |

CRC is computed over the two data bytes of each word.

## Timing Rules

| Rule | Timing |
| --- | --- |
| Power-up settle before first command | up to 30 ms |
| Minimum command spacing `tIDLE` | at least 1 ms |
| Short command execution | 1 ms |
| Stop periodic settle | 500 ms |
| Wake-up settle after expected NACK | up to 30 ms |
| Single-shot RHT-only execution | 50 ms |
| Full single-shot execution | 5000 ms |
| Forced recalibration execution | 400 ms |
| Persist settings execution | 800 ms |
| Factory reset execution | 1200 ms |
| Self-test execution | 10000 ms |

During command execution the sensor may NACK read attempts. The driver must
model these delays as observable pending work or explicit diagnostic blocking
APIs, not hidden unbounded waits.

The `wake_up` command can NACK while still waking the sensor. Treat only precise
address/data NACK statuses as expected wake behavior. Generic I2C errors,
timeouts, bus faults, and read-header NACKs remain failures.

## Measurement Modes

| Mode | Command | Cadence / Execution | Notes |
| --- | --- | --- | --- |
| Periodic | `0x21B1` | 1 sample every 5 s | Full CO2/T/RH |
| Low-power periodic | `0x21AC` | 1 sample every 30 s | SCD41-only |
| Single-shot full | `0x219D` | 5000 ms | SCD41-only |
| Single-shot RHT-only | `0x2196` | 50 ms | SCD41-only, CO2 invalid |

While periodic measurement is active, only these commands are allowed without
first stopping measurement:

- `read_measurement`
- `get_data_ready_status`
- `set_ambient_pressure`
- `get_ambient_pressure`
- `stop_periodic_measurement`

`stop_periodic_measurement` requires the full 500 ms settle window before
idle-only commands are issued.

## Data-Ready And Conversion

`get_data_ready_status` returns one CRC-protected word. Data is ready when:

```cpp
(word & 0x07FF) != 0
```

`read_measurement` returns three CRC-protected words:

| Word | Meaning | Conversion |
| --- | --- | --- |
| 0 | CO2 ppm | Raw word is ppm |
| 1 | Temperature | `-45 + 175 * raw / 65535` C |
| 2 | Relative humidity | `100 * raw / 65535` %RH |

Fixed-point conversions used by this library:

```cpp
temperature_mdegC = ((21875 * raw) >> 13) - 45000;
humidity_milliPct = (12500 * raw) >> 13;
```

Temperature offset command encoding uses the datasheet scale:

```text
raw_offset = offset_C * 65535 / 175
offset_C   = raw_offset * 175 / 65535
```

## Command Summary

| Command | Code | Response / Data | Execution | Periodic Allowed |
| --- | --- | --- | --- | --- |
| `start_periodic_measurement` | `0x21B1` | none | short | no |
| `read_measurement` | `0xEC05` | 3 words | short | yes |
| `stop_periodic_measurement` | `0x3F86` | none | 500 ms | yes |
| `set_temperature_offset` | `0x241D` | 1 word write | short | no |
| `get_temperature_offset` | `0x2318` | 1 word | short | no |
| `set_sensor_altitude` | `0x2427` | 1 word write | short | no |
| `get_sensor_altitude` | `0x2322` | 1 word | short | no |
| `set_ambient_pressure` | `0xE000` | 1 word write | short | yes |
| `get_ambient_pressure` | `0xE000` | 1 word | short | yes |
| `perform_forced_recalibration` | `0x362F` | 1 word result | 400 ms | no |
| `set_automatic_self_calibration_enabled` | `0x2416` | 1 word write | short | no |
| `get_automatic_self_calibration_enabled` | `0x2313` | 1 word | short | no |
| `set_automatic_self_calibration_target` | `0x243A` | 1 word write | short | no |
| `get_automatic_self_calibration_target` | `0x233B` | 1 word | short | no |
| `get_data_ready_status` | `0xE4B8` | 1 word | short | yes |
| `persist_settings` | `0x3615` | none | 800 ms | no |
| `get_serial_number` | `0x3682` | 3 words | short | no |
| `perform_self_test` | `0x3639` | 1 word result | 10000 ms | no |
| `perform_factory_reset` | `0x3632` | none | 1200 ms | no |
| `reinit` | `0x3646` | none | 30 ms | no |
| `set_asc_initial_period` | `0x2445` | 1 word write | short | no |
| `get_asc_initial_period` | `0x2340` | 1 word | short | no |
| `set_asc_standard_period` | `0x244E` | 1 word write | short | no |
| `get_asc_standard_period` | `0x234B` | 1 word | short | no |
| `measure_single_shot` | `0x219D` | deferred measurement | 5000 ms | no |
| `measure_single_shot_rht_only` | `0x2196` | deferred RHT sample | 50 ms | no |
| `power_down` | `0x36E0` | none | short | no |
| `wake_up` | `0x36F6` | expected NACK possible | 30 ms settle | no |

## Calibration And Persistence

- Automatic self-calibration (ASC) is enabled by default and assumes regular
  exposure to fresh-air CO2 near the configured target.
- Forced recalibration requires a stable known reference concentration.
- `persist_settings` writes EEPROM. It must never run implicitly from examples,
  diagnostics, or normal telemetry loops.
- `factory_reset` changes persisted calibration/settings state and must require
  explicit operator confirmation.
- Destructive or EEPROM-backed commands belong in opt-in workflows with
  transcripts, not safe smoke tests.

## Library Policy Derived From The Device

- Core code remains framework-neutral and uses injected I2C transport and time.
- Fallible APIs return `Status`; asynchronous completion failures are surfaced
  through `tick()` or `poll()` and retained in async result accessors.
- `probe()` is diagnostic and health-clean.
- `OFFLINE` is a local driver transport latch, not aggregate application health.
- Raw byte command helpers are diagnostic-only. Prefer typed word helpers so CRC
  checks remain automatic.
