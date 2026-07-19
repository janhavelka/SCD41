# Assumptions

These assumptions define device facts or application-policy boundaries that the
core driver cannot determine by itself.

## Device and protocol assumptions

- The production target is SCD41. SCD40, SCD42, and SCD43 identities can be
  observed, but are not claimed as supported devices.
- The I2C address is fixed at `0x62`.
- Serial-number word 0 bits `[15:12] == 0x1` identify SCD41.
- Ambient pressure is supplied in Pa at the public API and encoded as `Pa / 100`
  for the device. The documented useful input range is 70000-120000 Pa.
- A generic write NACK for a request marked `EXPECTED_WRITE_NACK` is sufficient
  evidence for the documented wake-up behavior. Timeout, bus error, short
  transfer, and generic failure are not accepted as expected wake behavior.
- EEPROM configuration storage is rated for at least 2000 write cycles.
  Persistence is always explicit and is never inferred from a runtime setting
  change.
- FRC and ASC field-calibration history is automatically stored in a separate
  EEPROM dimensioned for the specified sensor lifetime. Forced recalibration is
  therefore classified as nonvolatile maintenance even though it does not use
  the 2000-cycle user-settings EEPROM budget.

## Host integration assumptions

- `TransferResult::completedMs`, operation `nowMs`, and poll `nowMs` use the same
  wrapping 32-bit monotonic millisecond clock.
- An individual operation deadline is less than half the 32-bit clock range
  from its start, so wrap-safe signed time comparisons remain unambiguous.
- The transfer adapter enforces `TransferRequest::timeoutMs` and returns after
  exactly one attempt. It does not perform hidden retries.
- The callback's user context and buffers remain valid for the synchronous
  duration of the callback. The driver does not retain transfer-buffer pointers.
- Calls for one instance are serialized by the application. The API is not
  thread-safe or ISR-safe, and transport callbacks do not recursively enter the
  same instance.
- Cancellation prevents future host work only. It cannot undo a command or
  payload already accepted by the sensor.
- Application retry is a new operation with a new request identity. The
  application first interprets the previous effect state and performs
  reconciliation when required.
- Bus locking, arbitration with other devices, aggregate health, reset policy,
  sensor rail control, scheduling, and watchdog policy remain application-owned.

## State and product-policy assumptions

- `ATTACH` is the explicit hardware reconciliation operation after zero-I2C
  `begin()`. Applications do not publish device data until attach succeeds.
- Reset-like operations advance the sensor epoch. A retained sample from an
  older epoch is historical diagnostic data, not current sensor output.
- Sleep/wake does not by itself define a new product measurement epoch. The
  result and reconciliation fields remain the authoritative driver evidence.
- The first single-shot CO2 results after power-up or a mode transition may be
  unsuitable for publication. Warm-up discard count is product policy.
- Temperature offset, altitude, pressure source, ASC policy, calibration
  cadence, reference concentration, persistence frequency, and sample-quality
  thresholds are product decisions.
- Forced recalibration and factory reset require operator/application authority;
  constructing a confirmed maintenance request is treated as that authority.
- Physical rail interruption can invalidate any in-flight hardware effect.
  The application cancels host work, restores the bus/rail, and starts a new
  attach/reconciliation operation.
