# External I2C Owner Integration

This guide is for systems where one application task owns the I2C controller
and schedules several device drivers. The SCD41 library is a protocol state
machine below that owner. It does not own a bus, task, mutex, queue, deadline,
retry policy, recovery policy, or sensor power rail.

## Ownership boundary

The application owns:

- controller creation, pins, frequency, pullups, and controller teardown
- bus serialization and work-queue capacity
- per-attempt timeout policy
- monotonic clock and task scheduling
- retries as new operations
- aggregate device health, bus reset, and rail recovery
- product measurement, warm-up, calibration, and persistence policy

The driver owns:

- SCD41 command shapes, CRC, timing gates, periodic-mode restrictions, and
  conversions
- one active typed operation and one retained terminal result
- SCD41-local mode, identity, configuration, sample, and diagnostic health
- conservative cache invalidation and hardware-effect reporting

No core method takes a platform lock. Call the instance only from the owner task
or serialize it externally. Public APIs are not ISR-safe.

Transport callbacks are synchronous leaves: they must not call `begin`,
`start`, `poll`, `cancel`, `takeResult`, `end`, or any cache accessor on the
same instance. The owner may copy a completed `OperationResult` into its own
bounded snapshot/queue after `takeResult`; consumer tasks must not call the
driver directly.

## Transfer adapter

Configure exactly one callback:

```cpp
SCD41::TransferResult transfer(const SCD41::TransferRequest& request,
                               void* user);
```

One invocation means one physical attempt. The adapter must not retry. It must:

1. Validate its context and request before starting the controller.
2. Use the fixed 7-bit address in `request.address`.
3. Enforce `request.timeoutMs` as a finite bound.
4. Perform the requested write, read, or combined transfer exactly once.
5. Return exact-transfer success only when all requested bytes completed.
6. Return the best supported effect disposition and byte count.
7. Set `completedMs` from the same 32-bit monotonic clock used by the owner.

Mapping guidance:

| Adapter observation | `TransferCode` | `TransferDisposition` |
| --- | --- | --- |
| Validation failed before bus access | `FAILED` | `NOT_STARTED` |
| Exact complete transfer | `OK` | `COMPLETE` |
| Proven address NACK before payload acceptance | `NACK` | `NO_EFFECT` |
| Generic NACK after an effectful write began | `NACK` | `INDETERMINATE` |
| Timeout or bus fault after controller start | `TIMEOUT` / `BUS_ERROR` | normally `INDETERMINATE` |
| Short transfer | `SHORT_TRANSFER` | normally `INDETERMINATE` |

Do not claim `NO_EFFECT` unless the platform proves it. Do not convert timeout
or bus failure into NACK. The request intent `EXPECTED_WRITE_NACK` is used for
SCD41 wake and attach stop-reconciliation phases; a generic NACK is acceptable
only in those marked contexts, while other failures remain failures.

## Lifecycle

### 1. Bind

`begin(config)` validates and copies the non-owning configuration. It performs
zero I2C and does not create a task or configure a controller.

`transferTimeoutMs` must be `1..1000` and `powerUpDelayMs` must be `30..1000`.
Values are milliseconds. Keep the transfer timeout within the owner's per-slot
latency budget. The driver enforces the fixed 1 ms SCD41 inter-command spacing.

### 2. Attach and reconcile

Start `OperationKind::ATTACH` after power becomes available. Attach applies the
power-up timing gate, reconciles an unknown retained sensor mode, reads the
serial number and dedicated sensor-variant word, CRC-checks both responses, and
requires the SCD41 variant when strict variant checking is enabled. Serial words
are never interpreted as variant evidence. Do not publish samples or verified
settings before attach succeeds.

An MCU restart does not imply a sensor restart. Treat mode as unknown until the
attach result establishes usable driver state. If the application interrupts
the rail or bus during attach, cancel the host operation, restore the hardware,
and start a new attach request.

### 3. Start work

```cpp
SCD41::OperationOptions options;
options.requestId = ownerRequestId; // nonzero and unique while results may exist
options.nowMs = nowMs;
const SCD41::OperationLimits limits = SCD41::SCD41::limits(kind);
options.deadlineMs = nowMs + limits.maxWaitMs +
    static_cast<uint32_t>(limits.maxCallbacks) * transferTimeoutMs +
    ownerSchedulingMarginMs;

SCD41::OperationId id;
Status status = sensor.start(OperationRequest::make(kind), options, id);
```

`start()` performs admission and validation only. It performs zero I2C. The
accepted status is `IN_PROGRESS`; any other status means no operation was
admitted. The
assigned `OperationId` combines the caller request ID with a driver generation,
which prevents a late result from being attributed to a reused request ID.

The deadline is immutable. It uses the same wrapping clock as poll and callback
completion timestamps. Keep every operation horizon below half the 32-bit range.
The driver accepts any such finite deadline; the application owns queueing and
scheduling slack. A practical minimum budget is the published sensor wait plus
`maxCallbacks * transferTimeoutMs`, followed by an explicit owner margin.

### 4. Advance from the owner task

```cpp
const SCD41::PollResult progress = sensor.poll(nowMs, 1);
```

The numeric budget is a hard cap on callback invocations in that call. With a
budget of one, the owner regains control after at most one configured transfer
timeout plus bounded CPU work. A wait gate calls no transport and returns the
next due time. The owner should schedule again at or after `nextDueMs`; frequent
early polling is safe but unnecessary.

The driver does not sleep between the command-write and response-read phases.
Those are separate poll calls when the budget is one.

### 5. Consume the result

When `progress.state == RESULT_PENDING`, consume the matching result:

```cpp
SCD41::OperationResult result;
Status status = sensor.takeResult(progress.id, result);
```

The result is delivered exactly once. A mismatched ID is stale and cannot drain
another request's result. The next operation is admitted only after the retained
result has been consumed.

### 6. Cancel or unbind

`cancel(id, nowMs)` performs no I2C. It prevents later phases and stores a
terminal cancelled result. If bytes were already accepted, the effect field and
reconciliation flag remain conservative; cancellation is not hardware rollback.
After every attempted transfer, and after cancellation or timeout during a
long sensor action, `RuntimeSnapshot::nextSafeCommandMs` is the earliest safe
admission time while `nextSafeCommandValid` is true. The validity flag matters
when the absolute timestamp wraps to zero. `start()` returns zero-I2C `BUSY`
until that time; no new operation silently absorbs an inherited wait.

`end()` also performs no I2C. Active work becomes a retained cancellation
result. The application remains responsible for any desired stop command, bus
shutdown, or rail change before or after unbinding. Because `end()` has no time
argument, that result uses the last owner timestamp accepted by the driver.

## Operation classes and admission

| Class | Operation kinds | Scheduling use |
| --- | --- | --- |
| `STEADY_STATE` | identity, data-ready and individual configuration reads; `FETCH_SAMPLE` | normal owner-task slots |
| `RUNTIME` | `ATTACH`, periodic start/stop, single shots, configuration snapshot/writes, power-down/wake, reinit | startup or bounded multi-step runtime jobs |
| `MAINTENANCE` | self-test, forced recalibration, persist settings, factory reset | explicit maintenance/factory window |
| `DIAGNOSTIC` | CRC-checked diagnostic word read, command write, word write | controlled engineering access |

`SCD41::limits(kind)` is authoritative for each kind. It publishes:

- `maxCallbacks`: total transport attempts in the complete operation
- `maxRetries`: internal retries; currently zero
- `maxWaitMs`: maximum cumulative sensor wait and mandatory inter-transaction
  spacing after the operation is admitted. `start()` returns `BUSY` instead of
  admitting work during a carried safety window from an earlier operation.
- `writesNonvolatile` and `destructive` flags

Rare work is not forced into a steady-state latency. Self-test can wait 10 s,
for example, while each poll still performs no more than the owner budget.

Current per-operation limits are:

| Operation group | Class | Max callbacks | Max wait ms | Completion evidence |
| --- | --- | ---: | ---: | --- |
| `ATTACH` | runtime | 6 | 1533 | configurable power-up bound, retained-mode reconciliation, and CRC-checked serial plus variant identity |
| full identity read | steady | 4 | 3 | CRC-checked serial plus dedicated variant response |
| variant, data-ready, individual setting reads | steady | 2 | 1 | CRC-checked typed response |
| periodic start, low-power start, power-down | runtime | 1 | 1 | acknowledged command and resulting mode evidence |
| stop periodic | runtime | 1 | 500 | acknowledged command plus zero-I2C settle |
| fetch periodic sample | steady | 4 | 3 | ready result and, when ready, complete CRC-checked sample |
| full single shot | runtime | 5 | 5003 | conversion plus complete sample read |
| RHT-only single shot | runtime | 5 | 53 | complete RHT read with CO2 invalid |
| individual setting write | runtime | 5 | 4 | write plus readback verification |
| full configuration read | runtime, idle only | 14 | 13 | up to seven verified fields; partial mask on failure |
| wake-up, reinit | runtime | 5 | 33 | settle plus serial/variant reconciliation read |
| self-test | maintenance | 2 | 10000 | returned self-test word |
| forced recalibration | maintenance | 2 | 400 | nonvolatile calibration history plus returned result word |
| persist settings | maintenance | 1 | 800 | zero-write no-op when clean; otherwise acknowledged write and settle |
| factory reset | maintenance | 5 | 1203 | reset settle plus serial/variant reconciliation read |
| diagnostic word read | diagnostic | 2 | 1 | 1-3 CRC-checked words; state then requires attach |
| diagnostic command/word write | diagnostic | 1 | 1 | acknowledged transfer; state then requires attach |

Every row has `maxRetries == 0`. These values are also available at runtime;
code should use `limits()` rather than duplicating them in a scheduler.

Managed command words are rejected from the diagnostic API. Unknown command
words can still have unknown side effects, so even a diagnostic read command
marks reconciliation required and invalidates managed mode/config evidence.
Consume the diagnostic result, then run `ATTACH` before production work.

Typed `STOP_PERIODIC` is admitted only when the driver's reconciled mode is
periodic or low-power periodic. Asking to stop while already idle is a zero-I2C
`BUSY` precondition result. The internal stop phase of `ATTACH` is deliberately
different: retained hardware mode is unknown, so attach uses the documented
expected-NACK reconciliation sequence before identity verification.

A standalone sensor-variant read does not reread the serial number. Matching
family evidence refreshes the cached variant word; changed or strictly
unsupported family evidence invalidates the composite identity and requires
`ATTACH`. Do not pair a newly observed variant with an older cached serial as a
verified identity.

## Outcome and effect rules

The owner must use both `OperationOutcome` and `EffectState`:

| Outcome | Meaning | Owner action |
| --- | --- | --- |
| `SUCCEEDED` | Completion criteria met | consume typed value; publish only valid fields |
| `NO_DATA` | Valid check found no sample | schedule later; not a transport retry |
| `FAILED` | Known failure without partial completion claim | apply product policy |
| `CANCELLED` | Host stopped future phases | inspect effect and reconciliation |
| `TIMED_OUT` | Immutable operation deadline expired | do not continue the old request |
| `PARTIAL` | Some fields/stages completed | use completed mask only; reconcile remainder |
| `INDETERMINATE` | Hardware effect cannot be proven | do not blindly retry; reconcile or reattach |

Effect states distinguish `NOT_ATTEMPTED`, `ATTEMPTED`, `ACKNOWLEDGED`,
`VERIFIED`, and `UNKNOWN`. A successful bus write is acknowledged, not
necessarily verified device state. Where readback is possible, verified cache
state is published only after it succeeds.

Use `OperationResult::kind` to select the authoritative value member:

| Kind group | Result value |
| --- | --- |
| sample fetch/single shot | `sample` |
| attach, identity, sensor variant, wake, reinit | `identity`; variant-only also uses raw `value` |
| factory reset | `identity` and reconciled `configuration` |
| setting read | scalar member below plus `configuration` |
| setting write, configuration read, persistence | `configuration` |
| data-ready | `dataReady` |
| temperature offset / forced recalibration | `signedValue` |
| altitude, pressure, ASC numbers, self-test | `value` |
| ASC enabled | `boolValue` |
| diagnostic read / deferred maintenance response | `rawWords`, `wordCount` |

## Cache and publication

Cache snapshots perform zero I2C. They are evidence, not commands.

- `configurationSnapshot().verifiedMask` identifies read/verified fields.
- `dirtyMask` identifies EEPROM-persistable fields changed or possibly changed
  through this driver instance that are not known to have been persisted. A
  field can be both verified and dirty. Ambient pressure is a runtime override
  and never appears in `dirtyMask`. Persistence is rejected while a dirty field
  is unverified; reconcile it through typed readback or reapply first.
- `persistenceIndeterminate` prevents an ambiguous EEPROM result from being
  reported as known persisted state.
- `runtimeSnapshot().reconciliationRequired` signals that normal publication or
  effectful commands should wait for reconciliation.
- samples carry `sensorEpoch`, sequence, mode, timestamp, and validity flags.

Use the terminal operation result as the primary publication event. The cached
sample is useful for diagnostics and late readers, but the application should
still check epoch and flags.

A zero-write `PERSIST_SETTINGS` success means only that this driver instance has
no known unpersisted setting change. It does not read or prove EEPROM contents,
especially after a fresh bind.

## Retry and recovery policy

The library never retries a physical attempt. This is intentional:

- a timeout or NACK may have occurred after the sensor accepted an effectful
  command
- a bus-wide reset affects other devices and belongs to the owner
- product retry cadence and health thresholds differ by system

If an operation is known to have no effect, the application may submit a new
request after its own delay. If the effect is unknown, first use a safe typed
readback, `ATTACH`, or an application-controlled rail recovery. Give every new
request a new identity. Never resume polling an old timed-out or cancelled ID.

## Scheduling CLI and diagnostic work

The repository CLIs are bring-up examples whose loop/task owns their example
bus. Their direct `scan` implementation is valid only inside that boundary. In
a product with one I2C-owner task:

1. Parse operator commands outside or inside the owner as product architecture
   requires, but submit only fixed command/request data to the owner queue.
2. Let the owner translate the request to a typed `OperationRequest`, assign
   its ID/deadline, and call `start`.
3. Advance it only from the owner context, normally with `poll(nowMs, 1)`, and
   schedule from `nextDueMs` / `nextSafeCommandMs` while serving other devices.
4. Consume the terminal result in the owner and publish a copy to the requester.
5. Implement a bus scan as another bounded owner request. Never call a raw scan
   or the SCD41 transport from a console/network task in parallel with the
   owner.

The example `stress`, `stress_mix`, and `selfcheck` commands demonstrate this
cooperative shape: they schedule one existing typed operation, wait for its
exact terminal result, then admit the next step. They do not create a task,
hold a bus lock across a sensor wait, or run a blocking operation loop. Raw
diagnostic commands require explicit confirmation and must be followed by an
owner-scheduled `ATTACH`/`recover`; that operation reconciles sensor protocol
state, not the controller, bus, or power rail.

`HealthSnapshot` separates transfer, protocol/CRC, and operation counters. Its
`OFFLINE` state is a diagnostic threshold, not a hidden transport gate and not a
replacement for system health policy. `Config::offlineThreshold == 0` disables
the `OFFLINE` transition; attempted failures still report `DEGRADED`.

## Integration checklist

- [ ] One task owns the instance and bus serialization.
- [ ] Adapter timeout is compatible with the owner's per-slot latency budget.
- [ ] Adapter returns one attempt, exact byte counts, disposition, and clock.
- [ ] Every start uses a nonzero correlation ID and explicit deadline.
- [ ] Owner schedules from `nextDueMs` and normally polls with budget one.
- [ ] Every terminal result is taken once before starting more work.
- [ ] Cancellation and rail interruption lead to result inspection and
      reconciliation, not blind retry.
- [ ] Maintenance commands require explicit product/operator authority.
- [ ] Cache epoch, validity, dirty, and verified fields are checked before
      publication.
- [ ] Bus reset, task watchdog, queue capacity, and aggregate health remain in
      the application.
