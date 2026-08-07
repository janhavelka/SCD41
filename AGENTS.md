# AGENTS.md - SCD41 Production Embedded Guidelines

## PlatformIO

Before editing, fetch remotes and fast-forward the newest intended working
branch to its upstream. Stop and report dirty, divergent, or conflicted state;
never overwrite work to force a sync.

On Windows, use `.\scripts\pio.cmd <arguments>`; it selects the current user's
VS Code-managed installation. Never install another PlatformIO Core; if the
wrapper cannot find it, stop and report the missing installation.

## Role and Target
You are a professional embedded software engineer building a production-grade SCD41 library.

- Target: ESP32-S2 / ESP32-S3, Arduino and native ESP-IDF consumers.
- Device: Sensirion SCD41 photoacoustic NDIR CO2 sensor with integrated temperature and humidity outputs.
- Goals: deterministic behavior, long-term stability, clean API contracts, portability, no surprises in the field.
- These rules are binding.

---

## Repository Model (Single Library)

```text
include/SCD41/         - Public API headers only (Doxygen)
  CommandTable.h       - Command definitions and bit masks
  Status.h
  Config.h
  SCD41.h
  Version.h            - Auto-generated (do not edit)
src/                   - Implementation (.cpp)
examples/
  01_*/
  common/              - Example-only helpers (Log.h, BoardConfig.h, I2cTransport.h,
                         I2cScanner.h, CommandHandler.h)
platformio.ini
library.json
README.md
CHANGELOG.md
AGENTS.md
```

Rules:
- `examples/common/` is NOT part of the library. It simulates project glue and keeps examples self-contained.
- No board-specific pins or bus setup in library code or `Config`; keep them in
  application/example adapters.
- Public headers only in `include/SCD41/`.
- Examples demonstrate usage and may use `examples/common/BoardConfig.h`.
- Keep the layout boring and predictable.

---

## Core Engineering Rules (Mandatory)

- Prefer simplicity, clarity, correctness, robustness, safety, and readability over clever abstractions or speculative flexibility.
- Before coding, inspect whether existing code can be simplified, reused, or deleted.
- Prefer deleting unnecessary code over adding more code.
- Keep changes tightly scoped to the user's request.
- Preserve dirty user changes and never revert unrelated work.
- Prefer extending existing owners, modules, APIs, and contracts over creating parallel abstractions.
- Before adding a service, class, file, interface, or abstraction, verify that an existing owner/module is not the correct home.
- Add abstractions only for a concrete current need with a clear caller or test.
- Do not add placeholder classes, future stubs, empty managers, broad frameworks, plugin systems, registries, generic layers, or speculative extension points.
- Prefer explicit state, explicit ownership, and small local helpers over hidden global state.
- Deterministic: no unbounded loops or waits; all timeouts via deadlines, never `delay()` in library code.
- No unbounded waits, retries, loops, allocations, queues, or buffers in steady paths.
- Every hardware operation that can block must have a timeout and an observable failure path.
- Recovery logic must be bounded, deterministic, and testable.
- Do not hide hardware failures behind silent retries or fake success.
- Non-blocking lifecycle: zero-I2C `begin()`, `start()`, `cancel()`,
  `takeResult()`, and `end()`; only `poll(nowMs, maxCallbacks)` may invoke I2C.
- `tick(nowMs)` is compatibility syntax for `poll(nowMs, 1)`, not a second
  scheduler or completion path.
- Every command sequence is one bounded operation with an absolute deadline,
  request identity, callback limit, cancellation, and one terminal result.
- No heap allocation in steady state (no `String`, `std::vector`, `new` in normal ops).
- Avoid dynamic allocation in steady embedded paths unless it is already an accepted local pattern and the bound is clear.
- No logging in library code; examples may log.
- No macros for constants; use `static constexpr`. Macros only for conditional compile or logging helpers.
- Core/public headers and `src/` must be framework-neutral: no Arduino or ESP-IDF framework headers unless a rare exception is justified in docs and enforced by tooling.
- Arduino APIs (`Arduino.h`, `Wire.h`, `Serial`, `String`, `TwoWire`) are allowed only in Arduino examples or example-only Arduino adapters.
- ESP-IDF examples must be native IDF examples using `app_main`, `driver/i2c_master.h`, `esp_timer`, FreeRTOS timing, and fixed C buffers or native console APIs.
- ESP-IDF examples must not include Arduino CLI source or use `ArduinoCompat`, `IdfArduinoCompat`, `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire` facades.
- Preserve Arduino/ESP-IDF CLI parity through a repo-local command contract/checker, not by sharing Arduino implementation in IDF builds.

---

## Hardening Contract Rules (Mandatory)

- Core public headers and `src/` must remain framework-neutral: no `Arduino.h`,
  `Wire.h`, ESP-IDF headers, FreeRTOS headers, framework logging APIs, dynamic
  framework strings, or global bus ownership.
- SCD41 core must use injected I2C transport. The driver must not own, configure,
  or reset the bus except through explicit application-provided callbacks.
- Public APIs that can fail must expose `Status` or a documented status/result
  channel.
- Long SCD41 command completions must never silently disappear. Async failures
  must be observable by the application through an explicit result channel.
- Timing must use one coherent clock model. Do not mix unrelated time sources for
  scheduling and completion checks.
- Public APIs are not ISR-safe.
- Driver instances are not internally thread-safe unless explicitly changed and
  tested. Applications must serialize multi-task access externally.
- Transport callbacks must not recursively call into the same `SCD41` instance.
- EEPROM-writing and destructive commands must be opt-in and clearly confirmed in
  examples, diagnostics, and HIL scripts.
- Do not claim ESP-IDF build validation, hardware validation, or HIL validation
  without real command output or recorded hardware evidence.

---

## I2C Manager + Transport (Required)

- The I2C bus must have one clear owner.
- The library MUST NOT own I2C. It never touches `Wire` directly.
- Device drivers must not directly own or reconfigure a shared bus unless this repository's architecture explicitly says so.
- `Config` MUST accept a transport adapter (function pointers or abstract interface).
- Adapter errors MUST map to framework-neutral `TransferCode`,
  `TransferDisposition`, and detail values (no leaking `Wire` or `esp_err_t`
  types into the core API).
- The library MUST NOT configure bus timeouts or pins.
- I2C transactions must be timeout-bounded and report errors clearly.
- Do not implement chip protocols manually if an existing hardened project library already provides the needed timeout, recovery, and testability behavior.
- Keep chip-level protocol code inside the driver/wrapper. Keep application policy outside the chip driver.
- Do not add fake devices, simulated buses, or test doubles to production paths.

---

## Status / Error Handling (Mandatory)

All fallible APIs return `Status`:

```cpp
struct Status {
  Err code;
  int32_t detail;
  const char* msg;  // static string only
};
```

- Silent failure is unacceptable.
- No exceptions.

---

## SCD41 Driver Requirements

- I2C address is fixed at `0x62`.
- `begin()` only validates and copies configuration. Explicit `ATTACH` performs
  wake/stop reconciliation, then reads and CRC-checks `get_serial_number` and
  the dedicated `get_sensor_variant` (`0x202F`) response.
- Strict variant policy validates bits `[15:12] == 0x1` only in the dedicated
  sensor-variant response. Serial-number words carry no variant contract.
- All commands are 16-bit, MSB-first.
- Every returned 16-bit data word MUST be CRC-8 checked.
- Every written 16-bit payload word MUST append the correct CRC-8 byte.
- Enforce minimum command spacing `tIDLE >= 1 ms`.
- Wait at least the documented 30 ms power-up settle before the first command.
- Support measurement modes:
  - periodic measurement, 1 sample per 5 s
  - low-power periodic measurement, 1 sample per 30 s
  - single-shot measurement, 5 s execution
  - single-shot RHT-only measurement, 50 ms execution
- Periodic-mode command restrictions are mandatory. While periodic measurement is active, only:
  - `read_measurement`
  - `get_data_ready_status`
  - `set_ambient_pressure`
  - `get_ambient_pressure`
  - `stop_periodic_measurement`
  are allowed without first stopping measurement.
- `stop_periodic_measurement` requires a bounded 500 ms settle window before idle-only commands.
- `wake_up` must treat the command NACK as expected behavior, not as a device fault.
- `get_data_ready_status` must use `(word & 0x07FF) != 0`.
- `read_measurement` returns:
  - CO2 in ppm directly from the raw 16-bit word
  - temperature as `-45 + 175 * raw / 65535`
  - humidity as `100 * raw / 65535`
- Fixed-point conversions should be available:
  - `temperature_mdegC = ((21875 * raw) >> 13) - 45000`
  - `humidity_milliPct = ((12500 * raw) >> 13)`
- Support runtime compensation and maintenance commands:
  - temperature offset
  - sensor altitude
  - ambient pressure override
  - ASC enable/disable
  - ASC target and period settings
  - forced recalibration
  - persist settings
  - reinit
  - self-test
  - factory reset
  - power-down / wake-up
- EEPROM-backed commands (`persist_settings`, factory reset related flows) must be explicit and wear-aware. No hidden persistence.

---

## Driver Architecture: Owner-Driven Operation Engine

The driver has one execution model:

- At most one active operation and one retained terminal result exist.
- `start()` validates/admit work without I2C. Result backpressure prevents a
  new operation until the prior result is consumed.
- `poll(nowMs, maxCallbacks)` is the only transport executor and must never
  exceed the supplied callback budget.
- Every transport callback is one physical attempt. The library performs no
  hidden retry (`OperationLimits::maxRetries == 0`).
- Wait phases perform zero I2C. Absolute deadlines and 32-bit wrap-safe time
  comparisons use the owner-supplied clock and callback completion timestamps.
- `RuntimeSnapshot::nextSafeCommandMs` plus its validity flag is the earliest
  safe next admission. `start()` returns zero-I2C `BUSY` during a retained
  command-spacing or sensor settle window.
- Cancellation stops future host work; it never claims hardware rollback.
- Recovery, bus reset, rail cycling, retries, locking, and scheduling remain
  application policy. Reconciliation after application recovery is `ATTACH`.
- Diagnostic word commands are explicit operations and invalidate managed
  state because unknown commands can have unknown side effects.

### DriverState (4 states only)

```cpp
enum class DriverState : uint8_t {
  UNINIT,    // begin() not called or end() called
  READY,     // Bound; consecutiveTransferFailures == 0
  DEGRADED,  // 1 <= consecutiveTransferFailures < offlineThreshold
  OFFLINE    // consecutiveTransferFailures >= offlineThreshold
};
```

State transitions:
- `begin()` success -> READY (bound, not yet attached)
- Any tracked I2C failure in READY -> DEGRADED
- Success in DEGRADED/OFFLINE -> READY
- Failures reach `offlineThreshold` -> OFFLINE
- `end()` -> UNINIT

### Transport Contract

```text
typed OperationRequest
    -> bounded operation phase
    -> command/CRC helper
    -> one TransferRequest callback attempt
    -> normalized TransferResult + terminal OperationResult
```

Rules:
- A callback must return exact bytes, disposition, and completion time for one
  attempt. Contradictory results are normalized conservatively.
- Every attempted transfer advances the one command-safety gate. `NOT_STARTED`
  does not.
- Expected NACK intent is used only in the documented wake/reconciliation
  phases and must not poison ordinary transfer-failure counters.
- Ambiguous effectful writes are never retried and require reconciliation.

### Health and Cache Rules

- Health is passive telemetry and never blocks a caller-authorized attempt.
- Transport, protocol/CRC, and terminal-operation counters/errors are separate.
- Validation, admission, and precondition failures do not update hardware
  health. Expected NACKs have their own counter.
- Busy/not-ready maps to `MEASUREMENT_NOT_READY` only when command context
  proves that interpretation.
- Identity, sample, and configuration caches carry sensor epoch/provenance.
  Failed, cancelled, reset-like, or ambiguous work must not publish stale state
  as verified.
- Configuration setters read current hardware, avoid unchanged writes,
  invalidate before mutation, and verify by readback.
- `dirtyMask` contains only EEPROM-persistable settings. Ambient pressure is a
  runtime override and never creates persistence work.
- Persistence and factory-reset uncertainty block blind `PERSIST_SETTINGS`
  until explicit reinit/reconciliation clears it.

---

## Versioning and Releases

Single source of truth: `library.json`. `Version.h` is auto-generated and must never be edited.
The version check also synchronizes `idf_component.yml` and `Doxyfile`.

SemVer:
- MAJOR: breaking API, config, enum, or timing-contract changes.
- MINOR: new backward-compatible features or error codes (append only).
- PATCH: bug fixes, refactors, tooling, and docs.

Release steps:
1. Update `library.json`.
2. Update `CHANGELOG.md` (Added/Changed/Fixed/Removed).
3. Update `README.md` and `ASSUMPTIONS.md` if device behavior or scope notes changed.
4. Run native, sanitizer, package, target, and applicable framework builds.
5. Run/record physical HIL gates required by the release policy.
6. Commit and tag: `Release vX.Y.Z`. Do not create the release tag while a
   required physical evidence gate remains open.

---

## Documentation Contract

- Every public type, enum, field, method, parameter, return value, unit, and
  non-owning lifetime rule must have concrete Doxygen at its declaration.
- `doxygen Doxyfile` is warning-clean and warning-as-error. Generated output
  belongs under `.doxygen`; never commit generated HTML into `docs/`.
- `README.md` is the user entry point. Stable device facts belong in
  `docs/reference/`, owner contracts in `docs/integration/`, framework details
  in `docs/porting/`, and evidence procedures in `docs/validation/`.
- `ASSUMPTIONS.md` holds facts the driver cannot prove and product-policy
  boundaries. Completed prompts and closed audits belong in git history, not
  active documentation. Retain dated reports only for real required evidence.
- Update `CHANGELOG.md` whenever public behavior, metadata, or release-visible
  documentation changes.

---

## Naming Conventions

- Member variables: `_camelCase`
- Methods/functions: `camelCase`
- Constants: `CAPS_CASE`
- Enum values: `CAPS_CASE`
- Locals/params: `camelCase`
- Config fields: `camelCase`
