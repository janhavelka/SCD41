# SCD41 Safe HIL Smoke Report

## Verdict

Safe HIL smoke result: **FAIL / BLOCKED**.

The ESP32-S2 board accepted the current firmware build and the CLI responded, but the I2C scan did not find an SCD41 at the required address `0x62`. The scan found devices at `0x2A` and `0x3C` only. The SCD41 command sequence was stopped at that point.

No destructive EEPROM, factory reset, or forced recalibration commands were run.

## Operator Metadata

- Date/time: `2026-06-03T08:27:19.663482+02:00`
- Branch: `hardening/scd41-industry-readiness`
- Repository HEAD used for upload: `f3c3e47`
- Firmware version command self-report: commit `unknown`, status `unknown`
- Framework/build environment: PlatformIO, Arduino framework, Espressif 32 platform
- I2C pins from example `BoardConfig.h`: SDA `8`, SCL `9`
- I2C frequency from example `BoardConfig.h`: `400000` Hz
- Pull-ups: not verified from this host session
- Supply voltage: not verified from this host session
- SCD41 board/module: not verified; no device found at `0x62`

## Hardware Observed

| Board | Serial port | Result |
| --- | --- | --- |
| ESP32-S3 target `esp32s3dev` / `esp32-s3-devkitc-1` | `COM19` (`USB VID:PID=303A:1001`) | Upload blocked by host serial access: `PermissionError(13, 'Access is denied.')`. Not tested. |
| ESP32-S2 target `esp32s2dev` / `esp32-s2-saola-1` | `COM8` (`USB VID:PID=303A:0002`) | Upload succeeded. Detected MCU: `ESP32-S2FH4` rev `v1.0`, 4 MB embedded flash, MAC `48:f6:ee:71:86:66`. CLI responded. SCD41 scan failed. |

## Evidence Logs

Raw evidence logs were saved locally under the ignored `hil_logs/` folder and were not committed:

- `hil_logs/scd41_hil_20260603T0820_s3_com19_upload.log`
- `hil_logs/scd41_hil_20260603T0820_s2_com8_upload.log`
- `hil_logs/scd41_hil_20260603T0828_s2_com8_safe_smoke.log`

## Exact Commands Run

Host commands:

```text
python -m platformio run -e esp32s3dev -t upload --upload-port COM19
python -m platformio run -e esp32s2dev -t upload --upload-port COM8
```

Safe serial commands run on `COM8`:

| Command | Result | Notes |
| --- | --- | --- |
| `help` | PASS | CLI help printed. |
| `version` | PASS | Firmware responded; self-reported commit/status were `unknown`. |
| `scan` | FAIL | Found `0x2A` and `0x3C`; did not find SCD41 at `0x62`. |

The remaining requested smoke commands were **not run** because the SCD41 was not present on the configured I2C bus:

```text
begin
drv
serial
variant
dataready
periodic
watch 3
read
fetch
sample
stop
single
single_start
dataready
read
rht
lowpower
watch 2
stop
sleep
wake
serial
recover
drv
periodic
stress 20
watch 10
stop
drv
```

CLI-equivalent mappings planned for the shorthand commands were:

- `stop` -> `periodic off`
- `rht` -> `single rht` / `single_start rht`
- `lowpower` -> `periodic lp`
- `watch N` -> `watch 1`, bounded observation, then `watch 0`

## Measurements

No representative CO2, temperature, or RH values were captured. The sensor was not detected at `0x62`, so measurement-mode tests were not valid.

## Timing And Events

- Sensor timing anomalies: not evaluated; SCD41 communication did not start.
- Reset/disconnect events: S3 `COM19` was unavailable/locked by the host. S2 upload succeeded after retry and the CLI remained responsive.
- Destructive validation: not run.
- Fault-injection validation: not run.
- ESP32-S3 HIL: not run because `COM19` could not be opened.

## Final Verdict

This run does **not** provide passing SCD41 HIL smoke evidence.

The repository is not ready for release candidate tagging based on this HIL attempt. Required next action is to connect or rewire an SCD41 at address `0x62` on the configured bus, free the ESP32-S3 serial port if S3 coverage is required, and rerun the safe smoke plus bounded stress matrix.
