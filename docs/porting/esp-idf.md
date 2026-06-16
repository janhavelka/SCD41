# ESP-IDF Porting Guide

This library can be built as a framework-neutral ESP-IDF component while keeping
Arduino examples and glue separate. The core driver must not include Arduino,
ESP-IDF, FreeRTOS, logging, or bus-driver headers.

## Boundaries

- Public API lives under `include/SCD41/`; implementation lives in `src/`.
- The root `CMakeLists.txt` registers the core as an ESP-IDF component.
- `examples/idf/basic` is the native ESP-IDF example and owns bus setup, pins,
  pullups, time hooks, power/reset hooks, and console behavior.
- `examples/common/` is Arduino example glue only. Do not compile it into an
  ESP-IDF target.
- `include/SCD41/Version.h` is generated from `library.json` and tracked for
  clean package consumers.

## Required Application Hooks

`Config` is the portability boundary:

- `i2cWrite`
- `i2cWriteRead`
- `nowMs`
- `nowUs`
- optional `cooperativeYield`
- optional `busReset`
- optional `powerCycle`

Initialized operation requires `nowMs` and `nowUs`. Use one coherent clock
domain for scheduling and completion checks. Public APIs are not ISR-safe, and
driver instances are not internally thread-safe; serialize access externally.

## ESP-IDF I2C Adapter

Use the ESP-IDF v6 I2C master driver:

```cpp
#include <driver/i2c_master.h>
```

The adapter should implement the existing callback shape:

```cpp
Status idfWrite(uint8_t addr,
                const uint8_t* data,
                size_t len,
                uint32_t timeoutMs,
                void* user);

Status idfWriteRead(uint8_t addr,
                    const uint8_t* txData,
                    size_t txLen,
                    uint8_t* rxData,
                    size_t rxLen,
                    uint32_t timeoutMs,
                    void* user);
```

Adapter rules:

- `addr` is the fixed 7-bit SCD41 address `0x62`.
- `idfWrite()` calls `i2c_master_transmit()`.
- `idfWriteRead()` calls `i2c_master_receive()` when `txLen == 0`,
  `i2c_master_transmit()` when `rxLen == 0`, and
  `i2c_master_transmit_receive()` only for explicit combined transactions.
- Many SCD41 reads are split command/wait/read transactions; do not convert
  them into repeated-start transactions.
- Clamp or reject `timeoutMs` before passing it to ESP-IDF's signed transfer
  timeout. Never let overflow become `-1`, because `-1` waits forever.
- `Status::Ok()` means exact transfer: all requested write bytes accepted and
  all requested read bytes filled.
- Return a non-OK status for short writes, short reads, or zero-byte reads when
  bytes were requested. Put the observed byte count in `Status.detail` when
  available.

Recommended status mapping:

| ESP-IDF status | Library status |
| --- | --- |
| `ESP_OK` | `Err::OK` |
| `ESP_ERR_TIMEOUT` | `Err::I2C_TIMEOUT` |
| `ESP_ERR_INVALID_ARG` | `Err::INVALID_PARAM` |
| Precise address NACK | `Err::I2C_NACK_ADDR` |
| Precise data NACK | `Err::I2C_NACK_DATA` |
| Ambiguous invalid response | `Err::I2C_ERROR` with raw detail |

Do not set `TransportCapability::READ_HEADER_NACK` for a normal ESP-IDF
adapter. Set it only if a custom adapter can reliably distinguish read-header
NACK from timeout, arbitration, and bus faults.

Wake-up expected-NACK handling is deliberately strict: only precise write
address/data NACK statuses may be accepted as expected behavior. Generic
`I2C_ERROR` remains a failure so bus faults are not hidden.

## Component Snippets

Core component:

```cmake
idf_component_register(
  SRCS "src/SCD41.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
```

Example component:

```cmake
idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES SCD41 esp_driver_i2c esp_timer
)
```

## Build And Contract Checks

Run repository checks before claiming compatibility:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
```

In a configured ESP-IDF v6.0.1 environment, build both supported targets:

```bash
idf.py -C examples/idf/basic -B build-esp32s3 set-target esp32s3
idf.py -C examples/idf/basic -B build-esp32s3 build
idf.py -C examples/idf/basic -B build-esp32s2 set-target esp32s2
idf.py -C examples/idf/basic -B build-esp32s2 build
```

Do not claim local ESP-IDF validation without the command output.
