# ESP-IDF Porting Guide

The framework-neutral C++17 core can be consumed as a native ESP-IDF component.
It does not use Arduino, `Wire`, FreeRTOS, `esp_timer`, logging, or the ESP-IDF
I2C driver internally. Those dependencies belong to the application adapter and
owner task. Component metadata does not restrict the target MCU; the bundled
examples and CI builds target ESP32-S2 and ESP32-S3.

The complete native example is in `examples/idf/basic`. Its transport requires
ESP-IDF 6 or newer and CI pins `6.0.1`; that example-only requirement does not
apply to consumers supplying their own transport.

## Component use

The package root contains:

```cmake
idf_component_register(
  SRCS "src/SCD41.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
```

The example main component declares its platform dependencies:

```cmake
idf_component_register(
  SRCS "main.cpp" "IdfI2cTransport.cpp"
  INCLUDE_DIRS "." "../../../common"
  REQUIRES SCD41 esp_driver_i2c esp_driver_gpio esp_timer freertos vfs
)
```

The library component must not add those platform dependencies.

Resolve the library under a fixed component name. `EXTRA_COMPONENT_DIRS`
pointing at a checkout root takes the component name from the directory name,
which breaks in any clone or release archive not named exactly `SCD41`. The
bundled example instead ships `components/SCD41/CMakeLists.txt`, which
registers `src/SCD41.cpp` and `include/` under the fixed name the example's
`REQUIRES SCD41` expects.

## Create the application-owned bus

Use the current ESP-IDF master API:

```cpp
#include <driver/i2c_master.h>

i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t device = nullptr;

i2c_master_bus_config_t busConfig{};
busConfig.i2c_port = I2C_NUM_0;
busConfig.sda_io_num = boardSda;
busConfig.scl_io_num = boardScl;
busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
busConfig.glitch_ignore_cnt = 7;
busConfig.flags.enable_internal_pullup = true;
ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &bus));

i2c_device_config_t deviceConfig{};
deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
deviceConfig.device_address = SCD41::cmd::I2C_ADDRESS;
deviceConfig.scl_speed_hz = 400000;
ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &deviceConfig, &device));
```

Pins, frequency, internal/external pullups, bus lifetime, and error recovery are
application decisions. Production hardware normally uses external pullups.

## Unified transfer adapter

The adapter context can remain fixed-size:

```cpp
struct IdfI2cContext {
  i2c_master_dev_handle_t device = nullptr;
  uint8_t address = 0x62;
};
```

The callback receives one requested attempt:

```cpp
SCD41::TransferResult idfI2cTransfer(
    const SCD41::TransferRequest& request,
    void* user);
```

Dispatch rules:

- write and read present: `i2c_master_transmit_receive()`
- write only: `i2c_master_transmit()`
- read only: `i2c_master_receive()`
- no bytes: reject before starting the controller

Many SCD41 reads are command/write, zero-I2C execution wait, then read-only
response. The core emits those as separate callback invocations. The adapter
must not combine calls or add retries.

Clamp a `uint32_t` timeout before passing it to ESP-IDF's signed millisecond
parameter. Never allow overflow to become `-1`, because `-1` is an unbounded
wait.

Return a completion timestamp on every path:

```cpp
uint32_t idfNowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}
```

This must be the same clock domain passed to operation start and poll.

## Error mapping

The ESP-IDF API does not always expose the exact byte or address phase that
NACKed. Preserve that uncertainty.

| ESP-IDF result | Library result | Disposition |
| --- | --- | --- |
| `ESP_OK` | `TransferCode::OK` | `COMPLETE` |
| `ESP_ERR_INVALID_RESPONSE` | `NACK` | `NO_EFFECT` for read-only requests; otherwise `INDETERMINATE` |
| `ESP_ERR_TIMEOUT` | `TIMEOUT` | `INDETERMINATE` after controller start |
| Invalid local context/request | `FAILED` | `NOT_STARTED` |
| Other controller error | `BUS_ERROR` | `INDETERMINATE` |

Set `bytesTransferred` to the full requested byte count only on `ESP_OK` unless
the platform gives a trustworthy partial count. Store the raw `esp_err_t` in
`detail`.

The example targets synchronous ESP-IDF v6.0.1 transfers. Its
[transmit API](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_driver_i2c/include/driver/i2c_master.h)
documents `ESP_ERR_INVALID_RESPONSE` for a NACK, and the
[shared synchronous driver path](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_driver_i2c/i2c_master.c)
uses that value for transmit, receive, and transmit-receive. `ESP_ERR_NOT_FOUND`
is the missing-device result of `i2c_master_probe`, which this adapter does not
call. Generic `ESP_FAIL` and `ESP_ERR_INVALID_STATE` results are not proof of a
NACK and map to `BUS_ERROR`. Verify the error contract before porting the adapter
to another ESP-IDF version; do not broaden expected-NACK handling to generic
controller or queue failures.

The SCD4x does not acknowledge `wake_up` (datasheet section 3.11.4), so genuine
NACKs must reach the core as `NACK` for attach reconciliation to proceed.

Wake-up and attach stop reconciliation do not require fabricated address/data
NACK codes. The driver marks only those transfers with
`TransferIntent::EXPECTED_WRITE_NACK`; a generic `NACK` is accepted only in a
marked phase. Timeout and bus error remain failures.

## Backend compatibility

Framework version support depends on the consuming application's transport,
not on the framework-neutral core. The included adapters use two specific
baselines whose error contracts preserve a genuine NACK:

- Native ESP-IDF `6.0.1`, using the synchronous master API described above.
- Arduino-ESP32 `3.1.3` / ESP-IDF `5.3.2`, pinned by
  [pioarduino `53.03.13`](https://github.com/pioarduino/platform-espressif32/releases/tag/53.03.13).
  Its [legacy I2C HAL](https://github.com/espressif/arduino-esp32/blob/3.1.3/cores/esp32/esp32-hal-i2c.c)
  uses the legacy driver's distinct NACK result, and
  [Wire](https://github.com/espressif/arduino-esp32/blob/3.1.3/libraries/Wire/src/Wire.cpp)
  maps that result to error 2. The example maps it to framework-neutral `NACK`.

ESP-IDF 5.5.5's new master backend does **not** provide that error contract: its
[synchronous transfer implementation](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_i2c/i2c_master.c#L683-L687)
returns `ESP_ERR_INVALID_STATE` for multiple failed transfer conditions. Arduino
3.3.11 forwards that result through its new I2C HAL, and Wire reports generic
error 4. That Wire backend cannot qualify wake-up NACKs reliably. Do not map
error 4 or `INVALID_STATE` to NACK or success: doing so also hides bus faults.
The native example rejects pre-6 ESP-IDF builds instead of silently accepting
that incompatible error contract. Arduino's separate legacy backend is why
the Arduino example uses its selected older baseline.

These are source-level transport distinctions, not physical validation.
Verify wake NACK, an unrelated bus fault, and bounded callback latency on the
actual board before claiming a backend works. A consumer may use another
framework or version with its own adapter that preserves this same contract.

## Owner-task loop

Bind without I2C:

```cpp
IdfI2cContext context{device, SCD41::cmd::I2C_ADDRESS};
SCD41::Config config;
config.transfer = idfI2cTransfer;
config.transferUser = &context;
config.transferTimeoutMs = 20;

SCD41::SCD41 sensor;
ESP_ERROR_CHECK(sensor.begin(config).ok() ? ESP_OK : ESP_FAIL);
```

The accepted transfer-timeout range is 1-1000 ms. The shared-bus owner should
normally choose a much smaller bound based on its queue/service latency.

Submit typed work with an explicit deadline, then advance it only from the bus
owner:

```cpp
const auto request =
    SCD41::OperationRequest::make(SCD41::OperationKind::ATTACH);
const auto limits = SCD41::SCD41::limits(request.kind);
const uint32_t nowMs = idfNowMs();

SCD41::OperationOptions options;
options.requestId = requestId++;
options.nowMs = nowMs;
options.deadlineMs = nowMs + limits.maxWaitMs +
    static_cast<uint32_t>(limits.maxCallbacks) * config.transferTimeoutMs +
    1000U; // explicit owner scheduling margin for this example

SCD41::OperationId id;
SCD41::Status status = sensor.start(request, options, id);
if (!status.inProgress()) {
  // Admission failed; no I2C occurred.
  return;
}

for (;;) {
  const SCD41::PollResult progress = sensor.poll(idfNowMs(), 1);
  if (progress.state == SCD41::OperationState::RESULT_PENDING) {
    SCD41::OperationResult result;
    (void)sensor.takeResult(progress.id, result);
    break;
  }
  // A real owner schedules other bus work and wakes at progress.nextDueMs.
  vTaskDelay(1); // At least one RTOS tick, including a 100 Hz tick configuration.
}
```

The example's delay is application scheduling, not library behavior. A shared
bus owner should use its normal deadline/queue mechanism instead of a dedicated
sensor loop.

## Native CLI boundary

The ESP-IDF example intentionally uses:

- `app_main`
- `driver/i2c_master.h`
- `esp_timer_get_time()`
- FreeRTOS scheduling
- fixed C buffers for command parsing

It must not use `Arduino.h`, `Wire.h`, `TwoWire`, `String`, `Serial`,
Arduino-compatibility facades, or the Arduino CLI implementation. Command parity
is checked by `tools/check_idf_example_contract.py`.

Console input uses `O_NONBLOCK` and consumes at most 128 bytes per owner-loop
iteration, including discarded overlong lines. Partial input cannot block the
operation engine. The loop yields for one FreeRTOS tick; converting 1 ms to
ticks can yield zero at the default 100 Hz tick rate.

## Build checks

Repository checks on Windows:

```powershell
python tools/check_core_timing_guard.py
python tools/check_repository_hygiene.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
.\scripts\pio.cmd test -e native
```

In an ESP-IDF v6.0.1 environment:

```bash
idf.py -C examples/idf/basic -B build-esp32s3 set-target esp32s3
idf.py -C examples/idf/basic -B build-esp32s3 build
idf.py -C examples/idf/basic -B build-esp32s2 set-target esp32s2
idf.py -C examples/idf/basic -B build-esp32s2 build
```

Do not claim a local ESP-IDF or hardware pass without retaining the command
output or hardware transcript.
