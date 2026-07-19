# SCD41 Library Suitability Audit for TunnelMonitor-node

Baseline date: 2026-07-19
Re-audit completion date: 2026-07-19
Status: library refactor complete; TunnelMonitor production integration not performed
Verdict: **library-side owner contract resolved; physical SCD41 validation and
TunnelMonitor product decisions remain before production integration**

## Re-audit And Implementation Disposition

The findings below were rechecked at SCD41 revision
`a66ac59ceb2044b0884a7bdf6111c75fc0fa1ef9` and TunnelMonitor-node revision
`b708f511964db6c51e949e99c67820476f00f9c7`. All twelve findings still applied
to that baseline. The SCD41 work was performed on
`hardening/tunnelmonitor-suitability-reaudit`. TunnelMonitor-node remained
read-only.

The implemented design is one fixed-memory operation slot plus one retained
terminal-result slot. `begin()` only validates and binds. `start()` only admits
and identifies work. `poll(nowMs, budget)` is the only transport executor. The
application supplies the absolute deadline and owns scheduling, locking,
retries, bus recovery, and power control. The library performs no hidden retry.

| Finding | Revalidated baseline evidence | Resolution and implementation evidence | Test evidence | Final status |
| --- | --- | --- | --- | --- |
| TM-SCD-01 - I2C budget was not end-to-end | Starts, polls, and completion paths could perform work outside one owner budget. | Every device action is an `OperationKind`. `start()` is zero-I2C. `poll()` receives an explicit callback budget and reports exact use. `OperationLimits` gives hard callback, cumulative sensor-wait, retry, nonvolatile, and destructive bounds. | `test_start_is_zero_io_and_result_backpressure_is_exact`, `test_attach_budget_waiting_and_expected_wake_nack`, stage-failure matrix. | **RESOLVED** |
| TM-SCD-02 - No logical deadline or cancellation | The old mixed synchronous/async procedures had no immutable job deadline, exact cancellation result, or request correlation. | `OperationOptions` carries request ID, start time, and absolute deadline. `OperationId` adds a non-reused generation. `cancel()` is zero-I2C. One matching terminal result is consumed exactly once. | Deadline boundary/callback/wrap tests, backward-clock test, cancellation-before/after-effect tests, stale/exactly-once tests. | **RESOLVED** |
| TM-SCD-03 - Initialization assumed idle sensor | The old initialization could issue idle-only work without converging a sensor left periodic or powered down by an MCU restart. | `begin()` performs no I2C. `ATTACH` is a bounded wake, settle, stop, settle, serial/CRC/variant verification sequence. It converges from idle, periodic, low-power periodic, and power-down modes and starts a new cache epoch on hotplug. | Attach mode/hotplug test plus fault injection at every attach transfer. | **RESOLVED** |
| TM-SCD-04 - Two recovery authorities | The old library exposed bus reset/power-cycle recovery and blocked work through active offline policy. | Bus reset, power-cycle, library retry, automatic recovery, and recovery admission were removed. Health is passive only. `OFFLINE` never blocks a caller-authorized transfer. Reconciliation is a typed `ATTACH` operation after application recovery. | Passive-offline test and rebind/context tests. | **RESOLVED** |
| TM-SCD-05 - Illegal/conflicting public work | Multiple old executors could overlap and admission allowed commands in invalid sensor modes. | One active operation/result invariant, zero-I2C admission, typed mode restrictions, result backpressure, and managed-command exclusion from diagnostics. Periodic mode admits only the commands allowed by the device. | Backpressure, periodic-admission, diagnostic-invalidation, end/rebind, and operation-stage tests. | **RESOLVED** |
| TM-SCD-06 - Wake NACK was adapter-specific | Expected wake NACK could not be represented portably by the prior split transport API. | One `TransferRequest`/`TransferResult` callback uses `EXPECTED_WRITE_NACK`, generic `NACK`, and an explicit effect disposition. Expected wake NACK is counted separately and does not poison health. | Attach expected-NACK test; Arduino and native ESP-IDF adapter contract checks. | **RESOLVED** |
| TM-SCD-07 - No direct fixed-point sample | The old public result path centered float values and did not provide a fixed-size consuming result suitable for the owner task. | `FixedSample` provides CO2 ppm, milli-degrees C, milli-percent RH, time, sequence, mode, sensor epoch, and validity flags. The terminal result is consuming; `peekLatestSample()` is explicitly non-consuming cache access. | Fixed-point sample/result/cache and conversion-boundary tests. | **RESOLVED** |
| TM-SCD-08 - Settings cache could be stale but valid | Setters could publish desired values without complete verification and did not represent partial configuration reads. | Setters read current hardware, skip unchanged writes, invalidate before mutation, and read back after mutation. `verifiedMask`, `dirtyMask`, `completedFieldMask`, sensor epoch, and persistence-indeterminate state expose provenance. Ambiguous writes are never retried. | Fourteen-stage configuration read faults, setter readback/no-op, mutation ambiguity, later-verification fault, persistence retry-block tests. | **RESOLVED** |
| TM-SCD-09 - Release and physical evidence incomplete | The baseline manifest/tag/evidence did not describe the substantially changed API or prove current hardware behavior. | Manifest/component/generated headers are aligned at `1.0.0`; packaging and clean consumers are checked; the stale retained HIL transcript is excluded from release artifacts. A release tag is intentionally withheld until physical evidence exists. | Version guard, package checks, Arduino S2/S3 builds, clean source/package consumers, exact Tunnel target package build. Physical HIL was not run. | **EXTERNAL GATE: PHYSICAL HIL** |
| TM-SCD-10 - Transport health hid operation health | Transport-phase success/failure did not describe logical job completion. | `HealthSnapshot` separates transport, protocol/CRC, and terminal operation counters/errors. Terminal outcomes include success, no-data, failure, cancellation, timeout, partial, and indeterminate. Health remains diagnostic. | Transport-contract, CRC, passive-offline, cancellation, and stage-failure tests. | **RESOLVED** |
| TM-SCD-11 - Attempt timing and float validation defects | Attempt timestamps could be recorded before the callback and float encoding did not reject all non-finite/extreme inputs. | Every callback returns its completion time in the owner clock domain. Deadline and command spacing use completion time. Backward callback time and contradictory/short completion contracts fail observably. Float helpers require finite in-range values. | Callback-deadline, transport-contract, owner-clock, wrap, and extreme-float tests. | **RESOLVED** |
| TM-SCD-12 - Two apparent clocks | The old API combined caller time with platform timing hooks and an inert fallback clock. | Timing hooks and `PlatformTime.h` were removed. Start, poll, cancel, transfer completion, deadline, and spacing use one wrapping 32-bit monotonic millisecond domain. The driver rejects a backward owner clock without I2C. | Clock-wrap and backward-owner/callback-clock tests; core timing guard. | **RESOLVED** |

Current host verification has 37 public-contract native tests passing. Arduino
ESP32-S2 and ESP32-S3 builds, source/package consumers, repository guards, and
the exact Tunnel target package compile/link check pass. Native ESP-IDF tools,
local UBSan runtime, and physical hardware were unavailable; these are not
claimed as passing. Linux CI includes the UBSan job.

The library is now suitable for a future private TunnelMonitor leaf adapter.
TunnelMonitor must still define the product hardware mapping, CO2 measurement
contract, warm-up/publication policy, calibration values, maintenance policy,
and exact dependency pin. None of those product facts were invented here.

The remainder of this document preserves the detailed baseline evidence and
original proposed direction. Old line numbers refer to the audited baseline,
not the completed refactor.

## Baseline Executive Summary

The SCD41 library has a solid chip-protocol core. It already has injected I2C
transport, CRC checking, fixed-point conversions, measurement-mode tracking,
variant checking, framework-neutral source, no steady-state heap use, and good
native test coverage. This work should be retained.

The current lifecycle and job model does not meet TunnelMonitor-node's I2C owner
contract. Some public calls block, some `start*` calls perform I2C immediately,
some `poll()` completions perform more callbacks than their instruction budget,
and logical jobs have no absolute deadline or cancellation path. Recovery and
offline admission are also owned partly by the sensor library and partly by the
application. That creates two recovery authorities on one shared bus.

The correct direction is a focused refactor, not a new protocol implementation
and not a TunnelMonitor-side workaround. Keep the protocol codecs and typed
commands. Replace the mixed blocking/`tick()`/`poll()` execution model with one
passive, owner-stepped job model where every physical transfer is explicit and
bounded.

The main release blockers are:

1. Initialization and several command starts are not fully owner-poll driven.
2. Jobs have no absolute deadline or deterministic cancellation.
3. The library cannot safely attach after an MCU-only restart when the sensor
   may still be in periodic mode.
4. Library-owned offline gating and recovery escalation conflict with the sole
   I2C owner.
5. State validation permits an illegal single-shot command while periodic mode
   is active, and the public executors can interfere with each other.
6. Expected wake-up NACK handling is not usable with the supplied ESP-IDF
   adapter or TunnelMonitor's current generic NACK result.
7. There is no consuming fixed-point sample API.
8. Settings cache validity can become false after a successful setter.
9. There is no passing physical SCD41 HIL evidence for this library revision,
   and the current code is substantially newer than the `v0.1.0` tag while the
   manifest still reports `0.1.0`.

## Audit Baseline

| Repository | Branch | Commit |
| --- | --- | --- |
| SCD41 | `hardening/scd41-industry-readiness` | `a66ac59ceb2044b0884a7bdf6111c75fc0fa1ef9` |
| TunnelMonitor-node | `docs/mb85rc-suitability-contract-facts` | `b708f511964db6c51e949e99c67820476f00f9c7` |

The TunnelMonitor-node repository was inspected read-only. No TunnelMonitor
source, contract, guideline, build configuration, or report was changed.

This audit covers:

- the SCD41 public API, transport contract, state machine, timing, recovery,
  settings, tests, packaging, and retained HIL evidence;
- TunnelMonitor's authoritative I2C ownership, measurement, dependency, health,
  and platformization requirements;
- the current TunnelMonitor I2C backend and ENV/sample contracts;
- work required in the SCD41 library before a private TunnelMonitor adapter is
  reasonable.

## TunnelMonitor Integration Contract

The relevant TunnelMonitor requirements are already clear:

- `I2cTask` is the sole I2C owner. Device libraries must not own the bus, task,
  queue, retry policy, or bus recovery policy
  (`TunnelMonitor-node/docs/guidelines/ownership.md:174-205`).
- The accepted RTC-library pattern is a zero-I/O bind followed by typed jobs
  advanced by the I2C owner with one callback per poll. The application owns
  absolute deadlines, recovery, and result mapping
  (`TunnelMonitor-node/docs/guidelines/dependency_policy.md:130-189`).
- Normal device-library callbacks are one physical attempt. They do not hide a
  retry or recovery sequence
  (`TunnelMonitor-node/docs/guidelines/dependency_policy.md:145-174`).
- I2C commands carry a fixed request identity, a `Deadline64`, a bounded
  payload, and a later typed result
  (`TunnelMonitor-node/include/TunnelMonitor/contracts/FieldBus.h:13-82`).
- Optional I2C leaves are intended to become compile-time selected static
  descriptors under the same owner, not runtime plugins or additional owners
  (`TunnelMonitor-node/prompts/prompt_45g_optional_i2c_leaf_descriptors.md:19-34`).
- The measurement runtime is also moving to fixed compile-time source
  descriptors with typed, bounded results
  (`TunnelMonitor-node/prompts/prompt_45f_active_measurement_sources.md:19-35`).

These constraints are reasonable for an unattended ESP32-S3 node. The sensor
library needs to fit them directly.

## What Already Fits Well

| Area | Finding |
| --- | --- |
| Core portability | Public headers and `src/` are framework-neutral. The core does not include Arduino, Wire, ESP-IDF, or FreeRTOS headers. |
| Bus ownership | I2C is injected through callbacks. The library does not configure pins, bus frequency, or a global bus object. |
| Memory | Driver state and buffers are fixed-size. No steady-state dynamic allocation was found in the core. |
| Protocol | Command words are typed, MSB-first, and payload CRC bytes are generated. Returned words are CRC checked. |
| Device identity | `begin()` reads the serial number and checks the SCD41 variant by default. |
| Measurement support | Periodic, low-power periodic, full single-shot, and RHT-only single-shot modes exist. |
| Units | Fixed-point CO2 ppm, milli-Celsius, and milli-percent RH output already exists in `CompensatedSample`. |
| Mode restrictions | Most public and raw-command paths enforce periodic-mode restrictions. |
| Long operations | Long completion waits are represented as pending state rather than a single 5-10 second blocking call. |
| Diagnostics | Transport and protocol/CRC counters are separate and observable. Cached sample epoch/freshness data exists. |
| Build coverage | CI covers native tests, Arduino ESP32-S2/S3 builds, package checks, and ESP-IDF ESP32-S2/S3 example builds. |

The protocol layer should be refactored in place. Reimplementing SCD41 commands
inside TunnelMonitor would duplicate working code and create a second silicon
model.

## Hard Findings

### TM-SCD-01 - The I2C instruction budget is not an end-to-end guarantee

Priority: **P0 - integration blocker**

Evidence:

- `begin()` waits for the configured power-up delay and performs serial-number
  I2C before returning (`SCD41/src/SCD41.cpp:67-193`).
- Public `start*` methods perform their initial command write synchronously.
  Examples include periodic start (`SCD41/src/SCD41.cpp:664-693`), stop
  (`:731-750`), wake (`:772-791`), maintenance commands (`:1135-1266`), and
  single shot (`:2251-2292`).
- The external-owner guide acknowledges that initial writes are outside the
  poll executor (`SCD41/docs/integration/external-i2c-owner.md:54-60`).
- Due `WAKE_UP` and `POWER_CYCLE` work in `_pollPendingCommand()` calls
  `_verifySensorAfterRecovery()` directly
  (`SCD41/src/SCD41.cpp:2566-2581`). That verification performs a serial command
  write and response read (`:2456-2468`, `:806-819`) without decrementing the
  `instructionsRemaining` budget. A `poll(..., 1)` call can therefore perform
  two I2C callbacks plus a command-spacing wait.
- Short synchronous setters perform a write and then call `_waitMs()`; reads
  perform command/write and response/read in one public call
  (`SCD41/src/SCD41.cpp:873-1117`).
- Blocking command-spacing and delay loops use an iteration guard of roughly
  four iterations per millisecond plus 16 (`SCD41/src/SCD41.cpp:57-63,
  2190-2235`). `cooperativeYield` is optional. With a real millisecond clock and
  a no-op or very fast yield, the iteration guard can expire before wall time
  advances. That makes false `TIMEOUT` possible.

Impact:

- TunnelMonitor cannot prove one physical attempt per owner poll.
- A supposedly bounded leaf call can occupy the shared worker longer than its
  scheduler budget and delay RTC, FRAM, display, or other sensor work.
- Owner timing metrics would report one step while more than one transfer
  happened.
- A Tunnel adapter would need special cases around every synchronous method.
  That would be a band-aid and would recreate library state outside the library.

Required refactor:

- Make configuration/binding zero-I/O.
- Represent initialization, all command writes, command-spacing gates, response
  reads, stop settle, wake verification, recovery verification, and settings
  work as phases of the same poll-driven job engine.
- Define one poll budget as one transport callback. A successful return must
  report the exact callbacks consumed.
- Remove CPU polling waits from normal APIs. Store the next due time and return
  `IN_PROGRESS` without waiting.
- Keep any intentionally blocking diagnostic API separate and clearly named;
  TunnelMonitor must not need it.

Acceptance criteria:

- Every normal job, including initialize, wake, power-cycle notification,
  configuration read/write, and maintenance work, has tests proving zero or one
  callback when polled with a budget of one.
- No normal public `start*` method touches transport.
- No normal job contains a time-wait loop.

### TM-SCD-02 - Logical jobs have no deadline or cancellation

Priority: **P0 - integration blocker**

Evidence:

- The library accepts only per-transfer `i2cTimeoutMs`; job start APIs do not
  accept an absolute job deadline.
- A not-ready measurement is rescheduled by `dataReadyRetryMs` repeatedly
  (`SCD41/src/SCD41.cpp:2510-2527, 2650-2773`). There is no maximum attempt
  count or absolute completion deadline.
- No public cancel/abort/reset-pending-job API exists. `end()` clears local
  state, but it is a full detach and performs no sensor-state reconciliation
  (`SCD41/src/SCD41.cpp:280-321`).
- Tunnel I2C commands carry an absolute deadline and the owner must not renew it
  (`TunnelMonitor-node/include/TunnelMonitor/contracts/FieldBus.h:40-58` and
  `TunnelMonitor-node/docs/guidelines/measurement_data.md:66-72`).

Impact:

- TunnelMonitor can detect that its outer command expired, but it cannot
  terminally settle the library job without discarding the whole driver state.
- A not-ready or interrupted job can hold the library busy indefinitely.
- Late results can be confused with a later owner request unless the adapter
  builds a second shadow state machine.

Required refactor:

- Give every job one immutable absolute deadline in the driver's monotonic
  clock domain.
- Check that deadline before every phase and after every callback.
- Add a deterministic terminal result such as `DEADLINE_EXPIRED`.
- Add explicit cancellation. Cancellation must document the known sensor state
  for each job type. If the sensor state cannot be proven, require a later
  bounded attach/convergence job.
- Do not add an internal retry queue. A later owner request is the retry.

Acceptance criteria:

- Native tests expire every job at every phase boundary.
- Expiry and cancellation always clear local busy state and produce one
  terminal result.
- A late poll cannot complete an expired generation.

### TM-SCD-03 - Initialization assumes an idle sensor

Priority: **P0 - field restart blocker**

Evidence:

- The repository explicitly states that `begin()` assumes an idle,
  command-accepting sensor. It also states that an MCU restart may leave the
  sensor in periodic mode (`SCD41/ASSUMPTIONS.md:21-24`).
- `begin()` issues `get_serial_number` (`SCD41/src/SCD41.cpp:174-188`).
- `get_serial_number` is not legal during periodic measurement
  (`SCD41/docs/reference/scd41-protocol.md:197-207, 260-291`).
- `end()` resets only the local model and does not stop the physical sensor
  (`SCD41/src/SCD41.cpp:280-321`).
- TunnelMonitor has OTA, watchdog, and coordinated software restart paths. A
  software reset does not prove that an independently powered sensor rail was
  cycled.

Impact:

- After an MCU-only reset, an SCD41 that is still measuring periodically can
  reject the first identity command. The library then fails to initialize even
  though the sensor and bus are healthy.
- Repeatedly calling blocking `begin()` is not a safe hotplug or restart policy.

Required refactor:

- Split passive configuration from an explicit initialize/attach job.
- Add one datasheet-backed, bounded convergence path for an unknown physical
  mode. It must account for idle, periodic, low-power periodic, and powered-down
  possibilities without assuming that the MCU and sensor reset together.
- Allow the application to state when a real sensor power cycle is known to
  have occurred. Do not infer it from an MCU boot.
- Expose the final observed/established mode in the job result.

Acceptance criteria:

- HIL and native transport tests cover MCU-only restart while the sensor remains
  in periodic and low-power periodic modes.
- Initialization also handles absent-at-boot followed by hotplug without
  blocking the shared owner.

### TM-SCD-04 - Recovery and offline admission have two owners

Priority: **P0 - architecture blocker**

Evidence:

- `Config` exposes bus reset and sensor power-cycle callbacks plus recovery
  backoff and an offline threshold (`SCD41/include/SCD41/Config.h:50-54,
  75-103`).
- `recover()` owns a probe/reset/probe/power-cycle escalation sequence and
  internal backoff (`SCD41/src/SCD41.cpp:367-439`).
- That escalation is not complete even as a standalone policy. In local
  `POWER_DOWN`, `recover()` returns the wake job before trying later stages. A
  failed `busReset` callback returns immediately instead of trying a configured
  power cycle. In periodic mode, one successful data-ready transaction is
  treated as recovery success even though it does not prove that the sensor is
  still measuring (`SCD41/src/SCD41.cpp:384-431`).
- Tracked failures latch the driver `OFFLINE`, and normal I2C is then blocked
  until library recovery succeeds (`SCD41/src/SCD41.cpp:2140-2187`).
- `offlineThreshold=0` is changed to one during begin; observe-only behavior
  cannot be selected (`SCD41/src/SCD41.cpp:168-171`).
- TunnelMonitor's `I2cTask` already owns bus recovery, retry accounting,
  backoff, backend restart, and device health
  (`TunnelMonitor-node/src/i2c/I2cTask.cpp:3031-3160`).
- The accepted Tunnel dependency pattern requires the chip library to perform
  no recovery or retry (`TunnelMonitor-node/docs/guidelines/dependency_policy.md:145-168`).

Impact:

- The SCD41 driver can refuse a transfer that the I2C owner has admitted after
  the owner has already recovered the bus.
- Library recovery can reset a shared bus because of one device's local state.
- Health counters and retry results can be counted differently by the two
  layers.

Required refactor:

- Keep passive per-device transport/protocol telemetry in the library.
- Leave bus reset, power switching, retry, backoff, and command admission to the
  application owner.
- Replace the current escalation routine with a poll-driven chip-local
  reattach/verify job that the application starts after it has applied any bus
  or power policy.
- If the four-state library health enum is retained, it must not silently become
  a second bus admission authority. Its gating behavior must be explicit.

Acceptance criteria:

- No SCD41 callback resets or reconfigures the bus.
- A Tunnel-style adapter can use single-attempt callbacks only.
- Application recovery followed by a chip-local reattach returns the library to
  service without hidden retry or backoff.

### TM-SCD-05 - The public state machine permits illegal or conflicting work

Priority: **P0 - correctness blocker**

Evidence:

- `_startSingleShot()` checks initialization, local busy state, power-down, and
  variant, but does not reject periodic or low-power periodic mode before
  writing the single-shot command (`SCD41/src/SCD41.cpp:2251-2282`). The command
  is illegal in periodic mode.
- The class exposes both `tick()` and `poll()` as executors plus direct
  synchronous command/read methods. `tick()` does not honor an in-progress
  multi-phase `_pollStep` (`SCD41/src/SCD41.cpp:196-228`). Switching executors
  after a poll command-write phase can issue another command instead of reading
  the pending response.
- Many public command guards check `_pendingCommand` and
  `_measurementRequested`, but not `_settingsReadActive` or every `_pollStep`.
  A caller can start unrelated work while a poll-driven settings transaction is
  between its command and read phases.

Impact:

- The library can send a command prohibited by the sensor data sheet.
- A well-meaning caller can corrupt the protocol sequence by mixing documented
  public APIs.
- TunnelMonitor could avoid this only by duplicating undocumented library state
  in its adapter.

Required refactor:

- Use one normal executor and one central admission check for all jobs.
- Treat any active job phase as busy for unrelated work.
- Reject all illegal mode transitions before transport is touched.
- Deprecate or remove the competing normal executor after one compatibility
  release if source compatibility is required.

Acceptance criteria:

- A state/operation matrix test covers every public job in every sensor mode and
  every active job phase.
- Illegal requests return a local error with zero I2C callbacks.
- Mixing legacy/direct APIs cannot alter an active poll job.

### TM-SCD-06 - Expected wake NACK is not portable to the target adapter

Priority: **P0 - target transport blocker**

Evidence:

- The core accepts only `I2C_NACK_ADDR` or `I2C_NACK_DATA` as the expected
  wake-up result (`SCD41/src/SCD41.cpp:1781-1793`).
- The supplied native ESP-IDF adapter maps timeout, invalid response, generic
  bus error, and invalid argument, but never returns `I2C_NACK_ADDR` or
  `I2C_NACK_DATA`
  (`SCD41/examples/idf/basic/main/IdfI2cTransport.cpp:18-34, 56-99`).
- TunnelMonitor's backend exposes a generic `Nack`, not address/data phase
  detail (`TunnelMonitor-node/include/TunnelMonitor/contracts/FieldBus.h:27-38`;
  `TunnelMonitor-node/src/i2c/IdfI2cBackend.cpp:23-51`).
- TunnelMonitor normally accounts a NACK before returning the transfer result.
  The callback signature does not tell the owner that this specific write is
  expected to NACK. An adapter would have to decode the raw wake command bytes,
  which moves chip knowledge outside the library.

Impact:

- The advertised wake path cannot succeed through the supplied IDF mapping.
- A Tunnel adapter can poison device/bus diagnostics for a normal expected wake
  NACK.

Required refactor:

- Add a truthful generic write-NACK transport result that can be distinguished
  from timeout and bus fault without claiming address/data precision the
  platform does not provide.
- Pass minimal semantic transfer context to the callback, including whether a
  write NACK is expected for this exact phase. This lets the owner use a
  no-accounting single attempt without parsing SCD41 command bytes.
- Keep read-header/no-data NACK separate from expected wake behavior.

Acceptance criteria:

- Native tests use an ESP-IDF-like generic write-NACK adapter.
- Wake completes without incrementing bus/device error counters.
- Timeout and bus-stuck results during wake remain failures.

### TM-SCD-07 - Fixed-point samples cannot be consumed directly

Priority: **P1 - required API fix**

Evidence:

- TunnelMonitor uses fixed integer units in owner results
  (`TunnelMonitor-node/include/TunnelMonitor/contracts/EnvPowerDisplay.h:120-128`).
- `getMeasurement()` consumes the ready sample but returns floats
  (`SCD41/src/SCD41.cpp:541-558`).
- `getCompensatedSample()` returns fixed-point values but is a non-consuming
  peek (`SCD41/src/SCD41.cpp:587-595`).
- `requestMeasurement()` rejects a new request while `_measurementReady` remains
  true (`SCD41/src/SCD41.cpp:442-466`).

Impact:

- A fixed-point caller must perform a dummy float read only to clear readiness.
  That API sequence is surprising and easy to get wrong.

Required refactor:

- Add a consuming fixed-point API, for example `takeSample(FixedSample&)`.
- Use clear `take` versus `peek` names.
- Return sample validity, mode/source, timestamp, and epoch/sequence with the
  fixed-point values in one bounded structure.

### TM-SCD-08 - Settings snapshots can report stale values as valid

Priority: **P1 - diagnostic truth fix**

Evidence:

- Live settings are cached and exposed through one global `liveConfigValid`
  flag (`SCD41/include/SCD41/SCD41.h:109-148`).
- Successful setters write the sensor but do not update the matching cached
  field and do not invalidate `_settingsLiveConfigValid`
  (`SCD41/src/SCD41.cpp:873-1117`).
- Cached fields are refreshed only by the settings-read paths
  (`SCD41/src/SCD41.cpp:1350-1487, 2781-2927`).

Impact:

- After a previous valid settings refresh, a successful setter can leave
  `liveConfigValid=true` while the snapshot still contains the old value.
- Tunnel status, CLI, or web output could present incorrect configuration as
  confirmed sensor state.

Required refactor:

- Prefer poll-driven set-and-readback jobs for settings where verification is
  practical.
- At minimum, invalidate the affected cached field immediately when a mutation
  starts and mark it valid only after confirmed completion/readback.
- Replace the single validity boolean with a small fixed field-validity mask.
- Separate runtime state, health telemetry, and device configuration snapshots
  so each has clear validity rules.

### TM-SCD-09 - Release and physical evidence are not ready

Priority: **P1 - release gate**

Evidence:

- The current retained report records all 28 sensor HIL items as `NOT RUN`
  because no SCD41 was attached
  (`SCD41/docs/reports/hil-validation-COM8-20260629.md:64-95`).
- A historical safe smoke attempt at commit `f3c3e47` uploaded and ran the CLI
  on ESP32-S2 but found devices only at `0x2A` and `0x3C`, not SCD41 address
  `0x62`. It was correctly recorded as `FAIL / BLOCKED`; no sensor measurements
  were captured. That report existed as `docs/SCD41_HIL_SMOKE_REPORT.md` in
  commit `d88e062` and was later removed during documentation cleanup.
- `library.json` and `idf_component.yml` still report `0.1.0`, but current HEAD
  is 22 commits after the `v0.1.0` commit. The unreleased changes include major
  API, polling, ESP-IDF, packaging, and hardening work
  (`SCD41/CHANGELOG.md:8-74`).
- There is no exact TunnelMonitor consumer build or SCD41 owner-adapter test.
- The clean-consumer checker is useful header hygiene, but its consumer is
  compile-only and does not link and run the packaged `src/SCD41.cpp` through a
  real owner adapter (`SCD41/tools/check_clean_consumer_compile.py:95-122`).

Impact:

- Passing native and build tests do not prove the sensor, shared bus, wake NACK,
  restart convergence, or long-run field behavior.
- TunnelMonitor cannot exact-pin a reviewed named release that represents the
  current code.

Required work:

- Complete the P0 refactor first.
- Add target-exact consumer and adapter tests.
- Run real SCD41 HIL on the ESP32-S3 target and shared bus.
- Update version metadata and changelog, then tag a reviewed release. Tunnel
  should pin the full immutable commit, not a branch name.

### TM-SCD-10 - Transport health does not describe logical operation health

Priority: **P1 - diagnostic truth fix**

Evidence:

- A normal typed read is two tracked transport phases: a command write and a
  later response read (`SCD41/src/SCD41.cpp:1870-1882, 1913-1943`).
- Every successful phase resets `_consecutiveFailures`, clears `_lastError`,
  and sets `DriverState::READY` (`SCD41/src/SCD41.cpp:2140-2168`).
- Repeating the pattern "command write succeeds, response read fails" therefore
  never accumulates more than one consecutive failure, even though every
  logical read operation fails.
- `lastError()` is documented as the most recent tracked failure, but the next
  successful transport phase erases its status while `lastErrorMs` remains
  (`SCD41/include/SCD41/SCD41.h:247-250`).

Impact:

- Library health can say `READY` while the application has received no usable
  measurements.
- A status page cannot explain the last failure reliably from the paired error
  status and timestamp.

Required refactor:

- Keep raw transfer counters if useful, but separately publish terminal logical
  job outcomes.
- Preserve the historical last error until an explicit clear or replacement;
  a successful phase should update `lastOkMs`, not erase failure history.
- Do not use library health to gate owner requests. TunnelMonitor should derive
  required/optional product health from terminal job results and freshness.

### TM-SCD-11 - Transfer-attempt timing and float validation need correction

Priority: **P1 - core correctness fix**

Evidence:

- `_writeCommand()`, `_writeCommandWithData()`, and `_readOnly()` update the
  command-spacing timestamp only after a successful transfer, or the special
  mapped not-ready result (`SCD41/src/SCD41.cpp:1819-1905`). A timeout, data
  NACK, or bus error may still occur after bytes were clocked to the sensor.
  The next request can then start without the required idle spacing.
- `setTemperatureOffsetC(float)` and `encodeTemperatureOffsetC(float)` check
  only that the float is finite, convert `offsetC * 1000` to `int32_t`, and
  validate or clamp later (`SCD41/src/SCD41.cpp:854-860, 1603-1619`). A very
  large but finite float can exceed the integer conversion range before the API
  rejects it.

Required refactor:

- Record the end time of every transport attempt that reached the callback,
  including failed attempts. Do not update it for local validation or admission
  failures where no transfer occurred.
- Validate the engineering-unit offset range before integer conversion. Add
  extreme finite input tests under an undefined-behavior sanitizer host build.

### TM-SCD-12 - The clock API has two apparent sources of truth

Priority: **P2 - API clarity and testability**

Evidence:

- `tick(uint32_t nowMs)` and `poll(uint32_t nowMs, ...)` explicitly discard
  `nowMs` and read `Config::nowMs` instead (`SCD41/src/SCD41.cpp:196-244`).
- The public signature implies that the caller-provided value drives the
  operation, but it has no effect.

Required refactor:

- Choose one coherent clock contract in the new job engine. Either accept the
  current time in `poll()` and use it, or use only the configured monotonic
  clock and remove the unused parameter. Keep microsecond command spacing in a
  clearly related monotonic domain.

## Required Library Refactor

The smallest robust architecture is one fixed, single-job state machine. It does
not need a task, queue, registry, polymorphic device framework, or dynamic
allocation.

### 1. Passive binding

Use a zero-I/O call that only validates and copies callbacks/configuration.
Initialization of the physical sensor is a separate typed job.

Conceptual API shape:

```cpp
Status configure(const Config& config);               // zero I/O
Status startJob(const JobRequest& request);            // zero I/O
PollResult poll(const PollTime& now, uint8_t maxCallbacks = 1);
Status takeJobResult(JobResult& result);               // cache only
void detach();                                         // zero I/O
```

Names are illustrative. The important contract is zero-I/O start and one
executor.

### 2. One job model

Use a small `JobKind` enum for concrete current operations:

- initialize/attach;
- start/stop periodic;
- request/read measurement;
- read or set one compensation/configuration value;
- read identity;
- sleep/wake;
- self-test and forced recalibration;
- reinit, persist, and factory reset;
- chip-local verify/reattach after application recovery.

Only one job is active. Every phase has an explicit next due time. Every job has
one immutable deadline and one monotonically changing generation or completion
sequence. This is enough for Tunnel's private adapter to correlate its owner
request without putting Tunnel request IDs into the chip library.

### 3. Exact poll result

A fixed `PollResult` should contain:

- current job kind;
- running or terminal state;
- `Status`;
- exact transport callbacks used by this call;
- next due time when running;
- completion generation when terminal.

This makes scheduling and timing diagnostics factual. It also removes the need
for `lastPollStatus()`, `lastAsyncStatus()`, and several loosely related busy
flags to act as separate result channels.

### 4. Application-owned recovery

Transport callbacks should be one physical attempt. The library reports the
result. The application decides whether and when to retry, reset the shared bus,
or power-cycle the sensor.

After application recovery, it starts one chip-local reattach job. This job may
verify identity and restore the selected operating mode, but it must not invoke
bus reset or hidden retries.

### 5. Truthful fixed-point data

Make fixed-point output the primary embedded result:

```cpp
struct FixedSample {
  uint16_t co2Ppm;
  int32_t temperatureMilliCelsius;
  uint32_t relativeHumidityMilliPercent;
  uint32_t capturedAtMs;
  uint32_t sensorEpoch;
  uint32_t sampleSequence;
  SampleFlags flags;
};
```

Useful `SampleFlags` are `CO2_VALID`, `TEMPERATURE_VALID`, `HUMIDITY_VALID`,
`FRESH`, and `WARMUP_CANDIDATE`. The library should report facts such as sample
mode and samples since attach/mode start. TunnelMonitor remains responsible for
the product policy that decides how many early CO2 samples to discard.

The existing float conversion helpers can remain as convenience APIs, but the
owner adapter should not need floating point to consume a sample.

### 6. Truthful snapshots

Use separate fixed snapshots:

- `RuntimeSnapshot`: physical mode, active job, next due time, sample epoch;
- `HealthSnapshot`: last transport/protocol errors and saturating counters;
- `ConfigurationSnapshot`: values plus per-field validity/readback mask;
- `Identity`: serial and variant.

Do not report a cached value as live after an unverified mutation.

### 7. Narrow maintenance surface

EEPROM persistence, FRC, self-test, reinit, and factory reset should remain typed
jobs, but they should be clearly separated from the normal telemetry surface.
Unsafe raw byte reads are not needed by TunnelMonitor. Keep them diagnostic-only
or move them behind an explicit build option so production application code is
not encouraged to bypass typed state handling.

## Helpful but Non-Blocking Improvements

These are useful after the hard findings are fixed:

- Add `errName()`, `jobKindName()`, `driverStateName()`, and classification
  helpers such as `isTransportFailure()`, `isProtocolFailure()`, and
  `isExpectedPending()`.
- Add `nextDueMs()` or include it in `PollResult` so the owner does not poll a
  long 5-10 second wait at high frequency.
- Remove `Config::i2cAddress`; the driver already rejects anything other than
  fixed address `0x62` (`SCD41/include/SCD41/Config.h:90-103` and
  `SCD41/src/SCD41.cpp:143-145`).
- Remove or implement the currently unused `TransportCapability::TIMEOUT` and
  `BUS_ERROR` capability bits. Only `READ_HEADER_NACK` currently affects core
  behavior.
- Use explicit `peek*` and `take*` names for cached result APIs.
- Add compile-time size checks for public fixed structs.
- After behavior is stable, split the 3000-line implementation only along
  concrete responsibilities: protocol codec, job engine, and passive
  diagnostics. Do not create a generic sensor framework inside this library.

## TunnelMonitor Follow-on Work

This section records integration consequences. These are not changes to make in
TunnelMonitor during this audit, and several are not defects in the SCD41
library.

### Dedicated compile-time leaf

Do not force SCD41 into the current auto-discovered SHT3x/BME280 `ReadEnv` path.
SCD41 is stateful, has CO2 data, and has a fixed 5-second sensor cadence. Use a
dedicated private SCD41 adapter under `I2cTask`, selected by the compile-time
product profile.

The adapter should:

- be called only by the I2C worker;
- translate one library callback to one no-retry backend attempt;
- preserve the original owner deadline;
- map one terminal library job to one exact Tunnel result;
- keep all public Tunnel contracts free of third-party driver types;
- never parse SCD41 command bytes to recover missing library semantics.

### New typed data contract

Current `EnvSensorKind` contains only BME280 and SHT3x, and `EnvReadResult` has
temperature, RH, and pressure but no CO2
(`TunnelMonitor-node/include/TunnelMonitor/contracts/EnvPowerDisplay.h:11-21,
120-128`). Current sample fields also contain no CO2
(`TunnelMonitor-node/include/TunnelMonitor/contracts/Sample.h:41-90,
262-274`).

Platformization will need deliberate append-only or profile-specific additions:

- stable SCD41 device identity;
- typed I2C operation/result for SCD41 sample work;
- fixed-point CO2 ppm result and validity bit;
- a selected sample-schema CO2 field and storage/cloud mapping;
- a decision whether SCD41 temperature/RH map into generic environment fields
  for that product profile.

The current fixed capacities also need an explicit platformization change:

- the known I2C device table is exactly five entries and has no address `0x62`
  (`TunnelMonitor-node/include/TunnelMonitor/i2c/I2cConfig.h:82` and
  `TunnelMonitor-node/src/i2c/I2cDiagnostics.cpp:96-105`);
- `DeviceId` has no SCD41/CO2 entry
  (`TunnelMonitor-node/include/TunnelMonitor/contracts/Health.h:76-97`);
- production device health currently uses all 16 of its 16 fixed entries
  (`TunnelMonitor-node/include/TunnelMonitor/contracts/Capacities.h:81-90`), so
  adding SCD41 requires an intentional capacity increase or inventory change;
- the sample schema uses 37 of 48 numeric slots, so CO2 and optional SCD41
  temperature/RH fit the current numeric array, but they still require explicit
  stable field IDs and downstream mappings
  (`TunnelMonitor-node/include/TunnelMonitor/contracts/Sample.h:41-81` and
  `TunnelMonitor-node/include/TunnelMonitor/contracts/Capacities.h:97-100`).

### Timing policy

The current ENV read deadline is 1000 ms, the general measurement I2C deadline
is 2000 ms, optional-device polling is 5000 ms, and optional stale time is
15000 ms
(`TunnelMonitor-node/include/TunnelMonitor/contracts/EnvPowerDisplay.h:50-54`;
`TunnelMonitor-node/include/TunnelMonitor/measurement/MeasurementScheduler.h:11-16`).

A full SCD41 single shot takes 5000 ms. It cannot be put behind the current
1000/2000 ms deadlines. The simplest field policy is likely continuous SCD41
periodic mode with owner-stepped readiness/sample fetch and a device-specific
deadline. The final choice belongs to the product profile because it affects
power, ASC behavior, sample freshness, and fan-temperature availability.

The active measurement cycle also has exactly four source request slots and
currently uses all four for SHZK, VibWire, ENV, and power
(`TunnelMonitor-node/src/measurement/MeasurementRuntime.cpp:34-72, 340-375` and
`TunnelMonitor-node/include/TunnelMonitor/measurement/MeasurementScheduler.h:16`).
Its tracked foreground I2C result cutoff is 1250 ms
(`TunnelMonitor-node/include/TunnelMonitor/i2c/I2cDiagnostics.h:16`). A new CO2
source therefore needs a deliberate source-capacity change, and a 5-second
single-shot must not be forced through the current foreground cycle contract.

If SCD41 temperature replaces the current ENV temperature source, the existing
5-second fan input and 15-second stale/failsafe behavior must be preserved or
deliberately changed. Do not accidentally make the fan depend on a 15-minute
logging interval.

### Health and maintenance policy

The product profile must decide whether SCD41 is required or optional. Transport
health, repeated CRC/protocol failure, data freshness, warm-up samples, and
physical absence are different facts and should not be collapsed into one
generic NACK state.

Temperature offset, altitude, ambient pressure, ASC, FRC, and EEPROM persistence
need explicit product/operator policy. Normal telemetry and safe diagnostics
must not persist settings, run FRC, or factory reset the sensor.

### Exact dependency pin

After the library refactor and validation, TunnelMonitor should add the reviewed
SCD41 commit as a full immutable Git SHA in both firmware and relevant native
test dependency sets. Do not pin this hardening branch or the stale `v0.1.0`
release.

## Validation Required Before Integration

### Library native tests

- zero-I/O configuration and all zero-I/O job starts;
- exactly one callback per poll with budget one for every job phase;
- absolute deadline before/after every phase;
- cancellation at every phase;
- illegal operation/mode matrix with zero-I2C rejection;
- settings per-field cache invalidation and verified readback;
- consuming fixed-point sample behavior;
- 32-bit clock wraparound for every scheduled delay/deadline;
- generic write-NACK wake behavior;
- attach from idle, periodic, low-power periodic, powered-down, and absent
  states;
- application recovery followed by chip-local reattach;
- no hidden retry, bus reset, or power-cycle callback.

### Tunnel consumer tests

- compile and link the released package under TunnelMonitor's exact pinned
  pioarduino ESP32-S3 platform and C++ flags;
- a private-adapter fake proving one backend attempt per owner poll;
- owner deadline expiry, cancellation, exact request identity, and late-result
  rejection;
- generic NACK accounting for wake versus real absence;
- shared-owner fairness with RTC, FRAM, display, and SCD41 work ready together;
- fixed payload and public-status size ceilings;
- current and alternate product/schema profile builds.

### Physical HIL

- real SCD41 detected at `0x62` on the target ESP32-S3 board;
- shared 400 kHz bus operation with normal RTC/FRAM/display traffic;
- 5-second periodic cadence, readiness, fixed-point values, and stale handling;
- MCU-only reset while the sensor remains powered and periodic;
- sensor power interruption and reconnect;
- expected wake NACK without false bus/device failure;
- CRC/error injection where practical and bounded recovery behavior;
- at least a 30-minute safe soak, followed by the project's longer field soak;
- no EEPROM, FRC, or factory-reset action in safe smoke tests;
- separate confirmed maintenance HIL only when explicitly authorized.

Hardware admission must also verify the sensor rail, I2C voltage compatibility,
the 175-205 mA measurement pulse budget, and local bulk capacitance. These are
board facts, not software-library tests
(`SCD41/docs/reference/scd41-protocol.md:10-40`).

## Validation Performed During This Audit

| Command | Result |
| --- | --- |
| `python -m platformio test -e native` in SCD41 | PASS: 108/108 tests |
| Core timing, CLI, ESP-IDF example, HIL-runner, and generated-version guards | PASS |
| `python -m platformio run -e esp32s3dev` in SCD41 | PASS |
| PlatformIO package creation plus package-content check | PASS |
| Clean consumer compile checker | PASS; header/compile hygiene only, not a linked Tunnel adapter |
| `python -m platformio test -e native` in TunnelMonitor-node | PASS: 1050/1050 tests |
| `python -m platformio run -e tunnelmonitor_wifi` | PASS; RAM 179632 bytes, flash 1796646 bytes |
| Physical SCD41 HIL | **NOT RUN** |
| Destructive/calibration commands | **NOT RUN** |

The passing Tunnel build does not include SCD41 and is only a baseline check.

## Recommended Order

1. Refactor the SCD41 library to passive bind plus one poll-driven job engine.
2. Fix mode admission, deadlines/cancellation, wake NACK semantics, fixed-point
   consumption, and settings cache truth.
3. Add library-native state/phase tests and a target-exact linked consumer test.
4. Prove attach/restart and shared-bus behavior with real SCD41 HIL.
5. Cut and tag a new reviewed SCD41 release.
6. During Tunnel platformization, add the private leaf adapter, typed CO2
   contract, selected schema fields, health policy, and product timing policy.

Do not integrate the current library by raising timeouts, disabling health with
a large threshold, calling `begin()` repeatedly, clearing state with `end()`,
or mirroring the internal state machine in TunnelMonitor. Those approaches hide
the ownership mismatch and make restart failures harder to diagnose.
