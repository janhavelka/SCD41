# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

The manifest is staged at `1.0.0` for compatibility validation. No `v1.0.0`
release or tag exists yet; physical HIL is still a release gate.

### Planned 1.0.0

#### Added

- One typed, externally scheduled operation model with request ID, generation,
  immutable deadline, progress, cancellation, and exactly-once terminal result.
- Explicit steady-state, runtime, maintenance, and diagnostic operation classes.
- `OperationLimits` metadata for maximum callbacks, retries, sensor wait,
  nonvolatile writes, and destructive operations.
- Callback-budgeted `poll(nowMs, maxCallbacks)` with zero-I2C wait phases.
- `TransferRequest` / `TransferResult` single-attempt transport contract,
  including transfer intent, generic result code, effect disposition, exact byte
  count, and callback completion timestamp.
- Observable successful, no-data, failed, cancelled, timed-out, partial, and
  indeterminate outcomes with effect and reconciliation state.
- Fixed-width sample, identity, configuration, runtime, and health snapshots.
- Typed requests for periodic and single-shot measurement, all supported
  compensation and ASC settings, power management, self-test, reinit, forced
  recalibration, persistence, factory reset, and bounded diagnostic words.
- Native Arduino and ESP-IDF owner-safe CLI examples with matching command
  contracts and explicit maintenance confirmations.
- Fault-injection, timing-boundary, cancellation, stale-result, cache-integrity,
  portability, and package-consumer validation.
- Undefined-behavior sanitizer CI coverage and packed-library consumer checks.

#### Changed

- `begin(const Config&)` is now a zero-I2C bind. Hardware discovery and
  reconciliation use an explicit `ATTACH` operation.
- All sensor traffic now occurs only from caller-authorized `poll()` calls.
- Long command waits no longer block inside public APIs; they are bounded
  operation phases driven by the caller's clock.
- I2C is injected through one unified callback instead of separate write and
  write/read callbacks.
- Health is passive diagnostic information. The application retains retry,
  reset, rail, and bus-recovery authority.
- Cache verification and sensor epochs now prevent failed or reset-like work
  from being published as verified current hardware state.
- Wake-up uses an explicit expected-NACK transfer intent, so adapters need not
  invent address/data NACK precision.
- `library.json` is the version source for both generated `Version.h` and
  `idf_component.yml`.
- Release packages include stable integration documentation and exclude dated
  audit/HIL reports.
- Public headers now document all exported enums, request/result fields,
  snapshots, helpers, units, parameters, and return contracts.
- Doxygen now fails on undocumented public API or parameter warnings, writes
  generated HTML under `.pio/`, and takes its project version from the manifest
  synchronization check.
- CI now builds the warning-clean generated API reference in the guard job.
- README and integration documentation now state the release-candidate status,
  complete `Config` contract, result-value mapping, and remaining evidence gate.

#### Removed

- Blocking/direct device command APIs and the separate legacy measurement
  scheduler.
- Driver-owned retry, recovery, bus reset, rail control, time callbacks, and
  cooperative-yield hooks.
- Precise address/data/read NACK requirements from the active transport API.
- Legacy raw byte APIs that could bypass CRC or managed state.

#### Compatibility

This is a breaking release. Existing consumers must replace legacy callback
fields and direct command calls with `Config::transfer`, typed
`OperationRequest`, `start()`, `poll()`, `cancel()`, and `takeResult()`.
Legacy precise NACK enum values remain for append-only status compatibility,
but the active unified transport maps ordinary NACKs to `Err::I2C_NACK`.

## [0.1.0] - 2026-04-14

### Added

- Initial SCD41 package metadata and repository policy files.
- First SCD41 driver core, examples, native tests, and datasheet reference.

[Unreleased]: https://github.com/janhavelka/SCD41/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/janhavelka/SCD41/releases/tag/v0.1.0
