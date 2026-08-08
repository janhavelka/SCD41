# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

The manifest is staged at `1.3.0` for compatibility validation. No release tag
is created while physical HIL remains an open release gate.

### Planned 1.3.0

#### Added

- Backward-compatible, zero-I2C `isInitialized()` / `isOnline()` lifecycle
  views and direct transfer-health accessors for the latest timestamps/error,
  consecutive failures, and current-health-session success/failure counters.
- Allocation-free descriptive name helpers and `toString()` overloads for the
  sensor variant, operating mode/evidence, operation kind/state/outcome/effect,
  and operation phase enums, plus the append-only `Err::CRC_ERROR` compatibility
  spelling.
- Exhaustive enum-name/unknown-value and public health-accessor regressions,
  with a CI-enforced repository naming and hygiene contract.

#### Changed

- Arduino and native ESP-IDF CLIs now render public core enum helpers directly,
  including descriptive final-phase names, instead of maintaining parallel
  mappings.
- Naming, health-channel, sole-owner, contribution, security, and package
  descriptions now state their exact contracts without implying physical
  hardware validation.
- Renamed the private operation failure finalizer to reflect transport and
  protocol failures and removed an unused private timestamp parameter.

#### Fixed

- Corrected all CI checkout pins to the authoritative, resolvable v7.0.1 commit
  and guard the exact pinned occurrence count.

#### Removed

- Proven-unused example-only bus/driver compatibility wrappers, parser, board,
  color, and logging helpers that duplicated active owners or had no caller.

### Planned 1.2.0

#### Added

- Matching Arduino and native ESP-IDF `probe`, `attach` / `recover`, bounded
  cooperative `stress`, mode-safe `stress_mix`, and aggregate `selfcheck`
  commands. Workflows use only existing typed operations, fixed memory, one
  operation at a time, and colored pass/warn/fail summaries.
- Datasheet v1.7 feature matrix covering every command through core, both CLIs,
  tests, documentation, and remaining physical evidence.

#### Changed

- Temperature-offset requests now expose the command's complete encoded
  0..175 C domain while both CLIs warn when a value exceeds the datasheet's
  recommended 0..20 C range. ASC target and FRC reference accept their complete
  documented uint16 word domains instead of reusing the CO2 output range.
- Raw diagnostic reads now require the same explicit confirmation as raw
  writes, because an unknown command can have side effects even when a response
  is expected.
- CLI contracts now derive commands from executable handlers, enforce workflow
  dispatch and confirmation, compare Arduino/native-IDF semantics, and check the
  shared fixed-memory workflow against native-IDF framework boundaries.
- External-owner documentation now states console/scan scheduling,
  serialization, non-reentry, result-copy, and owner-task recovery boundaries.

#### Fixed

- CRC-valid but semantically invalid altitude, pressure, ASC-enable, and ASC
  period responses no longer enter verified caches. The driver reports a
  protocol/operation failure, preserves the last usable value, clears the
  affected verified bit, and retains correct partial-read evidence.
- Typed stop-periodic requests while already idle now fail with zero I2C;
  attach retains its intentional expected-NACK stop phase for unknown-mode
  reconciliation.

### Planned 1.1.1

#### Fixed

- Fixed-point temperature and humidity conversion now use the datasheet's exact
  `65535` denominator and round to the nearest published milli-unit, including
  exact full-scale endpoints.
- Arduino and ESP-IDF bus scans now respect an unexpired driver safety window,
  CLI integer parsing rejects overflow consistently, and the parity gate checks
  operation handlers and accepted boolean tokens rather than help text alone.

### Planned 1.1.0

#### Added

- Append-only `READ_SENSOR_VARIANT` operation and `Identity::variantWord` for
  the dedicated CRC-protected `get_sensor_variant` (`0x202F`) response.
- Allocation-free `errorName` / `driverStateName` helpers with `toString`
  aliases and exhaustive enum/unknown-value coverage.
- Arduino and native ESP-IDF CLI parity for variant reads and bounded raw
  command, word-write, and CRC-checked word-read diagnostics, with complete
  timing, state, health, and configuration-result evidence.

#### Changed

- Synchronized the bundled vendor reference and protocol documentation to the
  Sensirion SCD4x datasheet v1.7 (April 2025), including MSL1 handling, current
  FRC preparation, and the removal of the old first-single-shot discard rule.
- `READ_IDENTITY`, `ATTACH`, wake, and reset reconciliation now verify both the
  serial number and the dedicated sensor-variant response atomically.
- Exact-pinned native host validation to PlatformIO `native@1.2.1`.
- Pinned CI runners and third-party actions to audited Ubuntu/action revisions
  while retaining the native ESP-IDF v6.0.1 build contract.

#### Fixed

- Strict variant checking no longer misinterprets arbitrary serial-number bits
  as product identity. Vendor serial example `0xF8969F073BBE` is accepted, and
  SCD40, SCD43, unknown, or CRC-invalid variant responses remain observable and
  fail safely under strict policy.
- Ambient-pressure encoding now follows the datasheet's exact integer
  `pressurePa / 100` conversion instead of rounding to the nearest hPa.

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

- Exact-pinned the Arduino ESP32-S2/S3 example builds to pioarduino
  `platform-espressif32` `55.03.311` (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`)
  while retaining the `54.03.20` TunnelMonitor target-package compatibility
  build. The ESP32-S3 example now explicitly selects its 4 MB flash / QSPI
  PSRAM memory type instead of relying on platform defaults. CI pins the
  platform's minimum supported PlatformIO Core version, `6.1.19`.
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
  generated HTML under `.doxygen/`, and takes its project version from the
  manifest synchronization check.
- CI now builds the warning-clean generated API reference in the guard job.
- README and integration documentation now state the release-candidate status,
  complete `Config` contract, result-value mapping, and remaining evidence gate.

#### Fixed

- Arduino CI explicitly builds the library and examples with GNU++17 on the
  pinned pioarduino `55.03.311` platform.
- GitHub-hosted CI actions use their Node 24-compatible stable majors.
- Doxygen output now uses a self-creatable ignored directory, has one unambiguous
  main page, and does not require optional Graphviz tooling.

#### Removed

- Blocking/direct device command APIs and the separate legacy measurement
  scheduler.
- Driver-owned retry, recovery, bus reset, rail control, time callbacks, and
  cooperative-yield hooks.
- Precise address/data/read NACK requirements from the active transport API.
- Legacy raw byte APIs that could bypass CRC or managed state.
- Completed implementation prompt sequences, the closed suitability audit, and
  a pre-HIL report containing only `NOT RUN` entries. Git history retains them.

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
