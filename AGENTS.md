# AGENTS.md - SCD41 Production Embedded Guidelines

## Role and Target
You are a professional embedded software engineer building a production-grade SCD41 library.

- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO.
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
- No board-specific pins or bus setup in library code; only in `Config`.
- Public headers only in `include/SCD41/`.
- Examples demonstrate usage and may use `examples/common/BoardConfig.h`.
- Keep the layout boring and predictable.

---

## Core Engineering Rules (Mandatory)

- Deterministic: no unbounded loops or waits; all timeouts via deadlines, never `delay()` in library code.
- Non-blocking lifecycle: `Status begin(const Config&)`, `void tick(uint32_t nowMs)`, `void end()`.
- Any command or wait that can exceed about 1-2 ms must be split into bounded phases driven by `tick()`.
- No heap allocation in steady state (no `String`, `std::vector`, `new` in normal ops).
- No logging in library code; examples may log.
- No macros for constants; use `static constexpr`. Macros only for conditional compile or logging helpers.

---

## I2C Manager + Transport (Required)

- The library MUST NOT own I2C. It never touches `Wire` directly.
- `Config` MUST accept a transport adapter (function pointers or abstract interface).
- Transport errors MUST map to `Status` (no leaking `Wire`, `esp_err_t`, etc.).
- The library MUST NOT configure bus timeouts or pins.

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
- Presence check in `begin()` uses `get_serial_number` and validates SCD41 variant bits `[15:12] == 0x1`.
- All commands are 16-bit, MSB-first.
- Every returned 16-bit data word MUST be CRC-8 checked.
- Every written 16-bit payload word MUST append the correct CRC-8 byte.
- Enforce minimum command spacing `tIDLE >= 1 ms`.
- Respect power-up settle time `<= 30 ms` before the first command.
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

## Driver Architecture: Managed Asynchronous Driver

The driver follows a managed command model with health tracking:

- Short command writes and short reads may remain synchronous.
- Long-running sensor operations (50 ms to 10 s) must be represented as start/poll/read sequences or other bounded state-machine steps driven by `tick()`.
- `tick()` may be used for periodic scheduling, single-shot completion, wake-up settle timing, and other bounded command deadlines.
- Health is tracked via tracked transport wrappers; public API never calls `_updateHealth()` directly.
- Recovery is manual via `recover()` - the application controls retry strategy.

### DriverState (4 states only)

```cpp
enum class DriverState : uint8_t {
  UNINIT,    // begin() not called or end() called
  READY,     // Operational, consecutiveFailures == 0
  DEGRADED,  // 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    // consecutiveFailures >= offlineThreshold
};
```

State transitions:
- `begin()` success -> READY
- Any tracked I2C failure in READY -> DEGRADED
- Success in DEGRADED/OFFLINE -> READY
- Failures reach `offlineThreshold` -> OFFLINE
- `end()` -> UNINIT

### Transport Wrapper Architecture

All I2C goes through layered wrappers:

```text
Public API (startMeasurement, readMeasurement, setOffset, etc.)
    v
Command helpers (writeCommand, writeCommandWithData, readWords)
    v
TRACKED wrappers (_i2cWriteReadTracked, _i2cWriteTracked)
    v  <- _updateHealth() called here ONLY
RAW wrappers (_i2cWriteReadRaw, _i2cWriteRaw)
    v
Transport callbacks (Config::i2cWrite, i2cWriteRead)
```

Rules:
- Public API methods NEVER call `_updateHealth()` directly.
- Command helpers use TRACKED wrappers so health updates automatically.
- `probe()` uses RAW wrappers and does NOT update health.
- `recover()` uses tracked access because the driver is initialized and failures must count.
- Expected `wake_up` NACK must have a dedicated path and must not poison health counters.

### Health Tracking Rules

- `_updateHealth()` is called ONLY inside tracked transport wrappers.
- State transitions are guarded by `_initialized` (no DEGRADED/OFFLINE before `begin()` succeeds).
- Validation failures (`INVALID_CONFIG`, `INVALID_PARAM`) do not update health.
- Precondition failures (`NOT_INITIALIZED`) do not update health.
- `probe()` is diagnostic only and does not update health.
- Busy/not-ready readouts may map to `MEASUREMENT_NOT_READY` only when command context proves that interpretation.

### Health Tracking Fields

- `_lastOkMs` - timestamp of last successful I2C operation
- `_lastErrorMs` - timestamp of last failed I2C operation
- `_lastError` - most recent error `Status`
- `_consecutiveFailures` - failures since last success (resets on success)
- `_totalFailures` / `_totalSuccess` - lifetime counters (wrap at max)

---

## Versioning and Releases

Single source of truth: `library.json`. `Version.h` is auto-generated and must never be edited.

SemVer:
- MAJOR: breaking API, config, enum, or timing-contract changes.
- MINOR: new backward-compatible features or error codes (append only).
- PATCH: bug fixes, refactors, tooling, and docs.

Release steps:
1. Update `library.json`.
2. Update `CHANGELOG.md` (Added/Changed/Fixed/Removed).
3. Update `README.md` and `ASSUMPTIONS.md` if device behavior or scope notes changed.
4. Commit and tag: `Release vX.Y.Z`.

---

## Naming Conventions

- Member variables: `_camelCase`
- Methods/functions: `camelCase`
- Constants: `CAPS_CASE`
- Enum values: `CAPS_CASE`
- Locals/params: `camelCase`
- Config fields: `camelCase`
