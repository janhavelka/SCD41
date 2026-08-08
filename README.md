# SCD41 Driver Library

Framework-neutral C++17 driver for the Sensirion SCD41 CO2, temperature, and
humidity sensor. Arduino and native ESP-IDF examples are included for ESP32-S2
and ESP32-S3.

Release status: `library.json` is staged at `1.3.1` for compatibility and
release-candidate validation, but no `v1.3.1` release/tag or current physical
HIL pass is claimed. See [CHANGELOG.md](CHANGELOG.md) and the
[hardware validation guide](docs/validation/hardware-hil.md).

The library is designed for an application-owned I2C bus. It does not configure
pins, own a bus, create a task, lock, log, sleep, retry, reset the bus, or power
cycle hardware. The application supplies one bounded transfer callback and
decides when the driver may use it.

## Main properties

- Fixed-memory, single-operation state machine.
- `begin()`, `start()`, `cancel()`, `takeResult()`, and cache snapshots perform
  no I2C.
- `poll(nowMs, maxCallbacks)` advances work and never calls the transport more
  than `maxCallbacks` times.
- Long sensor waits are zero-I2C poll phases. There is no library `delay()`.
- Every operation has a caller request ID, generation, immutable deadline, and
  exactly-once terminal result.
- Transfer errors preserve whether an effect was not started, had no effect,
  completed, or is indeterminate.
- The driver performs no automatic transfer retries or bus recovery.
- All returned SCD41 words are CRC-8 checked. All word payloads include CRC.
- Core headers and source have no Arduino, ESP-IDF, FreeRTOS, or logging
  dependency.
- Lifecycle, health, snapshots, and enum-name helpers are cache-only and
  allocation-free; they never probe or otherwise touch I2C.

## Minimal owner loop

```cpp
#include <SCD41/SCD41.h>

SCD41::TransferResult transfer(const SCD41::TransferRequest& request,
                               void* user);

SCD41::SCD41 sensor;

void bindSensor() {
  SCD41::Config config;
  config.transfer = transfer;
  config.transferUser = nullptr;
  config.transferTimeoutMs = 20;

  const SCD41::Status bound = sensor.begin(config); // validates and copies; zero I2C
  if (!bound.ok()) {
    return;
  }

  const uint32_t nowMs = applicationMonotonicMs();
  const SCD41::OperationRequest request =
      SCD41::OperationRequest::make(SCD41::OperationKind::ATTACH);
  const SCD41::OperationLimits limits = sensor.limits(request.kind);

  SCD41::OperationOptions options;
  options.requestId = nextApplicationRequestId();
  options.nowMs = nowMs;
  constexpr uint32_t OWNER_SCHEDULING_MARGIN_MS = 1000;
  options.deadlineMs = nowMs + limits.maxWaitMs +
      static_cast<uint32_t>(limits.maxCallbacks) * config.transferTimeoutMs +
      OWNER_SCHEDULING_MARGIN_MS;

  SCD41::OperationId id;
  const SCD41::Status started = sensor.start(request, options, id);
  if (!started.inProgress()) {
    return;
  }
}

void serviceSensor(uint32_t nowMs) {
  const SCD41::PollResult progress = sensor.poll(nowMs, 1);
  if (progress.state != SCD41::OperationState::RESULT_PENDING) {
    return;
  }

  SCD41::OperationResult result;
  const SCD41::Status taken = sensor.takeResult(progress.id, result);
  if (taken.ok()) {
    publishOrHandle(result);
  }
}
```

The transfer callback must execute exactly one physical attempt and return its
completion timestamp in the same wrapping 32-bit monotonic millisecond domain
used by `start()` and `poll()`. It must honor `request.timeoutMs`. A callback
must not recursively enter the same driver instance.

Validated configuration ranges are: transfer timeout `1..1000 ms` and power-up
delay `30..1000 ms`. Defaults are 50 ms and 30 ms respectively. The application
should choose the smallest transport timeout that is safe for its controller and
bus. The driver enforces the fixed 1 ms SCD41 inter-command spacing internally.

| `Config` field | Contract | Default |
| --- | --- | ---: |
| `transfer` | Required synchronous callback for exactly one bounded attempt | null (invalid) |
| `transferUser` | Non-owning context that must outlive use of the current binding | null |
| `transferTimeoutMs` | Per-attempt bound in the range 1..1000 ms | 50 ms |
| `powerUpDelayMs` | Initial attach wait in the range 30..1000 ms | 30 ms |
| `offlineThreshold` | Consecutive attempted failures for passive `OFFLINE`; zero disables only that transition | 5 |
| `strictVariantCheck` | Require verified SCD41 variant identity | true |

See [External I2C owner integration](docs/integration/external-i2c-owner.md) for
the complete contract and [ESP-IDF porting](docs/porting/esp-idf.md) for a native
adapter.

## Operation model

Only one operation or one unconsumed result exists per instance.

1. Call `start(request, options, id)`. Admission performs no I2C.
2. Call `poll(nowMs, budget)` from the I2C-owning context. A budget of `1`
   means at most one transfer callback in that call.
3. Continue at or after `PollResult::nextDueMs`. Wait-only phases use zero
   callbacks.
4. When state becomes `RESULT_PENDING`, call `takeResult(id, result)` once.
5. Start the next request only after consuming the terminal result.

When `RuntimeSnapshot::nextSafeCommandValid` is true, honor
`nextSafeCommandMs` even when its wrapped value is zero. `start()` returns
zero-I2C `BUSY` while command spacing or a retained sensor settle window is
active.

`cancel(id, nowMs)` cancels host-side future work without I2C and retains a
`CANCELLED` result. It cannot undo bytes already accepted by the sensor.
`end()` also performs no I2C; active work becomes a retained cancelled result
timestamped with the last owner time accepted by the driver.
`tick(nowMs)` is a narrow compatibility executor equivalent to a one-callback
poll. New owner integrations should use `poll()` because its result exposes
state, identity, callback use, and the next due time.

`SCD41::SCD41::limits(kind)` is the public scheduling contract for each
operation. It reports the operation class, maximum callback count, internal
retry count,
sensor wait, and whether the operation writes nonvolatile state or is
destructive. The library's internal retry count is zero. Applications may
retry only as new, separately identified operations after interpreting the
previous terminal effect and reconciliation state.

## Public result types

The public API is fixed-size and allocation-free:

- `TransferRequest` and `TransferResult` are the one-attempt adapter boundary.
- `OperationRequest`, `OperationOptions`, `OperationId`, and `OperationLimits`
  define an admitted job and its scheduling contract.
- `PollResult` reports bounded progress; `OperationResult` is the exactly-once
  terminal record.
- `RuntimeSnapshot`, `HealthSnapshot`, `Identity`, `ConfigurationSnapshot`, and
  `FixedSample` are cache-only views and never perform I2C.
- `Status` carries a stable `Err`, optional detail, and static-lifetime message.
  `errorName`, `driverStateName`, and the descriptive helpers for the public
  CLI-facing enums have `toString` aliases and return allocation-free,
  static-lifetime names.

`isInitialized()` is a compatibility alias for zero-I2C `isBound()`.
`isOnline()` reports only the passive transfer state: it is true in `READY` and
`DEGRADED`, false in `UNINIT` and `OFFLINE`, and does not imply a verified
sensor attachment. The direct `lastOkMs()`, `lastErrorMs()`, `lastError()`,
`consecutiveFailures()`, `totalFailures()`, and `totalSuccess()` accessors map
only to the transfer channel of `HealthSnapshot`; use the snapshot for separate
protocol/CRC and logical-operation telemetry. A successful `begin()` starts a
new health session and clears all of those fields. A rejected `begin()` leaves
the current session unchanged, while `end()` reports `UNINIT` and preserves the
session history until the next successful bind.

Interpret `OperationResult::value` by `OperationResult::kind`; other value
members remain default or are secondary raw evidence:

| Operation kind/group | Authoritative value member |
| --- | --- |
| `ATTACH`, `READ_IDENTITY`, `READ_SENSOR_VARIANT`, `WAKE_UP`, `REINIT` | `identity` (`value` also contains the raw word for `READ_SENSOR_VARIANT`) |
| `FACTORY_RESET` | `identity` and reconciled `configuration` |
| `READ_DATA_READY` | `dataReady` |
| `FETCH_SAMPLE`, `SINGLE_SHOT`, `SINGLE_SHOT_RHT_ONLY` | `sample` |
| `READ_TEMPERATURE_OFFSET` | `signedValue` plus `configuration` |
| `FORCED_RECALIBRATION` | `signedValue` plus raw response evidence |
| altitude, pressure, numeric ASC reads | `value` plus `configuration` |
| `SELF_TEST` | `value` plus raw response evidence |
| `READ_ASC_ENABLED` | `boolValue` plus `configuration` |
| setting writes, `READ_CONFIGURATION`, `PERSIST_SETTINGS` | `configuration` |
| diagnostic reads and deferred maintenance responses | `rawWords`, `wordCount` |

The detailed owner actions for every outcome/effect combination are in
[External I2C owner integration](docs/integration/external-i2c-owner.md).

## Operation classes

| Class | Intended use | Examples |
| --- | --- | --- |
| `STEADY_STATE` | Normal owner-task reads in small chunks | identity/data-ready/config-field reads, sample fetch |
| `RUNTIME` | Multi-step startup, mode, conversion, and configuration work | attach, periodic start/stop, single shot, settings writes, sleep/wake, reinit |
| `MAINTENANCE` | Explicit slow, calibration, or nonvolatile work | self-test, forced recalibration, persist settings, factory reset |
| `DIAGNOSTIC` | Controlled lab access with typed transaction shapes | CRC-checked word reads and explicit diagnostic writes |

The class does not change ownership: all work still advances only through
caller polling and the supplied callback budget.

Diagnostic command values are arbitrary. The driver cannot prove that even a
command followed by a read has no side effect, so every dispatched diagnostic
operation invalidates managed state and requires a new `ATTACH` before normal
production operations.

## Results, cache, and ambiguous effects

`OperationResult` includes the operation ID and kind, terminal outcome, effect
state, final phase, status, start/completion/deadline timestamps, callback count,
sensor epoch, completed configuration-field mask, and typed value storage.

Terminal outcomes distinguish `SUCCEEDED`, `NO_DATA`, `FAILED`, `CANCELLED`,
`TIMED_OUT`, `PARTIAL`, and `INDETERMINATE`. For effectful writes, the result
also distinguishes no attempt, attempted, acknowledged, verified, and unknown
effect. The driver never blindly retries an ambiguous write.

Cache-only access is available through:

- `runtimeSnapshot()`
- `healthSnapshot()`
- `configurationSnapshot()`
- `identity()`
- `peekLatestSample()`

Configuration fields become verified only after successful readback. A failed,
timed-out, cancelled, diagnostic, or ambiguous operation marks affected state
dirty or requires reconciliation as applicable. Reset-like operations advance
the sensor epoch so an older sample cannot be presented as current sensor state.
Setting operations read the current value first, skip the write when it already
matches, and verify after a real write. `dirtyMask` means this driver changed or
may have changed an EEPROM-persistable field and does not know it to be
persisted; a field may be both verified and dirty. Ambient pressure is a runtime
override and never creates persistence work. Persistence is rejected while any
dirty field is unverified. Persistence is a zero-write success when this
instance has no known unpersisted field. That no-op does not read or prove
EEPROM contents after a fresh bind.

`READ_SENSOR_VARIANT` refreshes the decoded family and raw variant word without
rereading the serial number. When the family still agrees, the already verified
composite identity remains valid. A different or strictly unsupported family
invalidates the composite identity, publishes only the newly observed variant
evidence, and requires a new `ATTACH` before managed work.

CRC-valid configuration responses are also checked against every explicit
datasheet domain before publication: altitude 0..3000 m, pressure word
700..1200, ASC enable 0/1, and ASC periods in 4-hour steps. A violating response
records a protocol failure, clears the affected verified bit, and does not
overwrite the prior cached value. Temperature offset spans the full encoded
0..175 C domain; the datasheet's 0..20 C range is a recommendation. ASC target
and FRC reference use the complete uint16 ppm command-word domain because v1.7
does not state a narrower valid range.

`FixedSample` contains fixed-width CO2 ppm, temperature in milli-degrees C,
humidity in milli-percent, capture time, sensor epoch, sequence, mode, and
validity/freshness flags. Sequence is 1-based since the latest sensor epoch or
mode transition; the application chooses any warm-up discard count. The
RHT-only operation does not mark CO2 valid.

## Supported device functionality

- CRC-protected serial identity plus dedicated `get_sensor_variant` validation
  at fixed address `0x62`; typed serial-plus-variant and variant-only reads are
  available.
- Periodic measurement at 5 s and low-power periodic measurement at 30 s.
- Full 5 s single-shot and 50 ms RHT-only single-shot measurement.
- Data-ready and CRC-checked sample reads.
- Temperature offset, altitude, ambient pressure, ASC enable, ASC target, and
  ASC initial/standard period reads and writes. The composite configuration
  snapshot is idle-only; periodic mode exposes only the device-permitted
  ambient-pressure operations.
- Live configuration snapshot read.
- Power-down, wake-up, reinit, self-test, forced recalibration, explicit
  persistence, and factory reset.
- Bounded diagnostic command/word operations.
- Pure fixed-point/float conversion and encoding helpers.

While periodic measurement is active, the driver enforces the SCD41 command
restriction: only measurement read, data-ready status, ambient pressure access,
and stop-periodic are admitted. Stop-periodic includes the required 500 ms
zero-I2C settle phase.
Typed stop while already idle is rejected without I2C. Unknown retained mode is
handled only by the explicit `ATTACH` reconciliation sequence.

## Transport result contract

The adapter maps platform results into:

- `TransferCode`: `OK`, `NACK`, `TIMEOUT`, `BUS_ERROR`, `SHORT_TRANSFER`, or
  `FAILED`.
- `TransferDisposition`: `NOT_STARTED`, `NO_EFFECT`, `COMPLETE`, or
  `INDETERMINATE`.

`OK` requires the complete requested transfer. Short or zero-length responses
when bytes were requested must not be reported as success. `bytesTransferred`
must be accurate when the platform exposes it.

The SCD41 wake command can NACK by design, and attach reconciliation may see a
stop-command NACK when no periodic mode is active. Those marked phases carry
`EXPECTED_WRITE_NACK`, so an adapter exposing only a generic NACK need not
invent address/data precision. Timeouts and bus errors are not expected NACKs.

## Concurrency and ownership

- Instances are not internally thread-safe. Serialize every call for an
  instance, normally in the bus-owner task.
- Public APIs are not ISR-safe.
- The application owns callback blocking bounds, locking, retries, bus health,
  bus reset, sensor rail control, and recovery policy.
- Driver health is diagnostic. `OFFLINE` does not seize recovery authority or
  silently block a caller-authorized operation.
- Do not call the transport callback independently while a driver poll is using
  the same bus without the application's normal serialization.

## Device and hardware notes

- Allow up to 30 ms after power-up before the first command.
- Minimum command spacing is 1 ms.
- All 16-bit commands are MSB-first; every returned data word has CRC-8.
- The sensor's photoacoustic pulse can draw 175-205 mA at 3.3 V. Provide a sound
  supply and at least 10 uF local bulk capacitance.
- `persist_settings` is rated for at least 2000 EEPROM write cycles. It is never
  implicit.
- Forced recalibration requires a known, stable reference. Factory reset and
  persistence require explicit request confirmation.
- FRC/ASC calibration history is stored automatically in a separate EEPROM
  dimensioned for sensor lifetime; FRC is still nonvolatile maintenance.
- Warm-up and measurement-quality policy belong to the product; the current
  vendor datasheet no longer prescribes discarding the first single-shot result.

`Config::strictVariantCheck` defaults to true. Setting it false is a diagnostic
escape hatch that can attach to another observed SCD4x identity; it is not a
support claim. Known SCD41-only operations remain rejected with `UNSUPPORTED`.

Detailed device facts are in
[SCD41 protocol reference](docs/reference/scd41-protocol.md).

## Examples and validation

- Arduino/PlatformIO CLI: `examples/01_basic_bringup_cli`
- Native ESP-IDF CLI: `examples/idf/basic`
- Hardware evidence process: [Hardware and HIL validation](docs/validation/hardware-hil.md)

The CLIs use the same command contract. Each loop advances the driver with one
callback per poll and automatically consumes and prints terminal results.
`probe` performs a CRC/variant-qualified identity check (attaching first when
needed), while `attach` / `recover` reconcile sensor protocol state. Cooperative
`stress`, mode-safe `stress_mix`, and aggregate `selfcheck` workflows schedule
one typed operation at a time and emit matching colored summaries in Arduino
and native ESP-IDF. EEPROM, calibration, factory-reset, and all raw diagnostic
commands require an explicit `confirm` token. Raw reads are not assumed safe
merely because a response is expected.

In a product with a separate console/network task, submit these commands to the
sole I2C owner; do not call the example's raw bus scan or a transport callback
from another task. See the
[feature coverage matrix](docs/validation/feature-coverage.md) and
[external-owner scheduling contract](docs/integration/external-i2c-owner.md).

Host checks on Windows (using the repository's approved PlatformIO wrapper):

```powershell
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_repository_hygiene.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/test_scd41_hil_runner.py
python tools/scd41_hil_runner.py --parser-self-test
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd run -e esp32s3dev
.\scripts\pio.cmd run -e esp32s2dev
doxygen Doxyfile
```

Doxygen writes the generated reference to `.doxygen/html/index.html`; do
not commit generated HTML under `docs/`.

Ubuntu CI additionally runs the `native_ubsan` environment; the Windows MinGW
toolchain commonly used by PlatformIO does not provide the required UBSan
runtime.

PlatformIO 6.1.19 or newer builds the Arduino examples for ESP32-S2 and
ESP32-S3 on the exact-pinned pioarduino `platform-espressif32` `55.03.311`
stack (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`). This pin controls only this
repository's examples; consuming applications retain control of their own
platform version. Native host tests use exact-pinned `native@1.2.1`. CI also
builds the native ESP-IDF example for both targets
and validates a packed-library consumer. A separate compatibility job
compile-links that package for TunnelMonitor-node's integration target
`esp32-s3-wroom-n16r8` on its retained pioarduino `54.03.20` stack.

## Versioning

`library.json` is the version source of truth. `include/SCD41/Version.h`,
`idf_component.yml`, and `Doxyfile` project metadata are generated or checked
from it. Version 1.3.1 tightens documentation, HIL evidence metadata, repository
hygiene, and reproducible documentation validation. Version 1.3.0 added
backward-compatible diagnostic naming and direct
transfer-health views on top of the staged 1.0.0 operation model. That
1.0.0 baseline was the breaking API change replacing direct calls
and dual transport callbacks with the
externally scheduled operation model.

## License

MIT. See [LICENSE](LICENSE).
