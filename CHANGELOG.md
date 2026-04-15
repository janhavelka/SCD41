# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- `begin()` now honors the configured power-up settle delay before the first command and no longer folds startup probe traffic into runtime health counters.
- Expanded the bring-up CLI to match the stronger family examples, including live settings readback, compensation/ASC controls, maintenance commands, version/identity views, and watch mode.
- Tightened the example Wire transport so short reads map to generic `I2C_ERROR` unless a transport can explicitly distinguish read-header NACK behavior.

### Added
- `readSettings()` and extended `SettingsSnapshot` live configuration fields for temperature offset, altitude, ambient pressure, and ASC state.
- Native coverage for power-up delay handling, live settings readback, periodic ambient-pressure behavior, self-test completion, probe-after-failed-begin diagnostics, and example transport mapping.

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
