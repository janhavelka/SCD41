# Hardware And HIL Validation

Hardware validation is optional because it needs real boards, a wired SCD41, and
operator control for destructive commands. Do not mark a run as passed without
a raw serial transcript or equivalent log.

Supported validation targets:

- Arduino/PlatformIO bring-up CLI in `examples/01_basic_bringup_cli`
- ESP-IDF CLI in `examples/idf/basic`

Both examples expose the same command contract. The examples own I2C pins, bus
setup, timing hooks, reset/power hooks, and console output.

## Evidence Required

Record these fields for every run:

- board type and revision
- SCD41 module/board and wiring
- SDA/SCL pins and pullups
- supply voltage and power switching, if any
- firmware commit and build environment
- serial transcript path
- ambient reference notes, if any
- operator and date/time

Optional runner:

```bash
python tools/scd41_hil_runner.py --parser-self-test
python tools/scd41_hil_runner.py --dry-run --port COM8 --output-dir hil-results
python tools/scd41_hil_runner.py --port COM8 --baud 115200 --output-dir hil-results --board "ESP32-S3 DevKitC-1" --fixture "SCD41 on 3V3, SDA/SCL pullups recorded"
```

The parser self-test and dry run do not open serial and do not produce hardware
evidence. A live runner invocation requires a source checkout, `pyserial`, a
connected board, and a wired SCD41. It runs safe commands by default and writes
raw transcript plus JSON/Markdown summaries. A passing runner result is
evidence only for that connected hardware and environment.

This repository keeps the concrete entry point named
`tools/scd41_hil_runner.py` because the sequence is SCD41-specific, including
SCD41 timing, maintenance, and destructive-command safeguards.

## Safe Smoke Sequence

Safe smoke tests must not write EEPROM, factory-reset calibration, or run forced
recalibration. Start with:

```text
help
version
cfg
scan
begin
drv
serial
variant
dataready
diag
periodic on
status
# wait >= 5 s
dataready
read
sample
raw
comp
stress 10
periodic off
# wait >= 500 ms
status
settings
single full
single_start full
# wait >= 5 s
read
single rht
single_start rht
# wait >= 50 ms
read
periodic lp
# wait >= 30 s
read
periodic off
sleep
# wait >= 1 s
wake
# wait >= 30 ms
serial
recover
drv
```

Safe smoke pass criteria:

- `scan` finds a device at `0x62`.
- `begin` succeeds and `drv` reports `READY`.
- `serial` returns a nonzero serial and SCD41 variant.
- `dataready` and `read` behave at the expected periodic cadence.
- Samples are plausible for the environment.
- `periodic off` settles before idle-only commands are accepted.
- single-shot full and RHT-only flows complete through pending work.
- wake-up does not poison health when precise expected NACK is observed.
- no hidden async failures or unexplained `OFFLINE` transition appears.

Smoke plausibility ranges are not calibration proof:

- CO2: 300 ppm to 5000 ppm for ordinary indoor/outdoor tests
- temperature: -10 C to 60 C
- relative humidity: 0 % to 100 %

## Soak And Fault Coverage

Recommended soak tests:

| ID | Purpose | Minimum Evidence |
| --- | --- | --- |
| T-01 | 5-minute periodic smoke | valid samples near 5 s cadence, no hidden failures |
| T-02 | 30-minute periodic soak | stable health counters and no watchdog/reset events |
| T-03 | low-power periodic soak | samples near 30 s cadence, clean stop settle |
| T-04 | repeated single-shot loop | no stale cached sample across iterations |
| T-05 | reset epoch freshness | old sample becomes stale after reinit/power-cycle |

Fault tests require a fixture, fake transport, switchable sensor rail, or
controlled bus fault:

| Fault | Expected Result |
| --- | --- |
| missing device/address NACK | explicit status, recover after reconnect |
| data NACK | precise status when adapter can prove it |
| bad CRC | `CRC_MISMATCH`, protocol telemetry updates |
| truncated response | non-OK transport status, no partial parse |
| OFFLINE recovery | normal I2C blocked until `recover()` succeeds |

## Destructive Commands

Do not include destructive steps in safe CI/HIL runs. These require explicit
operator approval and a transcript:

- `persist confirm`
- `factory_reset confirm`
- `frc confirm <reference_ppm>`
- ASC setting persistence
- temperature-offset persistence

The optional runner refuses destructive steps unless both flags are provided:

```bash
python tools/scd41_hil_runner.py --port COM7 --include-destructive --confirm-destructive "I understand EEPROM and calibration risk"
```

After destructive tests, restore known project defaults and record the final
settings snapshot.

## Verdict Labels

- `HIL not run`: no connected hardware or no transcript.
- `Safe smoke passed`: safe sequence has transcript evidence on at least one
  board/sensor combination.
- `Soak/fault passed`: timing and recovery evidence exists for defined fixtures.
- `Destructive passed`: explicit operator approval and command confirmations are
  present in the transcript.
