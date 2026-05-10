# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- `begin()` now honors the configured power-up settle delay before the first command and no longer folds startup probe traffic into runtime health counters.
- Explicit recovery/reset bypass internals now use the shared `ScopedOfflineI2cAllowance` / `_reassertOfflineLatch()` procedure so failed recovery attempts that begin from `OFFLINE` keep the latch asserted.
- Expanded the bring-up CLI to match the stronger family examples, including live settings readback, compensation/ASC controls, maintenance commands, version/identity views, and watch mode.
- Refined the bring-up CLI again to match the mature family behavior more closely: versioned help header, startup/log flow parity, `state` compact-health alias, toggle-style verbose output, more detailed health/error reporting, less noisy unknown-command handling, and a smarter `read` path that prints a ready sample before scheduling another fetch.
- Extended the bring-up CLI with chip-oriented `status` and cached `sample` / `last` views, plus explicit pending-work and completion summaries for deferred commands such as self-test, FRC, wake-up, stop-periodic, reinit, and factory reset.
- Tightened the example Wire transport so short reads map to generic `I2C_ERROR` unless a transport can explicitly distinguish read-header NACK behavior.
- Tightened the raw command helpers so they reject managed mode/state commands, preserve periodic-mode command restrictions, and stay aligned with the driver's internal state model.
- `readSettings()` now refreshes live ambient-pressure compensation even while periodic measurement is active, while still leaving other idle-only configuration fields untouched.
- Fixed the bring-up CLI so forced-recalibration failures are always reported and raw diagnostic reads no longer mask read-header NACKs by default.
- Tightened README and Doxygen coverage so the managed measurement model, raw-command constraints, snapshot behavior, and public API contracts are documented in engineering terms instead of implicit in the code.
- Raw reads and low-level transport wrappers now validate local buffer/length contracts before dispatching to I2C, and synchronous wait guards now return `TIMEOUT` if the injected timebase stalls.
- Health behavior is now standardized on latched `OFFLINE`: normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch I2C until `recover()` succeeds.

### Added
- `readSettings()` and extended `SettingsSnapshot` live configuration fields for temperature offset, altitude, ambient pressure, and ASC state.
- Public raw command helpers (`writeCommand`, `writeCommandWithData`, `readCommand`, `readWordCommand`, `readWordsCommand`) plus named single-shot and `readMeasurement()` command helpers.
- Raw self-test/FRC result accessors and ambient-pressure encode/decode helpers.
- Small public convenience helpers for application code: `measurementPending()`, `measurementReadyMs()`, `getLastMeasurement()`, and `getIdentity()`.
- Native coverage for power-up delay handling, direct `read_measurement` reads, raw-command helper restrictions/error paths, live settings readback, periodic ambient-pressure behavior, self-test completion, probe-after-failed-begin diagnostics, and example transport mapping.
- Native coverage proving latched `OFFLINE` blocks normal I2C operations without touching the bus while explicit recovery/reset commands remain available.
- README documentation for the single-threaded, non-ISR driver contract and explicit recovery model.

### Fixed
- `setTemperatureOffsetC(float)` now rejects NaN/infinite input before converting to the fixed-point command word.

### Removed
- Deleted the unused example-side `Scd41Protocol.h` duplicate command layer.

## [0.1.0] - 2026-04-14

### Added
- Initial SCD41 package metadata and repository policy files.
- Production-style SCD41 driver core with injected I2C transport, health tracking, periodic and single-shot measurement flows, calibration/configuration commands, and native tests.
- Family-style bring-up CLI example and shared example helpers.
- Datasheet-driven repository guidance in `AGENTS.md`.
- `ASSUMPTIONS.md` capturing SCD41-specific behavioral assumptions and remaining application-policy decisions.

### Changed
- Replaced the copied library shell with SCD41-specific README, changelog, and tooling metadata.
- Repointed version generation and timing-guard tooling to the SCD41 package layout.
- Updated Doxygen, contributing, security, and ignore rules to the SCD41 repository identity.

[Unreleased]: https://github.com/janhavelka/SCD41/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/janhavelka/SCD41/releases/tag/v0.1.0
