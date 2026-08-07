# Hardware and HIL Validation

Hardware validation is optional and must use a real board, wired SCD41, and raw
transcript. Parser tests, dry runs, native fake transports, and successful builds
are not hardware evidence.

Supported firmware entry points:

- Arduino/PlatformIO: `examples/01_basic_bringup_cli`
- native ESP-IDF: `examples/idf/basic`

Both examples expose the same owner-safe command contract. Each loop polls with
a one-callback budget and automatically consumes and prints terminal results.

## Evidence record

Record for every run:

- firmware commit and whether its tree was clean
- board and revision
- SCD41 module/fixture identity
- SDA/SCL pins, bus speed, pullups, and other bus devices
- supply voltage, bulk capacitance, and rail-switch arrangement
- framework/toolchain versions and exact build command
- serial port, baud rate, operator, and UTC date/time
- raw transcript and machine-readable summary paths
- which safe, fault, maintenance, and soak gates were actually run

Do not infer an unrun result. Use `not run`, `pass`, `fail`, or `blocked`, with
the observation that supports it.

## Runner

```bash
python tools/scd41_hil_runner.py --parser-self-test
python tools/scd41_hil_runner.py --dry-run --port COM8 --output-dir hil-results
python tools/scd41_hil_runner.py \
  --port COM8 --baud 115200 --output-dir hil-results \
  --board "ESP32-S3 DevKitC-1" \
  --fixture "SCD41 at 3.3 V; SDA/SCL and pullups recorded"
```

Parser self-test and dry-run do not open serial and do not create hardware
evidence. A live run requires `pyserial` and writes a raw transcript plus JSON
and Markdown summaries.

## Safe smoke sequence

The default runner executes this sequence. It does not change calibration or
EEPROM state.

```text
help
version
scan
begin
status
identity
variant
settings
dataready
periodic on
# wait at least 5 s
read
sample
periodic off
status
single full
single rht
periodic lp
# wait at least 30 s
read
periodic off
sleep
wake
identity
status
```

Pass criteria:

- Scan finds address `0x62`.
- `begin` produces a terminal successful `ATTACH` result.
- Identity is valid, serial is nonzero, and both the composite identity and
  standalone dedicated variant read report SCD41 with a CRC-valid raw word.
- Settings read completes with verified fields and no unexplained dirty state.
- `probe` and `recover` produce protocol-qualified SCD41 identity evidence;
  `selfcheck`, five readiness-stress iterations, and two idle mixed-stress cycles
  finish with colored `PASS` summaries and zero failures.
- Periodic and low-power samples arrive at their device cadence.
- Full single shot reports CO2/T/RH valid; RHT-only does not report CO2 valid.
- Stop-periodic includes the 500 ms settle without owner-task blocking.
- Wake and attach reconciliation accept documented generic expected NACK phases
  without incrementing a real transfer-failure counter; timeout and bus errors
  still fail.
- Every started operation has one correlated terminal result and the next
  request is not attributed to an older result.
- Final health has no unexplained CRC, transport, operation, or offline event.

Plausibility ranges are a smoke check, not calibration proof. Record the actual
environment before judging a value.

## Required integration fault gates

These gates need fixture control or a bus/sensor setup that can create the
condition. Run them separately from the safe automated sequence.

| Gate | Procedure | Required observation |
| --- | --- | --- |
| Unknown retained mode | Leave the sensor in periodic mode, restart only the MCU, then issue `begin` | attach reconciles without assuming sensor reset; result identity is new and final mode/evidence is explicit |
| Active-operation MCU restart | Restart MCU during stop, single-shot wait, or maintenance wait while sensor rail remains powered | new firmware does not publish the abandoned request; attach reconciles before normal work |
| Shared-bus load | Run other known devices through the same owner with the configured SCD41 callback budget | no SCD41 poll exceeds its callback budget; other devices continue to receive service |
| Generic expected NACK | Use the normal ESP-IDF adapter, which does not invent address/data NACK precision | wake and attach convergence succeed only for generic NACK in marked wake/stop reconciliation phases; health records expected NACK separately |
| Sensor hot-unplug | Disconnect SCD41 between operation phases | bounded terminal failure/indeterminate result; no unbounded poll or silent success; other bus devices recover by owner policy |
| Sensor hot-replug | Reconnect after a failed operation and submit a new attach ID | old result cannot be republished; new attach restores verified identity/state |
| Rail interruption | Cut the sensor rail after an effectful write attempt and before readback | result remains cancelled, timed out, partial, or indeterminate as evidence permits; cache is not reported verified |
| Deadline boundary | Delay owner polling through the operation deadline | old operation terminates timed out and cannot issue later I2C under that ID |
| Cancellation boundary | Cancel before first transfer, during a zero-I2C wait, and after an acknowledged write | cancellation performs no I2C; effect/reconciliation fields differ conservatively by stage |
| CRC corruption | Fixture or proxy corrupts each response word position | `CRC_MISMATCH`, no partial sample/config publication, protocol telemetry increments |
| Short transfer | Fixture returns fewer bytes than requested | explicit short-transfer failure and no parse of missing data |

For hot-plug or rail interruption, protect the board and sensor against unsafe
connector transients. Use a fixture designed for controlled switching.

## Soak gates

| ID | Purpose | Minimum evidence |
| --- | --- | --- |
| S-01 | 30-minute periodic run | samples near 5 s cadence, bounded callback use, stable counters |
| S-02 | low-power periodic run | samples near 30 s cadence and clean stop settle |
| S-03 | repeated single shots | unique request/result identity and increasing sample sequence |
| S-04 | repeated end/begin/attach | no leaked result, stale identity, or false verified cache |
| S-05 | clock-wrap fixture | correct deadlines and due scheduling across 32-bit wrap |

Record maximum observed owner-call latency and transfer timeout. A successful
sensor-only soak does not prove shared-bus scheduling.

## Maintenance and destructive gates

The default runner refuses these commands. They require explicit operator
approval, known starting settings, suitable gas/reference conditions, and a
transcript:

- `frc confirm <reference_ppm>`
- `persist confirm`
- `factory_reset confirm`

Enable the runner only with the exact confirmation phrase:

```bash
python tools/scd41_hil_runner.py \
  --port COM7 --include-destructive \
  --confirm-destructive "I understand EEPROM and calibration risk"
```

The runner's destructive group covers the persistence request and factory reset.
If no configuration field is dirty, persistence must complete as a zero-write
no-op; that result does not prove an EEPROM write. To validate a real persist,
the operator must record a deliberate setting change, the acknowledged persist
effect, a power cycle, and readback. The runner does not automate forced
recalibration: the required reference gas, stable
concentration, operating-mode history, and stabilization interval cannot be
proved by a serial script. Run `frc confirm <reference_ppm>` manually only after
recording those conditions and the datasheet stabilization procedure.

Maintenance evidence must distinguish:

- command not attempted
- attempted but not acknowledged
- acknowledged and verified
- partial progress
- indeterminate effect after timeout, bus fault, cancellation, or rail loss

Do not automatically repeat an indeterminate EEPROM, calibration, or reset
write. Reattach/read back where device behavior permits, then let the operator
or product policy decide. Record EEPROM write count/cadence and restore project
defaults after factory testing.

## Verdict labels

- `HIL not run`: no connected hardware transcript.
- `Safe smoke passed`: the complete safe sequence passed on a recorded fixture.
- `Fault gates passed`: each named fault has a separate recorded observation.
- `Shared-bus passed`: callback budget and coexistence were measured under load.
- `Soak passed`: the named soak and duration have evidence.
- `Maintenance passed`: operator authority, setup, outcomes, and final restored
  state are recorded.

A release can state only the labels supported by retained evidence.
