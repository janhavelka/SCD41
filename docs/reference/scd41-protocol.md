# SCD41 Protocol Reference

Source: Sensirion SCD4x datasheet, version 1.7, April 2025, from the
[official SCD41 product page](https://sensirion.com/products/catalog/SCD41?show_inventory=SCD41-D-R2),
plus this repository's command table. The local vendor PDF is
`docs/reference/vendor/SCD41_datasheet.pdf` and is deliberately synchronized
to that current vendor revision.

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
| Variant ID | `get_sensor_variant` (`0x202F`) word bits `[15:12]`; SCD41 is `0x1` |
| Package | LGA SMD, 10.1 mm x 10.1 mm x 6.5 mm, about 0.6 g |

Variant encodings in the dedicated CRC-protected response:

| Variant | ID bits `[15:12]` | Notes |
| --- | --- | --- |
| SCD40 | `0x0` | Periodic CO2/T/RH, no SCD41-only low-power or single-shot commands |
| SCD41 | `0x1` | Target device for this library |
| SCD42 | not defined | Compatibility enum only; datasheet v1.7 does not assign `0x2` to SCD42 |
| SCD43 | `0x5` | Not targeted by this library |

Bits `[11:0]` are not a family discriminator and may differ. Vendor examples
are SCD40 `0x0440`/CRC `0x3F`, SCD41 `0x1440`/CRC `0x51`, and SCD43
`0x5441`/CRC `0xE9`. The driver retains the full CRC-verified word in
`Identity::variantWord` while policy uses only bits `[15:12]`.

Recommended board design:

- Place 100 nF near VDD/GND.
- Add bulk capacitance on the sensor rail; the measurement pulse can exceed
  175 mA.
- Keep the unloaded supply ripple below 30 mV peak-to-peak.
- Keep pullups, pin choice, clock speed, rail switching, and reset circuitry
  owned by the application or board layer.

## Performance Summary

Default conditions are 25 C, 50 %RH, 1013 mbar, periodic measurement, and
3.3 V supply unless stated otherwise.

| Signal | Range / Condition | Typical Specification |
| --- | --- | --- |
| CO2 output | representable output | 0 ppm to 40000 ppm |
| SCD41 CO2 accuracy | 400 ppm to 1000 ppm | +/-(50 ppm + 2.5% of reading) |
| SCD41 CO2 accuracy | 1001 ppm to 2000 ppm | +/-(50 ppm + 3% of reading) |
| SCD41 CO2 accuracy | 2001 ppm to 5000 ppm | +/-(40 ppm + 5% of reading) |
| CO2 repeatability | typical | +/-10 ppm |
| CO2 response time | tau63, typical | 60 s |
| Humidity | full range | 0 %RH to 100 %RH |
| Humidity accuracy | 15 C to 35 C, 20 %RH to 65 %RH | +/-6 %RH |
| Humidity accuracy | -10 C to 60 C, 0 %RH to 100 %RH | +/-9 %RH |
| Humidity repeatability / drift | typical | +/-0.4 %RH, <0.25 %RH/year |
| Temperature | full range | -10 C to 60 C |
| Temperature accuracy | 15 C to 35 C | +/-0.8 C |
| Temperature accuracy | -10 C to 60 C | +/-1.5 C |
| Temperature repeatability / drift | typical | +/-0.1 C, <0.03 C/year |

CO2 exposure below 400 ppm can affect accuracy when ASC is enabled. Rough
handling, shipping, and assembly can temporarily affect accuracy; FRC or ASC can
restore accuracy after the post-assembly stabilization period.

## Interface, Electrical, And Handling Notes

| LGA signal | Meaning |
| --- | --- |
| `VDD` | Sensor supply voltage |
| `VDDH` | IR source supply; connect to `VDD` close to the sensor |
| `GND` | Ground |
| `SDA` | I2C serial data, bidirectional |
| `SCL` | I2C serial clock |
| `DNC` | Do not connect; solder to floating pads only |

There is no dedicated interrupt/data-ready pin. Data readiness is checked
through `get_data_ready_status`.

| Electrical item | Value |
| --- | --- |
| Supply voltage | 2.4 V to 5.5 V |
| Peak current at 3.3 V | 175 mA typical, 205 mA max |
| Peak current at 5.0 V | 115 mA typical, 137 mA max |
| Average current, periodic 5 s, 3.3 V | 15 mA typical, 18 mA max |
| Average current, low-power 30 s, 3.3 V | 3.2 mA typical, 3.5 mA max |
| Average current, single-shot 5 min, 3.3 V | 0.45 mA typical, 0.5 mA max |
| Input high | at least `0.65 * VDD` |
| Input low | at most `0.3 * VDD` |
| Output low | at most 0.66 V at 3 mA sink |
| SCL clock | 0 kHz to 400 kHz |

Absolute and handling limits to preserve in board/application docs:

- VDD absolute maximum: -0.3 V to 6.0 V.
- SDA/SCL/GND voltage: -0.3 V to `VDD + 0.3 V`.
- SDA/SCL/GND input current: -280 mA to 100 mA.
- Short-term storage: -40 C to 70 C; recommended storage: 10 C to 50 C.
- ESD: 2 kV HBM, 500 V CDM.
- MSL level: 1, per IPC/JEDEC J-STD-033B1. Follow the vendor's MSL1 handling
  and out-of-bag manufacturing conditions.
- Expected lifetime is greater than 10 years under typical indoor conditions.
- Device is REACH/RoHS compliant.

Mechanical and assembly notes:

- The notched protection-membrane corner marks pin 1.
- Do not remove, wet, or tamper with the white protection membrane.
- The device is not compatible with vapor phase reflow.
- Do not add extra flux, reflow more than once, or run board wash after reflow.
- Reflow peak must not exceed 245 C anywhere in the sensor; temporary CO2
  accuracy deviation after reflow can recover after up to five days.

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

Commands must not be sent while a preceding command is still being processed.
For read or send-command-and-fetch-result sequences, wait the documented
execution time before issuing the read header.

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

Command words are not followed by CRC. The vendor CRC example gives
`CRC(0xBEEF) == 0x92`; keep this vector in tests when CRC helpers change.

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

During command execution the sensor may NACK read attempts. The driver models
these delays as observable, zero-I2C pending phases driven by caller polling,
not hidden waits or early read retries.

The `wake_up` command can NACK while still waking the sensor. Attach
reconciliation may also see a stop-command NACK when periodic mode was not
active. The driver marks only these expected attempts with
`TransferIntent::EXPECTED_WRITE_NACK`. A generic transport NACK is accepted in
a marked phase because common controllers do not expose address/data NACK
precision. Timeout, bus fault, short transfer, and generic non-NACK failure
remain failures.

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

`read_measurement` can read out each sample only once per signal update
interval; the sensor empties the buffer on readout. If no data is available,
the sensor can NACK the read. Poll `get_data_ready_status` first when the
application wants to avoid no-data NACKs.

The shortest single-shot measurement interval is 5 seconds. Averaging several
single-shot measurements can reduce noise. ASC is not available for
power-cycled single-shot operation. Datasheet v1.7 removed the older
recommendation to discard the first single-shot value after a power cycle; any
warm-up/publication filter is application policy.

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
temperature_mdegC = round(175000 * raw / 65535) - 45000;
humidity_milliPct = round(100000 * raw / 65535);
```

The 64-bit intermediates preserve the datasheet denominator exactly, including
the full-scale endpoints of 130000 mC and 100000 milli-percent RH.

Temperature offset command encoding uses the datasheet scale:

```text
raw_offset = offset_C * 65535 / 175
offset_C   = raw_offset * 175 / 65535
```

Compensation ranges and effects:

| Setting | Range / Default | Notes |
| --- | --- | --- |
| Temperature offset | encoded 0 C to 175 C; recommended 0 C to 20 C; default 4 C | Improves RH/T output; does not affect CO2 accuracy |
| Sensor altitude | 0 m to 3000 m, default 0 m | Idle-only setting; persist if it must survive power cycle |
| Ambient pressure | 70000 Pa to 120000 Pa, default 101300 Pa | May be set/read during periodic measurement; overrides altitude compensation |

Pressure or altitude compensation improves CO2 accuracy across pressure
changes. Determine temperature offset in the final device under typical thermal
conditions and airflow.

The 0..20 C offset guidance is a recommendation, not the protocol domain. The
ASC target and FRC reference are uint16 ppm words with no narrower valid range
stated in datasheet v1.7. Returned settings with explicit domains are accepted
only for altitude 0..3000 m, pressure words 700..1200, ASC enable 0/1, and ASC
periods divisible by 4. A CRC-valid word outside one of those explicit domains
is still a protocol failure and must not enter a verified cache.

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
| `get_automatic_self_calibration_target` | `0x233F` | 1 word | short | no |
| `get_data_ready_status` | `0xE4B8` | 1 word | short | yes |
| `persist_settings` | `0x3615` | none | 800 ms | no |
| `get_serial_number` | `0x3682` | 3 words | short | no |
| `perform_self_test` | `0x3639` | 1 word result | 10000 ms | no |
| `perform_factory_reset` | `0x3632` | none | 1200 ms | no |
| `reinit` | `0x3646` | none | 30 ms | no |
| `get_sensor_variant` | `0x202F` | 1 word; variant in bits `[15:12]` | 1 ms | no |
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
- FRC should be performed in the operation mode later used by the application,
  at the intended application voltage, in a homogeneous and constant CO2
  concentration. Operate for at least 3 minutes in periodic modes, or for more
  than 3 single shots at a 1-minute interval. Apply any altitude or pressure
  compensation beforehand, then stop periodic measurement and wait 500 ms
  before issuing the FRC command.
- FRC returns `word[0] - 0x8000` as the correction in ppm, or `0xFFFF` when FRC
  failed.
- Self-test returns `0x0000` when no malfunction is detected; any nonzero word
  indicates a malfunction.
- ASC initial period defaults to 44 h. ASC standard period defaults to 156 h.
  Both are integer multiples of 4 h; an initial period of 0 h requests immediate
  correction. The period guidance assumes about a 5 minute average single-shot
  interval and should be scaled for other intervals.
- `persist_settings` writes EEPROM. It must never run implicitly from examples,
  diagnostics, or normal telemetry loops.
- EEPROM-backed configuration storage is rated for at least 2000 write cycles.
  FRC/ASC history is stored separately for the specified sensor lifetime.
- `factory_reset` changes persisted calibration/settings state and must require
  explicit operator confirmation.
- `reinit` reloads user settings from EEPROM. If it does not trigger the desired
  reinitialization, apply an application-controlled power cycle.
- Destructive or EEPROM-backed commands belong in opt-in workflows with
  transcripts, not safe smoke tests.

## Library Policy Derived From The Device

- Core code remains framework-neutral and uses one injected, single-attempt I2C
  transport callback. The application supplies the monotonic time to operations
  and polls.
- `begin()` binds without I2C. Every physical transfer occurs only while the
  caller advances an admitted typed operation through `poll()`.
- Long execution times are zero-I2C operation phases. Cancellation and deadline
  expiry stop future host phases but cannot undo an accepted sensor command.
- Every terminal result is correlated by request ID and generation and consumed
  exactly once.
- Effectful writes are never blindly retried. Partial or ambiguous completion
  remains visible through outcome, effect, dirty cache, and reconciliation state.
- `OFFLINE` is passive local diagnostic state, not aggregate application health
  and not driver-owned recovery authority.
- Diagnostic helpers have explicit word/command shapes. Returned words remain
  CRC-checked, and diagnostic effects invalidate or dirty managed cache state.
- Typed stop-periodic is rejected without I2C when the reconciled mode is
  already idle. Only attach uses stop with expected-NACK semantics because its
  purpose is to reconcile an unknown retained hardware mode.
- `SCD41::limits(OperationKind)` publishes the bounded callback, retry, sensor
  wait, nonvolatile, and destructive contract for every operation.
