# Security Policy

## Supported Versions

| Version | Supported |
| ------- | --------- |
| 0.1.x   | yes       |

## Reporting a Vulnerability

If you discover a security issue in this library:

1. Do not open a public GitHub issue for it.
2. Email the maintainer at `info@thymos.cz`.
3. Include:
   - a concise description of the issue
   - exact steps to reproduce
   - affected hardware and bus topology
   - impact assessment
   - any suggested mitigation, if available

We will acknowledge receipt within 48 hours and aim to provide a mitigation timeline quickly for valid reports.

## Scope

This is an embedded sensor library. Relevant security and safety considerations include:

- no network code in the library itself
- no dynamic allocation in steady state
- explicit, opt-in EEPROM writes only (`persist_settings` and reset/calibration related flows)
- bounded timing and no unbounded polling loops in library code
- shared-bus integrity for I2C transports

## Best Practices For Users

- Validate all external inputs before they reach `Config` or command wrappers.
- Budget the power rail for the SCD41 current pulse and avoid brownout-induced undefined behavior.
- Use watchdogs and system-level recovery around the driver in production firmware.
- Treat calibration and persistence commands as privileged maintenance actions, not routine telemetry flows.
