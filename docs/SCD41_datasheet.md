# SCD41 — Photoacoustic NDIR CO₂, Temperature & Humidity Sensor — Implementation Manual

> **Source:** Sensirion SCD4x Datasheet (SCD40 / SCD41), Sensirion embedded-i2c-scd4x reference driver  
> **Relevance:** Complete register-level, timing, electrical, and algorithmic reference for building a production-grade SCD41 driver.

---

## Key Takeaways

- I²C address is **fixed at 0x62**; no address-pin configuration.
- All commands are **16-bit, MSB-first**; every data word returned is followed by a **CRC-8** byte.
- Three measurement modes: **periodic** (5 s), **low-power periodic** (30 s), and **single-shot** (SCD41 only).
- Sensor requires **≤ 30 ms power-up** time before first command.
- Calibration data is stored in on-chip EEPROM with **≥ 2000 write cycles**; `persist_settings` saves runtime config.
- Automatic Self-Calibration (ASC) is **enabled by default** — assumes weekly exposure to 400 ppm fresh air.
- Signal conversion is integer-only: no floating-point required on device side.
- Peak current draw is **175–205 mA at 3.3 V** during the photoacoustic measurement pulse; average is **15 mA** in periodic mode.
- The sensor returns **NACK on wake-up command** — this is expected behavior, not an error.
- Sensor variant can be identified by reading the serial number: bits [15:12] of the first word encode the variant (SCD40=0x0, SCD41=0x1).

---

## 1. Device Overview

| Parameter | Value |
|---|---|
| Sensor type | Photoacoustic NDIR CO₂ + integrated SHT4x (T/RH) |
| I²C address | 0x62 (fixed, 7-bit) |
| Supply voltage (VDD) | 2.4 V – 5.5 V |
| I²C voltage levels | Follow VDD |
| Operating temperature | –10 °C to +60 °C |
| Storage temperature | –20 °C to +70 °C |
| I²C clock frequency | 0 – 400 kHz (standard/fast mode) |
| Package | 10.1 × 10.1 × 6.5 mm³ (LGA, SMD) |
| Weight | approx. 0.6 g |

### Sensor Variants

| Variant | ID bits [15:12] | Single-shot support | Low-power periodic |
|---|---|---|---|
| SCD40 | 0x0 | No | No |
| SCD41 | 0x1 | **Yes** | **Yes** |
| SCD42 | 0x2 | No | No |
| SCD43 | 0x5 | No | No |

Variant detection: read serial number via `get_serial_number` (0x3682), inspect bits [15:12] of the first 16-bit word.

---

## 2. Electrical Specifications

### Power Consumption

| Condition | VDD = 3.3 V | VDD = 5.0 V |
|---|---|---|
| Peak current | 205 mA | 175 mA |
| Average (periodic, 5 s) | 15 mA | 18 mA |
| Average (low-power, 30 s) | 3.2 mA | 3.8 mA |
| Average (single-shot on-demand) | 0.45 mA | 0.5 mA |
| Idle current | 0.4 mA | 0.55 mA |

### I²C Electrical Levels

| Parameter | Min | Max | Unit |
|---|---|---|---|
| VIH (input high) | 0.65 × VDD | VDD | V |
| VIL (input low) | — | 0.3 × VDD | V |
| VOL (output low, 3 mA sink) | — | 0.66 | V |
| Supply voltage ripple (unloaded, peak-to-peak) | — | 30 | mV |

### Absolute Maximum Ratings

| Parameter | Min | Max | Unit |
|---|---|---|---|
| DC supply voltage (VDD) | –0.3 | 6.0 | V |
| Max voltage on SDA, SCL, GND | –0.3 | VDD + 0.3 | V |
| Input current on SDA, SCL, GND | –280 | 100 | mA |
| ESD HBM (pads and metal cap) | — | 2000 | V |
| ESD CDM | — | 500 | V |
| MSL level | — | 3 | — |
| Operating humidity | 0 | 95 (non-cond.) | %RH |
| Short-term storage temperature | –40 | 70 | °C |
| Sensor lifetime (typical conditions) | — | >10 years | — |

### Recommended Bypass

- 100 nF close to VDD/GND.
- Bulk capacitor: 10 µF on the supply rail to handle the 175–205 mA current pulse.
- Supply must sustain the peak current without voltage droop below 2.4 V.

---

## 2b. Sensor Performance

### CO₂ Accuracy (SCD41)

| CO₂ Range | Accuracy |
|---|---|
| 400 – 1000 ppm | ± (50 ppm + 2.5% of reading) |
| 1001 – 2000 ppm | ± (50 ppm + 3% of reading) |
| 2001 – 5000 ppm | ± (40 ppm + 5% of reading) |

### CO₂ Accuracy (SCD40)

| CO₂ Range | Accuracy |
|---|---|
| 400 – 2000 ppm | ± (50 ppm + 5% of reading) |

CO₂ output range: 0 – 40,000 ppm (all variants). Repeatability: ± 10 ppm (typ). Response time (τ63%): 60 s (typ).

Accuracy drift after 5 years with ASC: ± (5 ppm + 0.5% of reading) typ, ± (5 ppm + 2% of reading) max.

### Humidity Accuracy

| Conditions | Accuracy |
|---|---|
| 15 – 35 °C, 20 – 65 %RH | ± 6 %RH |
| –10 – 60 °C, 0 – 100 %RH | ± 9 %RH |

Range: 0 – 100 %RH. Repeatability: ± 0.4 %RH. Response time (τ63%): 90 s (typ). Drift: < 0.25 %RH/year.

### Temperature Accuracy

| Conditions | Accuracy |
|---|---|
| 15 – 35 °C | ± 0.8 °C |
| –10 – 60 °C | ± 1.5 °C |

Range: –10 – 60 °C. Repeatability: ± 0.1 °C. Response time (τ63%): 120 s (typ). Drift: < 0.03 °C/year.

---

## 3. I²C Protocol

### Bus Configuration

- 7-bit address: **0x62**
- Clock: 0 Hz – 400 kHz
- Data: MSB-first, 16-bit command words
- Pull-ups: external, standard values for 400 kHz

### Command Format

All commands are 16-bit, sent MSB-first as two bytes. No register address pointer — commands are self-contained.

**Write (command only):**
```
[S] [0x62 << 1 | 0] [ACK] [cmd_MSB] [ACK] [cmd_LSB] [ACK] [P]
```

**Write (command + data):**
```
[S] [0x62 << 1 | 0] [ACK] [cmd_MSB] [ACK] [cmd_LSB] [ACK]
    [data_MSB] [ACK] [data_LSB] [ACK] [CRC] [ACK] [P]
```

**Read (after waiting execution time):**
```
[S] [0x62 << 1 | 1] [ACK] [data0_MSB] [ACK] [data0_LSB] [ACK] [CRC0] [ACK]
    [data1_MSB] [ACK] [data1_LSB] [ACK] [CRC1] [ACK] ... [P]
```

### CRC-8 Calculation

| Parameter | Value |
|---|---|
| Polynomial | 0x31 (x⁸ + x⁵ + x⁴ + 1) |
| Initialization | 0xFF |
| Width | 8 bits |
| Reflect in/out | No |
| Final XOR | 0x00 |

CRC is computed over each 16-bit data word (2 bytes). The CRC byte follows immediately after each word.

**Reference C implementation:**
```c
uint8_t sensirion_common_generate_crc(const uint8_t* data, uint16_t count) {
    uint16_t current_byte;
    uint8_t crc = 0xFF;  // init
    uint8_t crc_bit;
    for (current_byte = 0; current_byte < count; ++current_byte) {
        crc ^= data[current_byte];
        for (crc_bit = 8; crc_bit > 0; --crc_bit) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}
```

### I²C Timing Notes

- After sending a command, the host must wait the **execution time** before reading.
- During execution, the sensor will NACK read attempts — this is normal.
- Minimum idle time between commands: **1 ms** (tIDLE) — enforced by sensor.
- The `wake_up` command does **not get ACKed** — the sensor NACKs but wakes up. This is expected behavior.

---

## 4. Command Reference

### Complete Command Table

| Command | Code | Type | Exec Time | During Meas. | Description |
|---|---|---|---|---|---|
| `start_periodic_measurement` | 0x21B1 | Write | — | No | Start periodic mode (1 measurement/5 s) |
| `read_measurement` | 0xEC05 | Read 9 bytes | 1 ms | Yes | Read CO₂, T, RH (3 words + CRC each). NACKs if buffer empty |
| `stop_periodic_measurement` | 0x3F86 | Write | 500 ms | Yes | Stop periodic/LP periodic mode |
| `set_temperature_offset` | 0x241D | Write + data | 1 ms | No | Set T offset; word = offset_°C × 65536 / 175 |
| `get_temperature_offset` | 0x2318 | Read 3 bytes | 1 ms | No | Read current T offset; °C = word × 175 / 65535 |
| `set_sensor_altitude` | 0x2427 | Write + data | 1 ms | No | Set altitude (0–3000 m) for pressure comp. |
| `get_sensor_altitude` | 0x2322 | Read 3 bytes | 1 ms | No | Read altitude setting (meters) |
| `set_ambient_pressure` | 0xE000 | Write + data | 1 ms | **Yes** | Override pressure comp.; word = Pa / 100 (700–1200) |
| `get_ambient_pressure` | 0xE000 | Read 3 bytes | 1 ms | **Yes** | Read ambient pressure; Pa = word × 100 |
| `perform_forced_recalibration` | 0x362F | Write + Read 3 | 400 ms | No | FRC with reference CO₂ (ppm). Returns correction or 0xFFFF |
| `set_automatic_self_calibration_enabled` | 0x2416 | Write + data | 1 ms | No | 1 = enabled (default), 0 = disabled |
| `get_automatic_self_calibration_enabled` | 0x2313 | Read 3 bytes | 1 ms | No | Read ASC status (1/0) |
| `set_automatic_self_calibration_target` | 0x243A | Write + data | 1 ms | No | Set ASC target ppm (SCD41 only, default 400) |
| `get_automatic_self_calibration_target` | 0x233B | Read 3 bytes | 1 ms | No | Read ASC target ppm |
| `start_low_power_periodic_measurement` | 0x21AC | Write | — | No | Start LP periodic (1/30 s, SCD41 only) |
| `get_data_ready_status` | 0xE4B8 | Read 3 bytes | 1 ms | Yes | Check if new data available |
| `persist_settings` | 0x3615 | Write | 800 ms | No | Save T offset, altitude, ASC to EEPROM |
| `get_serial_number` | 0x3682 | Read 9 bytes | 1 ms | No | Read 48-bit serial (3 words) |
| `perform_self_test` | 0x3639 | Read 3 bytes | 10000 ms | No | Self-test; 0x0000 = pass, else malfunction |
| `perform_factory_reset` | 0x3632 | Write | 1200 ms | No | Reset all EEPROM to factory + erase cal history |
| `reinit` | 0x3646 | Write | 30 ms | No | Reload settings from EEPROM to RAM |
| `set_automatic_self_calibration_initial_period` | 0x2445 | Write + data | 1 ms | No | Set ASC initial period (hours, multiples of 4) |
| `get_automatic_self_calibration_initial_period` | 0x2340 | Read 3 bytes | 1 ms | No | Read ASC initial period (hours) |
| `set_automatic_self_calibration_standard_period` | 0x244E | Write + data | 1 ms | No | Set ASC standard period (hours, multiples of 4) |
| `get_automatic_self_calibration_standard_period` | 0x234B | Read 3 bytes | 1 ms | No | Read ASC standard period (hours) |
| `measure_single_shot` | 0x219D | Write | 5000 ms | No | Single-shot CO₂+T+RH (SCD41 only) |
| `measure_single_shot_rht_only` | 0x2196 | Write | 50 ms | No | Single-shot T+RH only; CO₂ returns 0 ppm (SCD41 only) |
| `power_down` | 0x36E0 | Write | 1 ms | No | Enter deep sleep (lowest power) |
| `wake_up` | 0x36F6 | Write | 30 ms | No | Wake from power-down (**NACKed — expected**) |

### Command Execution Time Summary

| Timing Category | Commands | Execution Time |
|---|---|---|
| Ultra-fast (1 ms) | Most get/set, read_measurement, data_ready, power_down | 1 ms |
| Wake/reinit (30 ms) | wake_up, reinit | 30 ms |
| Single-shot RHT (50 ms) | measure_single_shot_rht_only | 50 ms |
| Calibration (400 ms) | perform_forced_recalibration | 400 ms |
| Config stop (500 ms) | stop_periodic_measurement | 500 ms |
| EEPROM save (800 ms) | persist_settings | 800 ms |
| Factory reset (1200 ms) | perform_factory_reset | 1200 ms |
| Single-shot full (5000 ms) | measure_single_shot | 5000 ms |
| Self-test (10000 ms) | perform_self_test | 10000 ms |

---

## 5. Measurement Modes

### 5.1 Periodic Measurement (Standard)

- Start: `start_periodic_measurement` (0x21B1)
- Interval: **5 seconds** between measurements
- Stop: `stop_periodic_measurement` (0x3F86) — must wait **500 ms** after stop
- Data readout: poll `get_data_ready_status` (0xE4B8), then `read_measurement` (0xEC05)
- Average current: **15 mA at 3.3 V**

### 5.2 Low-Power Periodic Measurement (SCD41 Only)

- Start: `start_low_power_periodic_measurement` (0x21AC)  
- Interval: **30 seconds** between measurements
- Stop: same as periodic — `stop_periodic_measurement`
- Data readout: same as periodic
- Average current: **3.2 mA at 3.3 V**
- CO₂ accuracy slightly reduced vs standard periodic

### 5.3 Single-Shot Measurement (SCD41 Only)

- Full measurement (CO₂ + T + RH): `measure_single_shot` (0x219D) — **5000 ms** execution
- T + RH only: `measure_single_shot_rht_only` (0x2196) — **50 ms** execution
- After measurement completes, data is available via `read_measurement`
- **Note:** `measure_single_shot_rht_only` returns CO₂ as **0 ppm** — not a real measurement
- Sensor returns to idle after measurement
- Average current: **0.45 mA at 3.3 V** (one-shot on-demand)
- For reliable CO₂ readings: perform **≥ 3 single-shot measurements** (first may be inaccurate)
- After power cycle, discard initial single-shot CO₂ reading
- ASC in single-shot mode is optimized for 5-minute measurement intervals
- For power-cycled single-shot operation (VDD cut between shots), ASC is **not available**

### 5.4 Power-Down / Wake-Up

- `power_down` (0x36E0): enter lowest power state, **1 ms** exec time
- `wake_up` (0x36F6): exit power-down, **30 ms** exec time
- **IMPORTANT:** `wake_up` is NACKed by the sensor — this is by design, not an error
- After wake-up, the sensor is in idle state; previous periodic mode is NOT resumed

---

## 6. Data Ready Check

Command: `get_data_ready_status` (0xE4B8)

Returns a 16-bit status word + CRC:
```
data_ready = (status_word & 0x07FF) != 0
```
- If the **lower 11 bits are all zero**, data is NOT ready.
- If **any of the lower 11 bits are set**, new data is available for readout.

---

## 7. Signal Conversion Formulas

### Read Measurement Output

`read_measurement` (0xEC05) returns 9 bytes = 3 words with CRC:

| Word | Content | Bytes |
|---|---|---|
| Word 0 | CO₂ raw | MSB, LSB, CRC |
| Word 1 | Temperature raw | MSB, LSB, CRC |
| Word 2 | Humidity raw | MSB, LSB, CRC |

### Conversion Formulas

```
CO₂ [ppm] = word[0]                            (direct, unsigned 16-bit)

Temperature [°C] = -45 + 175 × word[1] / (2¹⁶ - 1)

Relative Humidity [%RH] = 100 × word[2] / (2¹⁶ - 1)
```

### Integer-Only Conversion (from Sensirion reference driver)

```c
// Temperature in milli-degrees Celsius
int32_t temperature_m_deg_c = ((21875 * (int32_t)temperature_raw) >> 13) - 45000;

// Humidity in milli-percent RH
int32_t humidity_m_percent_rh = ((12500 * (int32_t)humidity_raw) >> 13);
```

These integer formulas avoid floating-point entirely and produce results in milli-units.

---

## 8. CO₂ Measurement Accuracy

*(Detailed specs in Section 2b above. Summary repeated here for readout context.)*

---

## 9. Temperature Offset Compensation

The SCD4x has a self-heating effect. The **default temperature offset is 4 °C**.

### Offset Calculation Formula

```
T_offset_actual = T_SCD4x - T_Reference + T_offset_previous
```

Where:
- `T_SCD4x` = temperature reported by SCD4x with current offset
- `T_Reference` = actual ambient temperature from a reference sensor
- `T_offset_previous` = currently configured offset (default 4 °C)

### Commands

- Set offset: `set_temperature_offset` (0x241D) — value = offset × 2¹⁶ / 175
- Get offset: `get_temperature_offset` (0x2318)
- Offset is **volatile** — lost on power cycle unless saved with `persist_settings`

### Encoding

```
word = (uint16_t)(offset_deg_c * 65536.0 / 175.0)
```

---

## 10. Pressure / Altitude Compensation

CO₂ measurement depends on ambient pressure. Two methods to compensate:

### Method 1: Sensor Altitude

- Set: `set_sensor_altitude` (0x2427) — value in meters above sea level
- Get: `get_sensor_altitude` (0x2322)
- Default: 0 m (sea level)
- Volatile — save with `persist_settings`

### Method 2: Ambient Pressure Override

- Set: `set_ambient_pressure` (0xE000) — value = pressure_Pa / 100 (i.e., in hPa)
- Example: 98,700 Pa → send 987 (0x03DB)
- Valid range: **700 – 1200** (70,000 – 120,000 Pa)
- Default: **1013** (101,300 Pa)
- Can be updated during periodic measurement (does not require stop)
- Overrides altitude-based compensation

---

## 11. Calibration

### 11.1 Automatic Self-Calibration (ASC)

- **Enabled by default**
- Assumes sensor sees **400 ppm fresh air at least once per week**
- Initial calibration period: **44 hours** (default, must be multiple of 4 hours)
- Standard calibration period: **156 hours** (default, must be multiple of 4 hours)
- ASC target: **400 ppm** (configurable on SCD41)
- Period values assume 5-minute average measurement interval in single-shot mode
- For other intervals, scale inversely: e.g., 10-min interval → multiply period by 0.5
- A value of 0 for the initial period triggers immediate correction

**Commands:**
- Enable/disable: `set_automatic_self_calibration_enabled` (0x2416) — 0 = disabled, 1 = enabled
- Set target: `set_automatic_self_calibration_target` (0x243A)
- Set initial period: `set_automatic_self_calibration_initial_period` (0x2445) — value in hours
- Set standard period: `set_automatic_self_calibration_standard_period` (0x244E) — value in hours

### 11.2 Forced Recalibration (FRC)

- Command: `perform_forced_recalibration` (0x362F)
- Write reference CO₂ concentration (ppm) as argument
- Execution time: **400 ms**
- Returns FRC correction value (0xFFFF if failed)
- Sensor must be in idle state (stop periodic first, wait 500 ms)
- Expose sensor to known reference gas for **≥ 3 minutes** before FRC
- FRC calibration is stored **automatically** — no `persist_settings` needed

### FRC Response

The returned 16-bit word:
- `0xFFFF` → FRC failed
- Any other value → FRC correction applied: `correction = word - 0x8000` (signed offset)

---

## 12. EEPROM Persistence

### persist_settings (0x3615)

- Execution time: **800 ms**
- Saves the following to EEPROM:
  - Temperature offset
  - Sensor altitude
  - ASC enabled/disabled
  - ASC target (SCD41)
  - ASC initial/standard periods
- EEPROM endurance: **≥ 2000 write cycles**
- Calibration data (ASC/FRC corrections) are stored separately and automatically

### reinit (0x3646)

- Execution time: **30 ms**
- Reloads all settings from EEPROM into RAM
- Does NOT reset the sensor — only reloads persisted configuration
- Useful after changing settings without power cycling

### perform_factory_reset (0x3632)

- Execution time: **1200 ms**
- Resets ALL EEPROM settings to factory defaults
- Erases FRC/ASC calibration history
- Sensor enters idle state after reset

---

## 13. Self-Test

Command: `perform_self_test` (0x3639)

- Execution time: **10,000 ms** (10 seconds)
- Returns 16-bit result word + CRC
- Result = 0x0000 → **pass**
- Result ≠ 0x0000 → **sensor malfunction detected**
- Must be in idle state (not in periodic measurement)

---

## 14. Serial Number

Command: `get_serial_number` (0x3682)

- Returns 9 bytes: 3 × (16-bit word + CRC)
- 48-bit unique serial number
- First word bits [15:12] encode sensor variant (see Section 1)
- Must be in idle state

---

## 15. Driver State Machine Guidance

### Power-Up Sequence

1. Wait **≤ 30 ms** after power-on
2. Optionally read serial number to verify presence
3. Optionally configure: temperature offset, altitude, ASC settings
4. Start measurement mode

### Periodic Mode Lifecycle

```
IDLE → start_periodic_measurement → MEASURING
  ↓ (every 5 s)
  poll get_data_ready_status
  ↓ (ready)
  read_measurement → process data
  ↓ (stop needed)
  stop_periodic_measurement → wait 500 ms → IDLE
```

### Single-Shot Mode Lifecycle (SCD41)

```
IDLE → measure_single_shot → wait 5000 ms
  ↓
  read_measurement → process data → IDLE
```

### Command Restrictions During Periodic Measurement

While periodic measurement is active, **only these commands are allowed:**
- `read_measurement` (0xEC05)
- `get_data_ready_status` (0xE4B8)
- `set_ambient_pressure` (0xE000)
- `get_ambient_pressure` (0xE000)
- `stop_periodic_measurement` (0x3F86)

All other commands require stopping periodic measurement first, waiting 500 ms, then issuing the command.

### Power-Down / Wake-Up Sequence

```
IDLE → power_down → SLEEP
  ↓
  wake_up (NACKed — expected!) → wait 30 ms → IDLE
```

---

## 16. Implementation Notes

### CRC Validation

- **Every** 16-bit data word received must be CRC-8 validated.
- If CRC fails, the entire transaction should be considered invalid.
- CRC is computed over the 2 data bytes only (not the command bytes or address).

### NACK Handling

- During command execution, the sensor NACKs I²C read attempts — this is normal.
- `wake_up` command always generates a NACK — do not treat as error.
- In transport layer, distinguish between "sensor busy NACK" and "no device NACK."

### Data Ready Polling

- Poll `get_data_ready_status` before each `read_measurement`.
- Interpreting data_ready: check lower 11 bits: `(word & 0x07FF) != 0`.
- If not ready, wait and try again. Do not read measurement — stale data will be returned.
- If buffer is empty and `read_measurement` is called, the sensor **NACKs** the read — use `get_data_ready_status` to avoid this.

### Ambient Pressure Updates

- `set_ambient_pressure` can be called **during** periodic measurement.
- Value = pressure_Pa / 100. E.g., 101325 Pa → 1013; 98700 Pa → 987.
- Valid range: 700 – 1200 (hPa).
- Default: 1013 (101,300 Pa).
- Overrides altitude-based compensation when set.

### Multiple Measurements for Reliability

- After power-up or mode change, the first 1–3 CO₂ readings may be unreliable.
- For single-shot mode: perform ≥ 3 measurements before trusting the CO₂ value.
- Temperature and humidity converge faster (1 measurement usually sufficient).

### EEPROM Write Minimization

- EEPROM has ≥ 2000 write cycles — avoid calling `persist_settings` in loops.
- Configure settings once, persist once. Runtime changes (like ambient pressure) do not need persistence.

---

## 17. Pin Configuration

### SCD41 Module Pins (LGA, Top View)

| Pin | Name | Function |
|---|---|---|
| 1 | VDD | Supply voltage (2.4 – 5.5 V) |
| 2 | VDDH | IR source supply — **must be connected to VDD on PCB** |
| 3 | GND | Ground |
| 4 | SDA | I²C data |
| 5 | SCL | I²C clock |
| 6–10 | DNC | Do not connect — pads must be soldered to floating pad |

### Interface Selection (SEL Pin)

The SCD4x does not have a SEL pin — it supports I²C only.

**VDD and VDDH:** Both must be connected to the same supply voltage, close to the sensor. Combined peak current (VDD + VDDH) is per the electrical specs.

---

## 18. Mechanical & Environmental

| Parameter | Value |
|---|---|
| Package dimensions | 10.1 × 10.1 × 6.5 mm³ (LGA) |
| Weight | approx. 0.6 g |
| Mounting | SMD (reflow soldering) |
| MSL level | 3 (IPC/JEDEC J-STD-033B1) |
| Manufacturing floor time | 168 hours at ≤ 30 °C, 60 %RH |
| Reflow peak temp | ≤ 245 °C, < 30 s |
| Reflow ramp-up | < 3 °C/s |
| Liquidus temp (TL) | > 220 °C, < 60 s |
| Ramp-down rate | < 4 °C/s above TL |
| Vapor phase reflow | **Not compatible** |
| Operating temp | –10 °C to +60 °C |
| Storage temp | –20 °C to +70 °C |
| Short-term storage temp | –40 °C to +70 °C |
| Operating humidity | 0 – 95 %RH (non-condensing) |

### Sensor Placement Guidelines

- CO₂ sensor opening must not be obstructed.
- Avoid direct air flow over the sensor (affects measurement).
- Allow thermal equilibration after power-on.
- Keep away from heat-generating components to minimize temperature offset.

---

## 19. Application Design Considerations

### Supply Design

- The photoacoustic measurement creates a **175–205 mA current pulse** at 3.3 V.
- Power supply must be sized for peak current without drooping below 2.4 V.
- Add **≥ 10 µF bulk capacitor** on the supply rail near the sensor.

### I²C Bus Considerations

- Standard/fast mode only (≤ 400 kHz).
- Long execution times (up to 10 s for self-test) — do not block the bus.
- Implement proper timeout handling for each command.
- Consider a shared I²C bus topology if multiple sensors present.

### First Reading Reliability

- After startup, the sensor requires thermal stabilization.
- First few CO₂ readings may deviate — establish a "warm-up" period in software.
- ASC requires continuous operation or repeated single-shots to build calibration baseline.
