# External I2C Owner Integration

Use this guide when an application-level I2C task owns all sensor bus progress.
The core driver remains callback-driven; the application decides when bounded
work may advance.

## Preferred Steady Path

- Use `poll(nowMs, maxInstructions)` for bounded progress.
- Use `pollBusy()` and `lastPollStatus()` for scheduler decisions and
  diagnostics.
- Use `requestMeasurement()` to schedule a managed measurement read.
- Use `startReadSettings()` plus `getSettings()` for bounded live settings
  refresh.
- Use cached sample accessors and async result accessors for application state.

One command write or one read-only response frame consumes one poll
instruction. Delay gates consume zero instructions and return `IN_PROGRESS`.

## Compatibility And Diagnostic APIs

These APIs remain useful, but they are not the preferred external-owner cadence
surface:

| Classification | APIs |
| --- | --- |
| Blocking startup | `begin()` |
| Convenience-only | `tick()`, `readMeasurement()`, synchronous low-level command helpers |
| Diagnostic-only | `probe()`, `readSettings()`, unsafe raw byte reads, direct live configuration reads outside controlled diagnostics |
| Steady-path | `poll()`, `requestMeasurement()`, `startReadSettings()`, `getSettings()`, cached sample and async result accessors |

`begin()` performs startup settle and serial-number probing before the driver
enters `READY`. Keep it out of time-critical I2C-owner loops.

`readSettings()` can chain multiple live reads while idle. Do not call it on a
telemetry cadence. Use `startReadSettings()` with `poll()` for bounded refresh,
or read cached settings with `getSettings()`.

## Long Operations

Long SCD41 operations are exposed as pending work and completion results:

- full single-shot measurement
- RHT-only single-shot measurement
- self-test
- forced recalibration
- stop-periodic settle
- power-down and wake-up settle
- persist settings
- reinit
- factory reset
- external power-cycle recovery

Completion failures are returned by `poll()` or `tick()` and retained through
`lastAsyncStatus()` / `lastAsyncOperation()`.

Existing `start*` methods may still issue their initial command synchronously
for compatibility, then schedule completion work. If an application requires
absolute ownership of the initial command write as well, route that operation
through a poll-owned job in the application architecture.

## Error And Health Boundaries

- `Err::OFFLINE` is a local driver transport latch. It is not aggregate system
  health and should not by itself mark unrelated optional devices offline.
- Normal public I2C operations do not touch the bus while the driver is
  `OFFLINE`; use application-controlled `recover()`.
- `probe()` is diagnostic and does not update driver health counters.
- Validation failures such as `INVALID_PARAM` do not update I2C health.
- Generic `I2C_ERROR` is not accepted as expected wake behavior. Only precise
  write address/data NACK statuses may be suppressed for `wake_up`.

Transport adapters should preserve as much detail as the platform can prove:
timeout, bus error, address NACK, data NACK, and read-header NACK are different
integration signals.
