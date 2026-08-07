# SCD41 Feature Coverage

This matrix audits the library against the Sensirion SCD4x datasheet v1.7
(April 2025). It records source and host-test coverage only. No row is a claim
of physical, optical, electrical, long-duration, or hardware-in-the-loop
validation; those gates remain in [hardware-hil.md](hardware-hil.md).

## Datasheet command matrix

All command and response words are MSB-first. Returned words are CRC-8 checked
individually, and every payload word written by the driver carries CRC-8.

| Datasheet capability | Command | Core operation/API | Arduino CLI | Native ESP-IDF CLI | Executable/documented evidence |
| --- | --- | --- | --- | --- | --- |
| Start 5 s periodic measurement | `0x21B1` | `START_PERIODIC` | `periodic on` | `periodic on` | mode/admission/stage tests; protocol reference |
| Read CO2/T/RH measurement | `0xEC05` | `FETCH_SAMPLE`, `SINGLE_SHOT`, `SINGLE_SHOT_RHT_ONLY` deferred read | `read`, `single full`, `single rht` | same | CRC/fault/provenance/fixed-point tests |
| Stop periodic measurement | `0x3F86` | `STOP_PERIODIC`; typed idle use is zero-I2C `BUSY`; `ATTACH` retains its intentional unknown-mode stop/NACK reconciliation | `periodic off` | same | periodic/idle admission and attach convergence tests |
| Set temperature offset | `0x241D` | `setTemperatureOffsetMilliC`; full encoded 0..175 C domain | `toffset <mC>` | same | full-domain helper/write tests; CLI warns above recommended 20 C |
| Get temperature offset | `0x2318` | `READ_TEMPERATURE_OFFSET` | `toffset` | same | configuration and conversion tests |
| Set sensor altitude | `0x2427` | `setSensorAltitudeM`, 0..3000 m | `altitude <m>` | same | request-boundary/write/readback tests |
| Get sensor altitude | `0x2322` | `READ_SENSOR_ALTITUDE`; rejects values above 3000 m before cache publication | `altitude` | same | invalid-response/cache regression |
| Set ambient pressure | `0xE000` write form | `setAmbientPressurePa`, 70000..120000 Pa | `pressure <Pa>` | same | wire-shape/range/periodic-access tests |
| Get ambient pressure | `0xE000` read form | `READ_AMBIENT_PRESSURE`; validates encoded 700..1200 hPa | `pressure` | same | lower/upper invalid-response regressions |
| Forced recalibration | `0x362F` | confirmed `forcedRecalibration`, deferred result and `0xFFFF` failure handling | `frc confirm <ppm>` | same | maintenance/result/fault tests; preparation documented |
| Set ASC enabled | `0x2416` | `setAscEnabled` | `asc_enabled <0|1>` | same | request/write/readback tests |
| Get ASC enabled | `0x2313` | `READ_ASC_ENABLED`; accepts only 0 or 1 | `asc_enabled` | same | invalid-response/cache regression |
| Set ASC target | `0x243A` | `setAscTargetPpm`; complete uint16 command-word domain | `asc_target <ppm>` | same | zero/full-scale admission and write tests |
| Get ASC target | `0x233F` | `READ_ASC_TARGET`; complete uint16 response domain | `asc_target` | same | configuration/read tests |
| Start 30 s low-power periodic measurement | `0x21AC` | `START_LOW_POWER_PERIODIC` | `periodic lp` | same | mode/admission/stage tests |
| Read data-ready status | `0xE4B8` | `READ_DATA_READY`, `(word & 0x07FF) != 0` | `dataready` | same | mask, no-data, and stress-workflow tests |
| Persist user settings | `0x3615` | confirmed `persistSettings`; dirty/verified/persistence-indeterminate policy | `persist confirm` | same | no-op, dirty, ambiguity, and EEPROM-policy tests |
| Read serial number | `0x3682` | `READ_IDENTITY`; also used by attach/wake/reset verification | `identity`, `probe`, `attach` | same | three-word CRC/atomic identity tests |
| Run sensor self-test | `0x3639` | `SELF_TEST`, 10 s zero-I2C wait, zero-word pass | `selftest`; aggregate `selfcheck` includes it | same | typed result, timeout, cancellation, workflow tests |
| Factory reset | `0x3632` | confirmed `factoryReset`, settle and identity reconciliation | `factory_reset confirm` | same | ambiguity/cache/identity tests |
| Reinitialize from EEPROM | `0x3646` | `REINIT`, settle and identity reconciliation | `reinit` | same | reset-stage/cache tests |
| Read sensor variant | `0x202F` | `READ_SENSOR_VARIANT`; strict SCD41 family validation | `variant`, `probe` | same | SCD40/SCD41/SCD43/unknown/CRC tests |
| Set ASC initial period | `0x2445` | `setAscInitialPeriodHours`; uint16 multiples of 4, including zero | `asc_initial <h>` | same | request-boundary/write tests |
| Get ASC initial period | `0x2340` | `READ_ASC_INITIAL_PERIOD`; validates multiple of 4 | `asc_initial` | same | invalid-response/cache regression |
| Set ASC standard period | `0x244E` | `setAscStandardPeriodHours`; uint16 multiples of 4, including zero | `asc_standard <h>` | same | request-boundary/write tests |
| Get ASC standard period | `0x234B` | `READ_ASC_STANDARD_PERIOD`; validates multiple of 4 | `asc_standard` | same | invalid-response/cache regression |
| Full single-shot measurement | `0x219D` | `SINGLE_SHOT`, bounded 5000 ms conversion plus CRC sample | `single full` | same | timing/stage/provenance tests |
| RHT-only single shot | `0x2196` | `SINGLE_SHOT_RHT_ONLY`, bounded 50 ms conversion; CO2 flag clear | `single rht` | same | timing/value-flag tests |
| Enter power-down | `0x36E0` | `POWER_DOWN` | `sleep` | same | admission/mode/stage tests |
| Wake from power-down | `0x36F6` | `WAKE_UP`; expected write NACK, settle, serial/variant verification | `wake`, `attach`, `recover` | same | expected-NACK/identity/failure tests |

## Diagnostic and integration coverage

| Capability | Coverage |
| --- | --- |
| Discovery | `scan` is a raw address scan only in examples that own their bus. `probe` is protocol-qualified: it performs `READ_IDENTITY` when attached or the full `ATTACH` reconciliation when state is unknown. |
| Recovery | `attach` / `recover` run the same typed `ATTACH` operation. They reconcile sensor protocol state; they do not reset the I2C controller or power rail. |
| Health | `status` / `drv` / `health` print cache-only runtime state plus separate transfer, expected-NACK, protocol/CRC, operation, and cancellation counters. |
| Bounded stress | `stress [N]` schedules `N` CRC-protected readiness reads, one typed operation at a time. `N` is bounded to 1..1000. |
| Mixed stress | `stress_mix [N]` schedules read-only, mode-safe operation cycles. Idle cycles cover identity, configuration, variant, and readiness; periodic cycles cover readiness, sample fetch, and ambient pressure. A legitimate no-data sample is a warning, not a transport failure. |
| Aggregate self-check | `selfcheck` requires attached idle mode and sequences identity, variant, full configuration, and the 10 s sensor self-test with pass/warn/fail summary. The direct `selftest` command remains available. |
| Raw diagnostics | `command read_words`, `write`, and `write_word` require an explicit `confirm`. Managed command words are rejected, response words remain CRC-checked, and every dispatched raw command requires a later attach/recover. |
| CLI parity | `tools/check_cli_contract.py` proves Arduino handlers exist in `processCommand`; `tools/check_idf_example_contract.py` checks help, parser, handler semantics, workflow dispatch, confirmations, colors/output contract, and native-IDF purity. |
| External owner | Core lifecycle and operations are non-owning and fixed-memory. `start` is zero-I2C, `poll` consumes an explicit callback budget, results are exactly once, callbacks may not re-enter, and callers serialize the instance. |

## Read-only product-integration fit review

The audit inspected TunnelMonitor-node revision
`acae76ffe7b5de100d4102b122d5365d76f7c96d` read-only. Its actual I2C path has
one `I2cTask` owner, a fixed `I2cDeviceBinding` table, one-attempt
`I2cOwnerTransport`, owner-context lifecycle calls, and copied passive results
and status for consumers. SCD41 is not currently present in that binding table,
so this is an architecture-fit review, not an integration or build claim.

| TunnelMonitor owner boundary | SCD41 fit |
| --- | --- |
| zero-I2C binding | `begin(config)` validates and copies the non-owning callback table |
| immutable command identity/deadline | `OperationOptions` plus assigned `OperationId` |
| owner-only start/poll/take/cancel | direct mapping to `start`, `poll(nowMs, 1)`, `takeResult`, and `cancel` |
| one physical attempt per owner slot | one transport callback per poll budget unit; no hidden retry |
| bounded published device status | copy `runtimeSnapshot`, `healthSnapshot`, configuration/identity/sample evidence after owner-context work |
| bus invalidation/recovery | owner cancels/ends, performs controller/rail recovery, then submits a fresh `ATTACH`; the library never resets the bus |

No TunnelMonitor header, type, task, queue, mutex, or product policy is added to
this library. A future product adapter should remain private to that product and
translate fixed owner requests/results at the existing binding boundary.

## Response-domain policy

CRC proves transport integrity, not semantic validity. Before publishing or
marking a setting verified, the driver also checks every response domain that
datasheet v1.7 defines explicitly:

- altitude: 0..3000 m
- ambient-pressure word: 700..1200 (70000..120000 Pa)
- ASC enable: exactly 0 or 1
- ASC initial and standard periods: uint16 multiples of 4 h

Temperature offset uses the complete uint16 formula domain, 0..175 C; 0..20 C
is a recommendation, not a protocol-validity boundary. ASC target and FRC
reference are uint16 ppm command words with no narrower validity range stated
in v1.7. Invalid constrained responses are reported as protocol/operation
failures, clear the affected verified bit, and never overwrite the last usable
cached value.

## Remaining evidence

Native tests and static checks model protocol behavior and ownership contracts.
The following remain unverified without a wired sensor and recorded transcript:
physical address discovery, real CRC/ACK/NACK behavior, optical accuracy,
periodic and low-power cadence, single-shot timing, self-test outcome, FRC/ASC
behavior, EEPROM persistence/endurance, power-down current, hot-plug recovery,
and long-duration stress.
