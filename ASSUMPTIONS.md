# Assumptions

This repository now contains the full SCD41 driver, tests, and example tree. The assumptions below document places where the device documentation still leaves host policy decisions to the application.

## Explicit Assumptions

- This package targets the SCD41 variant specifically. SCD40, SCD42, and SCD43 are not treated as supported targets for the public library contract.
- Presence detection in `begin()` should use `get_serial_number` and validate variant bits `[15:12] == 0x1`.
- The intended public package naming is `SCD41::` with the main header at `include/SCD41/SCD41.h`.
- Long sensor operations such as self-test, single-shot measurement, persistence, and factory reset must be represented as bounded, `tick()`-driven flows rather than blocking for 50 ms to 10 s inside a public API.
- `wake_up` command NACK is expected behavior and should be handled as a special success path followed by a bounded 30 ms settle window.
- Ambient pressure payloads are interpreted as `Pa / 100`, with the documented valid range `700..1200`.
- EEPROM writes remain explicit maintenance operations only. Runtime compensation changes do not imply persistence.
- The first single-shot CO2 readings after power-up or mode transitions may be unreliable and should be treated as warm-up samples in higher-level application logic.

## Scope Boundary

- `begin()` assumes the sensor is already in an idle, command-accepting state. If the MCU restarts while the sensor remains in periodic mode, the application may need to stop periodic measurement or power-cycle the sensor before calling `begin()` again.
- The driver enforces datasheet command timing and treats long operations as scheduled work. Higher-level policy such as warm-up discard count, calibration cadence, and persistence frequency remains the application's responsibility.
