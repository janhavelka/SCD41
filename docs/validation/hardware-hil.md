# Hardware and HIL Validation

Hardware validation must use a real board, wired SCD41, and raw
transcript. Parser tests, dry runs, native fake transports, and successful builds
are not hardware evidence.

Firmware entry points:

- Arduino/PlatformIO: `examples/01_basic_bringup_cli`
- native ESP-IDF: `examples/idf/basic`

Both examples expose the same owner-safe command contract. Each loop polls with
a one-callback budget and automatically consumes and prints terminal results.

The reference backends are **Arduino-ESP32 3.1.3** (pioarduino `53.03.13`) and
**native ESP-IDF 6.0.1**. Both preserve a distinct NACK in their source-level
error contracts; physical wake/attach still needs validation on each fixture.
Arduino 3.3.11 / ESP-IDF 5.5.5 instead collapses wake NACK into a generic error.
Do not reinterpret generic errors as NACK or success. See
[backend compatibility](../porting/esp-idf.md#backend-compatibility) before
selecting a different firmware/backend for a run.

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

```powershell
python tools/scd41_hil_runner.py --parser-self-test
python tools/scd41_hil_runner.py --dry-run --port COM8 --output-dir hil-results
python tools/scd41_hil_runner.py `
  --port COM8 --baud 115200 --output-dir hil-results `
  --board "ESP32-S3 DevKitC-1" `
  --firmware-commit "<actually flashed commit; note dirty builds>" `
  --build-command "idf.py -C examples/idf/basic -B build-esp32s3 build" `
  --fixture "SCD41 at 3.3 V; SDA/SCL and pullups recorded"
```

Parser self-test and dry-run do not open serial and do not create hardware
evidence. A live run requires `pyserial` and writes a raw transcript plus JSON
and Markdown summaries. The summary automatically records the repository
branch, commit, clean/dirty state, invocation, operating system, Python, and
PlatformIO version. `hil-results/` is ignored so dry runs and incomplete setup
attempts do not pollute the checkout; retain only reviewed live evidence that a
release gate actually requires.

The host checkout commit is **not** evidence of the firmware currently flashed
to the board. Supply `--firmware-commit` and `--build-command`, and retain the
build output. Unspecified fixture/operator/build fields remain `NOT_RECORDED`;
a serial pass with missing provenance is insufficient for release evidence.
The runner does not flash firmware or drive a fixture's power/fault controls.

The default serial idle timeout is 15 s so `selfcheck` can finish its 10 s
sensor self-test without output. Each step still has its own absolute timeout;
an explicit shorter `--timeout-s` remains authoritative.
The final status step waits for complete health counters and the `last_errors`
line before closing serial. `cancelled=0` is healthy counter telemetry;
nonzero cancellations and a `CANCELLED` operation status remain failures.
Every command waits for complete newline-terminated response records. Admission,
terminal result, timing, and payload must agree; duplicate request/generation
pairs, stale pending output, truncated records, unexpected resets, and nonzero
health failure counters fail the run. The scanner waits for its final count,
and the help check recognizes aliases and verifies the complete command list.

Reads respect both absolute and idle deadlines. Startup and response buffers,
serial writes, and soak sample counts are bounded. The runner stops at the first
failure, preserves the raw transcript, and lists every remaining step as
`not-run`. Ctrl+C and serial/setup errors also produce failed summaries. There
are no automatic retries, reopens, or recovery writes after failure. A failed
run can leave the sensor measuring or a volatile setting changed; inspect its
recorded state before manual recovery. Exit codes are 0 for a completed selected
sequence (or a dry run/parser self-test), 1 for a live run failure, and 2 for
invalid options or an invalid destructive confirmation.

Both CLIs include `detail=<signed decimal>` in terminal result lines. For an
ESP-IDF transport failure this preserves the original `esp_err_t`. If `ATTACH`
fails with `I2C_BUS`, retain that detail, the final phase, and the raw transcript
before considering a mapping change. Do not infer the error code from its
framework-neutral name. A successful IDF attach must be accompanied by the
health record showing expected NACKs separately from transfer failures.

## Safe smoke sequence

The default runner executes this sequence. It does not issue explicit
calibration or EEPROM-writing commands. Existing sensor ASC policy remains in
effect during measurements.

The authoritative list is the `SAFE_STEPS` table in
[`tools/scd41_hil_runner.py`](../../tools/scd41_hil_runner.py); the runner
executes exactly that table, including its settle waits. To reproduce it by
hand, issue these commands in order:

```text
help
version
scan
begin
probe
recover
status
identity
variant
settings
selfcheck
stress 5
stress_mix 2
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
- Periodic and low-power samples are available after the selected 6 s and 31 s
  waits; repeated cadence checks require the soak group.
- Full single shot reports CO2/T/RH valid; RHT-only does not report CO2 valid.
- Reported operation durations include the mandatory stop, single-shot, and
  self-test waits and finish strictly before their deadlines. Owner-call latency
  is a separate instrumented integration gate.
- Wake and attach reconciliation accept documented generic expected NACK phases
  without incrementing a real transfer-failure counter; timeout and bus errors
  still fail.
- Every started operation has one correlated terminal result and the next
  request is not attributed to an older result.
- Final health has no unexplained CRC, transport, operation, or offline event.

Plausibility ranges are a smoke check, not calibration proof. Record the actual
environment before judging a value.

Samples must have the correct mode, sensor epoch, validity/freshness flags,
full-measurement CO2 within 1..40000 ppm, and representable T/RH values. Rejecting
zero CO2 is an ordinary-air smoke-test policy, not a claim that the chip's
encoded output range excludes zero. Each new sample's
timestamp equals its operation completion time; its sequence and timestamp must
advance within a mode/epoch. RHT-only samples must not mark CO2 valid. The cached
`sample` command must reproduce the preceding fresh sample exactly. Equal
concentrations in consecutive samples are valid and do not indicate staleness.
Workflow summaries must include the exact expected operation sequence and every
typed payload, including a zero self-test result and fully verified configuration
reads. Successful callback counts are checked against each operation's actual
phase sequence; a zero-transfer persistence no-op is recorded separately from
an attempted write. The attach duration check conservatively covers wake, stop,
and identity waits; initial power-up timing needs a power-cycle fixture because
`recover` may start after that initial interval has already elapsed.

## Volatile configuration sweep

```powershell
python tools/scd41_hil_runner.py --port COM8 `
  --include-config-writes --final-compensation altitude `
  --board "ESP32-S3 DevKitC-1" --fixture "<record actual fixture>"
```

After the safe sequence, this optional group reads a verified baseline,
exercises temperature offset, altitude, ambient pressure, ASC enable, ASC target,
ASC initial period, and ASC standard period, then verifies each setter's
readback and restores its original value. Alternate values always differ from
the baseline. The temperature-offset comparison allows the sensor's raw-word
quantization. A final full configuration read must match the original values.

Restoring readable values cannot recover the original active pressure
compensation source: the sensor provides no readable selector, and altitude or
pressure writes select that source. The final setter explicitly selects
`--final-compensation altitude` (default) or `pressure`, using its saved value.
The summary records this requested choice. This group performs no measurements
while ASC/compensation settings are temporarily changed, and never persists
them. Driver dirty bits can remain set after restoration because a volatile
write occurred; that is not evidence of EEPROM wear. On any failure the runner
stops without guessing whether further writes or restoration would be safe.

## Required integration fault gates

These gates need fixture control or a bus/sensor setup that can create the
condition. Run them separately from the safe automated sequence.
The JSON summary explicitly leaves these manual gates `not-run`, even when all
selected serial commands pass. Sensor faults are never relabeled as expected
exceptions in the safe/configuration/soak groups.

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

The runner can append a bounded acquisition group after the safe sequence:

```powershell
# At least 31 minutes of periodic sample waits, plus the safe sequence.
python tools/scd41_hil_runner.py --port COM8 --soak-samples 360 --soak-mode periodic
# At least 30 minutes of low-power sample waits.
python tools/scd41_hil_runner.py --port COM8 --soak-samples 60 --soak-mode low-power
# Repeated full single shots.
python tools/scd41_hil_runner.py --port COM8 --soak-samples 360 --soak-mode single
```

`--soak-samples` accepts 0..10000; 0 disables the group. Periodic reads wait 5.2 s,
low-power reads wait 30.5 s, and single shots use their bounded 5 s operation.
The runner verifies every sample and polls health after every 20 samples, stops
periodic measurement at completion, and captures final health. JSON retains
each operation ID, callback count, duration, sample, and inter-sample timestamp
delta. These deltas measure host acquisition cadence; they do not measure the
sensor's internal sampling jitter. Record the actual observed duration when
claiming a soak gate, rather than inferring one from the requested sample count.

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

```powershell
python tools/scd41_hil_runner.py `
  --port COM7 --include-destructive `
  --confirm-destructive "I understand EEPROM and calibration risk"
```

The runner's destructive group covers the persistence request, factory reset,
fresh attach, and reinit. `--skip-safe` still runs help/version/scan/attach before
that group and final health afterward; it cannot be combined with the volatile
configuration or soak options.
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
