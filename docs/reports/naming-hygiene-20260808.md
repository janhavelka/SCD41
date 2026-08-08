# SCD41 Naming And Repository-Hygiene Audit

Audit date: 2026-08-08
Target version: 1.3.2

This audit compares SCD41 read-only with clean, mature local I2C libraries:
PCA9555 3.0.2, INA228 3.0.3, INA3221 3.1.0, MB85RC 4.1.0,
RV3032-C7 3.0.1, and LDC1614 3.1.0. The completed OPT4001 1.2.2 and
TCA9548A 1.1.4 naming audits were also used as consistency checks.
Device-specific owner-driven behavior takes precedence over cosmetic
uniformity.

## Compatibility Rubric

| Concern | Mature-peer pattern | SCD41 decision |
| --- | --- | --- |
| Errors | Stable `Err`, structured `Status`, allocation-free names | Preserve every value and spelling; append `CRC_ERROR` as an alias of `CRC_MISMATCH`. |
| Driver state | Four passive states and direct health views | Preserve `DriverState`, `state()`, `driverState()`, and snapshots; add compatible zero-I2C direct transfer-health accessors. |
| Lifecycle | `begin`/`end`, often blocking `probe`/`recover` | Preserve owner-driven `begin`/`start`/`poll`/`takeResult`/`cancel`/`end`. Do not add a second blocking lifecycle path. Protocol-qualified discovery and recovery remain typed `READ_IDENTITY`/`ATTACH` jobs. |
| Initialization/online aliases | `isInitialized()` and `isOnline()` are common | `isInitialized()` is exactly `isBound()`. `isOnline()` means passive transfer state `READY` or `DEGRADED`, not verified attachment. |
| Enum rendering | Core-owned static names avoid CLI drift | Add descriptive names plus overload-safe `toString()` for enums rendered by both CLIs; retain `errorName()` and `driverStateName()`. |
| Health channels | Mature drivers expose direct transport counters | Map direct accessors only to transfer telemetry. Keep richer protocol/CRC and logical-operation fields in `HealthSnapshot`; all channels reset on successful `begin()`, not object construction alone. |
| Ownership | One non-owning driver instance, externally serialized I2C | Retain fixed-memory typed jobs. No task, lock, queue, heap, platform type, or TunnelMonitor coupling is added. |

No existing public type, enum value, field, method, CLI command, or operation
kind was renamed, removed, or reordered. `SensorVariant::SCD42` remains despite
having no datasheet v1.7 encoding because removing it would break source
compatibility.

## Internal Naming And Cleanup

- Renamed private `_finishTransferFailure()` to
  `_finishOperationFailure()`: the owner finalizes both mapped transport faults
  and CRC/protocol failures through that path.
- Renamed the private terminal publisher `_finish()` to `_finishOperation()` so
  it forms an explicit family with `_beginOperation()`,
  `_finishOperationFailure()`, and `_recordOperationOutcome()`.
- Retained `_attemptTransfer()` and `_recordTransfer()` instead of importing
  synchronous-driver raw/tracked wrapper names. SCD41 has one owner-driven,
  normalized transfer contract and no untracked probe path; every physical
  attempt is recorded.
- Kept transfer-adapter enums typed and numeric rather than adding unused name
  helpers. Core name helpers cover the driver enums rendered by both CLIs.
- Removed the unused `nowMs` argument from private `_applyReadValue()`.
- Removed example-only `BusDiag.h` and `DriverCompat.h`. The active Arduino CLI
  now uses `I2cScanner.h`, the public `SCD41` class, and core enum helpers
  directly.
- Removed zero-reference `BoardConfig::LED`, `CommandHandler::parseFloat`,
  unused color helpers, and unused logging levels/macros. No library-core path
  changed ownership, allocation, retry, timing, or I2C behavior.
- Native ESP-IDF and Arduino output now share core enum names, including a
  descriptive final operation phase, instead of duplicate local switch tables.

Repository-wide symbol searches established that each removed helper had no
caller outside its own declaration/definition. The static hygiene contract
rejects restoration of the obsolete wrappers and duplicate enum maps.

## Documentation And Evidence Boundary

- Current Windows PlatformIO instructions use `scripts\pio.cmd`; Linux CI keeps
  its pinned `python -m platformio` commands.
- Security support and synchronized metadata now name the staged 1.3.2 line.
- The package description states the actual owner-driven driver role and does
  not imply production or hardware validation.
- Stable HIL procedures, protocol references, feature coverage, and owner
  integration guides remain. No generated Doxygen HTML, completed prompt,
  empty transcript, or NOT-RUN-only artifact is retained.

These changes are source-level and host-testable. They do not claim physical
SCD41, electrical-fault, calibration, optical, or long-duration validation.

## Documentation And HIL Hygiene Follow-Up

A fresh repository-wide call-graph and artifact pass found no unused private
core method or active example helper. It did find two documentation/tooling
gaps: ESP-IDF component metadata retained an unqualified production claim, and
generated HIL summaries omitted repository/toolchain provenance required by
the validation guide. Version 1.3.1 corrects both, ignores transient
`hil-results/`, expands Doxygen coverage, and makes the hygiene guard reject
broken local links, generated prompts/transcripts/docs, stale metadata, and CI
toolchain drift. No protocol, public API, transport, timing, cache, or health
behavior changes in this follow-up.

Version 1.3.2 re-audits the public status, state, and health vocabulary against
the latest OPT4001 and TCA9548A passes; strengthens the executable hygiene
contract for lifecycle and health accessors; and applies the private terminal
publisher rename above. It does not change public API, protocol behavior, or
health semantics.
