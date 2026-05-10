# Initialization, Reset, And Operational Notes

## Startup

- After reaching the power-up threshold, the sensor takes up to the maximum power-up time to enter idle state, then it can receive commands. Source: datasheet, pp. 6-7.
- Typical periodic startup sequence: power up, send `start_periodic_measurement`, periodically `read_measurement`, and send `stop_periodic_measurement` to return to idle. Source: datasheet, p. 9.

## Configuration Flow

1. If periodic measurement is running, send `stop_periodic_measurement` and wait 500 ms. Source: datasheet, p. 10.
2. Send get/set commands for configuration such as temperature offset, sensor altitude, ASC enable, or ASC periods. Source: datasheet, pp. 10-14, 19-20.
3. Send `persist_settings` only when persistence is required and values actually changed; EEPROM endurance is at least 2000 write cycles. Source: datasheet, p. 15.
4. Restart the selected measurement mode. Source: datasheet, p. 10.

## Reset And Recovery

| Action | Command | Notes | Source |
|---|---:|---|---|
| Reinitialize from EEPROM | `0x3646` | Send only after stopping periodic measurement; use power cycle if it does not achieve the desired reinitialization. | Datasheet, p. 17 |
| Factory reset | `0x3632` | Resets EEPROM configuration and erases FRC/ASC history. | Datasheet, p. 17 |
| Wake from power-down | `0x36f6` | Command is not acknowledged; verify idle state by reading the serial number. | Datasheet, p. 19 |

## Application Accuracy Notes

- Rough handling, shipping, and assembly can temporarily affect CO2 accuracy; FRC or ASC can restore accuracy at least 5 days after sensor assembly. Source: datasheet, p. 3.
- Correct temperature offset matters for RH/T accuracy in the final device because design-in and self-heating affect the built-in RH/T sensor. Source: datasheet, pp. 3, 10.
- For pressure compensation, setting ambient pressure is recommended when ambient pressure changes significantly; it overrides altitude-based pressure compensation. Source: datasheet, p. 12.
- Full CO2 accuracy after reflow soldering is restored after at most five days, whether or not the sensor is operated. Source: datasheet, p. 24.
