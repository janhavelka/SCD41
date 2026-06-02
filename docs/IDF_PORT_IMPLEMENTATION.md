# SCD41 ESP-IDF Port Implementation

Last updated: 2026-06-02

## Implemented

- The library core no longer includes `<Arduino.h>` from `src/SCD41.cpp`.
- `src/PlatformTime.h` is framework-neutral and does not include Arduino or ESP-IDF headers.
- Root `CMakeLists.txt` registers the core as an ESP-IDF component with C++17 enabled.
- `examples/idf/basic` provides:
  - an ESP-IDF project CMake file;
  - a `main` component;
  - an `i2c_master` transport adapter;
  - explicit bus/device ownership in the example;
  - `Config::nowMs`, `nowUs`, `cooperativeYield`, `i2cWrite`, and `i2cWriteRead` wiring;
  - a native CLI matching the Arduino bring-up CLI command names, aliases,
    help sections, prompts, colorized status/health output, diagnostics,
    compensation controls, maintenance workflows, stress/self-test/demo flows, and
    raw command access;
  - fixed-size command input storage with a 127-character command limit and
    clean rejection of overlong commands;
  - a non-blocking example loop that advances pending driver work with `tick()`.
- `tools/check_idf_example_contract.py` compares the Arduino and ESP-IDF CLI
  help/command/raw-access surface, verifies the IDF example uses the
  `i2c_master` API, rejects Arduino compatibility facades, and rejects
  heap-backed parser regressions such as `<string>`, `std::string`, `new`, and
  C allocation calls.

## Core Boundary

The core driver still owns no bus resources. Applications must create and
destroy the ESP-IDF I2C bus/device handles and pass a transport context through
`Config::i2cUser`.

The IDF example maps `esp_err_t` values to library `Status` codes:

- `ESP_OK` -> `Err::OK`
- `ESP_ERR_TIMEOUT` -> `Err::I2C_TIMEOUT`
- `ESP_ERR_INVALID_ARG` -> `Err::INVALID_PARAM`
- `ESP_ERR_INVALID_RESPONSE` -> `Err::I2C_ERROR`
- other ESP-IDF failures -> `Err::I2C_BUS`

The example advertises only `TransportCapability::TIMEOUT`; it does not
advertise `READ_HEADER_NACK` because the standard IDF APIs do not prove the NACK
phase. Wake-up command NACK behavior remains handled by the driver only when the
transport reports a precise address/data NACK; generic `ESP_ERR_INVALID_RESPONSE`
continues to map to `Err::I2C_ERROR`.

## Validation

Run these checks from the repository root:

```bash
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack . -o dist
python tools/check_package_contents.py dist/*.tar.gz
```

Run these checks in an ESP-IDF v6.0.1 environment:

```bash
idf.py -C examples/idf/basic -B build-esp32s3 set-target esp32s3
idf.py -C examples/idf/basic -B build-esp32s3 build
idf.py -C examples/idf/basic -B build-esp32s2 set-target esp32s2
idf.py -C examples/idf/basic -B build-esp32s2 build
```

## Remaining Hardware Work

- Smoke test the fixed `0x62` address on real ESP32-S2/S3 boards.
- Verify serial-number variant detection and periodic samples against expected environmental ranges.
- Verify single-shot CO2/RHT, RHT-only, wake-up, stop-periodic, and maintenance command completion.
- Verify recovery behavior with injected I2C timeout and NACK failures.
