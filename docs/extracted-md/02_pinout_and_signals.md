# Pinout And Signals

## Pad Functions

| Name | Meaning | Driver/wiring note | Source |
|---|---|---|---|
| `VDD` | Supply voltage | Main sensor supply. | Datasheet, p. 5 |
| `VDDH` | IR-source supply | Must be connected to `VDD` on the customer PCB and kept at the same voltage. | Datasheet, p. 5 |
| `GND` | Ground | Ground contact. | Datasheet, p. 5 |
| `SDA` | I2C serial data | Bidirectional I2C line; use pull-up resistor. | Datasheet, p. 5 |
| `SCL` | I2C serial clock | I2C clock line; use pull-up resistor. | Datasheet, p. 5 |
| `DNC` | Do not connect | Pads must be soldered to floating pads on the customer PCB. | Datasheet, p. 5 |

## Hardware Notes

- Connect `VDDH` to `VDD` close to the SCD41 package; `VDDH` is the IR-source supply and the datasheet requires it to be tied to `VDD` on the customer PCB. Source: datasheet, p. 5.
- The datasheet recommends a low-noise supply, such as an LDO, that can handle peak current and ripple requirements. Source: datasheet, p. 5.
- The unloaded supply voltage ripple limit is 30 mV peak-to-peak. Source: datasheet, pp. 4-5.
- `SCL` and `SDA` require external pull-ups; the datasheet example is 10 kOhm, adjusted for bus capacitance and frequency. Source: datasheet, p. 5.
- The notched corner of the protection membrane marks pin 1, and the membrane must not be removed or tampered with. Source: datasheet, pp. 5, 22.
