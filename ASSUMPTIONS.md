# Assumptions

This repository now contains the full SCD41 driver, tests, and example tree. The assumptions below document places where the device documentation still leaves host policy decisions to the application.

## Explicit Assumptions

- This package targets the SCD41 variant specifically. SCD40, SCD42, and SCD43 are not treated as supported targets for the public library contract.
- Presence detection in `begin()` should use `get_serial_number` and validate variant bits `[15:12] == 0x1`.
- The intended public package naming is `SCD41::` with the main header at `include/SCD41/SCD41.h`.
- Long sensor operations such as self-test, single-shot measurement, persistence, and factory reset must be represented as bounded, `tick()`-driven flows rather than blocking for 50 ms to 10 s inside a public API.
- `begin()` is an explicitly blocking startup compatibility API: it may wait the configured power-up settle window and perform a serial-number probe before returning.
- `readSettings()` is an explicitly blocking diagnostic live refresh. It is not intended for tight steady-state control loops.
- `wake_up` command NACK is expected behavior only when the transport reports a precise address/data NACK; generic I2C errors, timeouts, and bus faults remain tracked failures.
- Cached samples are retained as historical diagnostic data across reset-like operations, but `reinit`, factory reset, and power-cycle recovery start a new sensor epoch and mark the retained cache stale.
- Sleep/wake does not start a new sensor epoch; sample age and warm-up policy after wake remain application decisions.
- `probe()` is health-clean and restores driver command-spacing state, but it still performs a real I2C transaction and drains the required post-probe inter-command guard before returning success.
- Ambient pressure payloads are interpreted as `Pa / 100`, with the documented valid range `700..1200`.
- EEPROM writes remain explicit maintenance operations only. Runtime compensation changes do not imply persistence.
- The first single-shot CO2 readings after power-up or mode transitions may be unreliable and should be treated as warm-up samples in higher-level application logic.

## Scope Boundary

- `begin()` assumes the sensor is already in an idle, command-accepting state. If the MCU restarts while the sensor remains in periodic mode, the application may need to stop periodic measurement or power-cycle the sensor before calling `begin()` again.
- The driver enforces datasheet command timing and treats long operations as scheduled work. Higher-level policy such as warm-up discard count, calibration cadence, and persistence frequency remains the application's responsibility.
