# SCD41 TunnelMonitor Hardening Audit

Date: 2026-06-16

## Scope

This report covers the SCD41 library audit for TunnelMonitor-style optional
device ownership by an external I2C task. It does not claim hardware, HIL, or
ESP-IDF build validation.

## Long-Command Behavior

- `poll(nowMs, maxInstructions)` is the steady-path bounded executor. One
  command write or one read-only response frame consumes one instruction.
  Delay gates consume zero instructions and return `IN_PROGRESS`.
- `poll()` uses its `nowMs` argument for millisecond scheduling gates and the
  configured `nowUs` callback for command-spacing gates.
- `tick(nowMs)` remains a legacy compatibility lifecycle API. It is not the
  TunnelMonitor I2C-owner surface and is not instruction-budgeted.
- Existing `start*` methods still issue their initial command synchronously,
  then schedule completion work. The chunked poll path owns delayed response
  reads, data-ready checks, measurement reads, and long-command result reads.
- Single-shot measurement, RHT-only measurement, self-test, forced
  recalibration, stop-periodic, power-down, wake-up, persist, reinit, factory
  reset, and external power-cycle recovery expose their completion phase as
  scheduled work.
- Self-test and forced recalibration expose result failures through the `poll()`
  or `tick()` return status plus `lastAsyncStatus()` / `lastAsyncOperation()`.
- Wake-up accepts only explicit write address/data NACK statuses as expected
  wake behavior. Generic `I2C_ERROR`, timeouts, bus errors, and read-header
  NACK remain failures.

## API Classification

- Steady-path: `poll()`, `requestMeasurement()`,
  `startReadSettings()`, `getSettings()`, cached sample accessors, and async
  result accessors.
- Diagnostic-only: `probe()`, `readSettings()`, raw unsafe byte reads, and
  direct live configuration reads when used outside controlled diagnostics.
- Convenience-only: `tick()`, `readMeasurement()`, synchronous low-level command
  helpers, and blocking live getters. These may perform command/read work
  immediately and are not the preferred TunnelMonitor cadence surface.
- Blocking startup: `begin()` remains a compatibility startup API. It performs
  the configured power-up settle wait and the serial-number probe before the
  driver enters `READY`.

## Fixed In This Audit

- Package metadata now exposes every public `include/SCD41` header, including
  generated `Version.h`.
- A clean consumer compile checker validates public headers from the source
  tree or a packed library tarball.
- `begin()` and `probe()` preserve detailed transport/protocol failures instead
  of collapsing I2C timeout, bus, and NACK statuses to `DEVICE_NOT_FOUND`.
- Latched driver offline now returns `Err::OFFLINE` with
  `Driver is offline; call recover()` and does not touch the bus.
- Wake expected-NACK coverage now includes rejection of read-header NACK.
- Poll command-delay tests cover visible `IN_PROGRESS` gates, no I2C consumed
  while the gate is pending, and `uint32_t` microsecond wraparound.

## Integration Risks

- Do not call `readSettings()` on a telemetry cadence. Use `startReadSettings()`
  with `poll()` for bounded live refresh, or `getSettings()` for cached/local
  state.
- Do not treat the SCD41 driver's latched `OFFLINE` state as TunnelMonitor
  aggregate health. It is a local transport latch and requires
  application-controlled `recover()`.
- Transport adapters must map timeout, bus error, address NACK, data NACK, and
  read-header NACK distinctly when the platform can prove them. Generic
  `I2C_ERROR` is intentionally not accepted as expected wake behavior.
- Synchronous compatibility helpers still exist for examples and diagnostics.
  TunnelMonitor should prefer the poll-driven surface for steady-state work.
- If TunnelMonitor needs absolute ownership of the initial command write too,
  the remaining integration step is to convert `start*` calls into poll-owned
  jobs. This pass focuses on delayed completion phases and instruction-budgeted
  reads, matching the current compatibility API boundary.
