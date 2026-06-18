# SCD41 I2C Uniformization Prompt

Repository: `SCD41`

Absolute path: `C:\Users\Honza\Documents\Projects\SCD41`

## Execution Rules

You are working inside this single repository. Implement this prompt directly;
do not repeat the cross-repository audit.

You may spawn subagents for read-only inspection of APIs, tests, I2C
transactions, docs, and diagnostics. Keep final judgment, edits, and
verification in the main agent.

Prefer simple, robust, readable code. Before adding code, inspect whether
existing code can be simplified, reused, tightened, or deleted.

Preserve dirty user changes. Do not commit unless explicitly asked.

## Common Uniformization Target

Apply this shared I2C library contract: injected non-owning transport, `Status` returns, cache-only `getSettings(SettingsSnapshot&) const`, active `probe()`/diagnostics named explicitly, `DriverState` with `state()` and `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, and `totalSuccess()`.

Keep the common `Err` vocabulary append-only where missing: `OK`, `NOT_INITIALIZED`, `INVALID_CONFIG`, `INVALID_PARAM`, `I2C_ERROR`, `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `DEVICE_NOT_FOUND`, `TIMEOUT`, `BUSY`, and `IN_PROGRESS`. Preserve SCD41-specific CRC, command, measurement, self-test, and maintenance behavior.

Uniformization is not a new base class or framework. Make only local, source-compatible additions and tests.

## Current State

- Public lifecycle and health are in `include\SCD41\SCD41.h`: `DriverState` at line 16, `SettingsSnapshot` at line 110, `probe()` at line 222, `recover()` at line 225, `driverState()` at line 232, health counters at lines 245-250, and `getSettings(SettingsSnapshot&)` at line 437.
- Active live refresh is already separate as `readSettings(SettingsSnapshot&)` at `include\SCD41\SCD41.h:445-449`.
- Self-test state is explicit: `selfTestReady()` at `include\SCD41\SCD41.h:417`, self-test fields at lines 681-684, and implementation at `src\SCD41.cpp:1158-1189`.
- HIL runner exists as `tools\scd41_hil_runner.py`; destructive steps require confirmation at `tools\scd41_hil_runner.py:160-169`.
- Native tests passed 91 tests.

## Best Sources To Adapt

- Keep SCD41 as a source pattern for separating cache-only `getSettings()` from active `readSettings()`.
- Use SHT3x HIL parser coverage as a richer model for address/health/status parsing, but avoid adding pytest-only dependency unless declared.
- Use SCD41's own destructive-step confirmation as a source pattern for other maintenance-heavy sensors.

## Implementation Tasks

1. Preserve `getSettings()` vs `readSettings()` separation. Do not hide live reads inside the cache-only status path.
2. Add host-side parser/classifier tests for `tools\scd41_hil_runner.py`. Cover serial number parsing, missing serial port/import failure behavior, destructive-step confirmation, common minimum `version`/`scan`/`probe`/`settings`/`health` coverage when the CLI supports it, and failure-token classification.
3. Ensure README/Doxygen state that factory reset, forced recalibration, ASC, and EEPROM-writing commands are maintenance actions, not normal diagnostics.
4. Confirm `probe()` remains raw/no-health and that identity/serial-number reads are explicit commands.
5. Keep recovery bounded and observable through the existing `recoverBackoffMs` policy in `include\SCD41\Config.h:101`.
6. If the existing HIL runner remains named `tools\scd41_hil_runner.py`, document that exception or add a thin `tools\run_i2c_hil.py` wrapper.

## API Changes Required

- None expected.

## Simplifications Before Adding Code

- Do not add register helper APIs. SCD41 uses CRC-protected commands, not a normal register map.

## Tests To Add Or Update

- Host HIL parser tests.
- Native bus-silence test for `getSettings()` if missing.
- Native tests for destructive command guards if implementation changes.

## Commands To Run

- `pio test -e native`
- `pio run -e esp32s3dev`
- If parser tests are added: `python tools\<new_hil_parser_test>.py`
- Live HIL only with `python tools\scd41_hil_runner.py --port <PORT>` and explicit destructive confirmation when requested.

## Constraints And Non-Goals

- Do not expose raw unvalidated diagnostic reads as the preferred API.
- Do not run destructive HIL steps by default.
- Injected transport only: no global `Wire`, new bus manager, pin ownership, or shared bus reset from the device driver.
- Preserve distinct timeout, address NACK/device-not-found, data NACK, read NACK, bus, CRC, command, measurement, and self-test statuses. Do not collapse them into generic `I2C_ERROR` or use `DEVICE_NOT_FOUND` for timeout/data/bus failures.

## Risks And Open Questions

- Open: whether HIL should classify environmental plausibility ranges or only serial/driver command success.
