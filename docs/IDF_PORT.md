# SCD41 ESP-IDF v6.0.1 Port Status

Last updated: 2026-05-19

Scope: implemented core portability changes plus an ESP-IDF component/example. Arduino/PlatformIO support remains intact.

Official ESP-IDF references:
- I2C master driver: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html
- Build system and components: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
- ESP-IDF v6 peripheral migration notes: https://docs.espressif.com/projects/esp-idf/en/release-v6.0/esp32c3/migration-guides/release-6.x/6.0/peripherals.html

## Current State

- Public API is in `include/SCD41/`; implementation is in `src/SCD41.cpp`.
- `library.json` is package metadata for Arduino and ESP-IDF consumers; `platformio.ini` remains the Arduino/native-test project file.
- Root `CMakeLists.txt` registers the core as an ESP-IDF component.
- The driver already uses injected I2C callbacks from `Config`; library code does not call `Wire` directly.
- The SCD41 address is fixed at `0x62`.
- The driver supports periodic measurement, low-power periodic measurement, single-shot commands, RHT-only single shot, stop periodic, power down, wake up, serial number, identity, compensation, ASC, persist settings, reinit, factory reset, self test, forced recalibration, raw command helpers, and health tracking.
- Long-running operations are state-machine based and are advanced by `tick(uint32_t nowMs)`.
- The driver validates Sensirion CRC-8 on command words and measurement words.
- Serial number variant checking currently expects variant bits `[15:12] == 0x1` unless disabled in `Config`.

## Arduino Dependencies

- `src/SCD41.cpp` no longer includes `<Arduino.h>`.
- `src/PlatformTime.h` is framework-neutral and intentionally inert unless the application supplies timing/yield callbacks through `Config`.
- `include/SCD41/Config.h` exposes framework-neutral callbacks:
  - `i2cWrite`
  - `i2cWriteRead`
  - `nowMs`
  - `nowUs`
  - `cooperativeYield`
  - optional `busReset`
  - optional `powerCycle`
- `examples/01_basic_bringup_cli/main.cpp` and all `examples/common/*.h`
  helpers, including CLI/compat/diagnostic helpers, are Arduino example glue.
  They use `Wire`, `Serial`, `String`, GPIO helpers, `delay()`, and `yield()`.
- `platformio.ini` builds Arduino examples and native tests. It is not an IDF project file.
- `library.json` declares Arduino and ESP-IDF framework compatibility and remains the PlatformIO package manifest.
- `include/SCD41/Version.h` is generated from `library.json`; do not edit it by hand.

## Portability Status

Implemented:

1. The core driver compiles without Arduino or ESP-IDF framework headers.
2. ESP-IDF timing/yield behavior is injected by the example through `Config::nowMs`, `Config::nowUs`, and `Config::cooperativeYield`.
3. Root `CMakeLists.txt` provides `idf_component_register`.
4. `examples/idf/basic` provides an ESP-IDF v6 `i2c_master` adapter and a native CLI matching the Arduino bring-up CLI command contract.
5. Arduino examples remain separate and are not part of the IDF component target.
6. The IDF example maps `esp_err_t` values to library `Status` codes and advertises only the timeout capability.

Still application-owned:

1. I2C bus/device creation and destruction.
2. SDA/SCL pins, pullups, clock speed, power rail/reset controls, and timeout policy.
3. Hardware smoke testing on target boards.

## Exact Files and APIs to Change

- `src/SCD41.cpp`
  - Keep framework headers out of the core implementation.
  - Keep long operations deadline-driven through existing state-machine paths.
  - Keep command spacing, CRC validation, and health tracking in the core driver.
- `include/SCD41/Config.h`
  - No API break is required for the IDF port.
  - Keep transport, timing, yield, bus-reset, and power-cycle callbacks as the portability boundary.
  - Under IDF, examples should always supply `nowMs`, `nowUs`, and `cooperativeYield` so timing is explicit.
- Private shim `src/PlatformTime.h`
  - Do not include Arduino or ESP-IDF headers.
  - Keep the fallback inert; examples/applications must inject real timing through `Config`.
- ESP-IDF example files under `examples/idf/basic/main/`
  - Own the I2C bus, device handle, optional regulator/reset GPIOs, and task timing.
  - Fill `Config` with IDF adapter callbacks.
  - Call `tick()` frequently enough for pending long operations.
  - Provide the native CLI command set, help text, prompts, status/health output,
    diagnostics, maintenance workflows, and raw command access in parity with the
    Arduino example.
- CMake files
  - Root component `CMakeLists.txt`.
  - Example project files under `examples/idf/basic`.

## Dual Arduino and ESP-IDF Architecture

- Keep the SCD41 core as a framework-neutral C++17 component.
- Keep Arduino-only adapters in `examples/common/`; they are not part of the library.
- Keep IDF-only adapters under an IDF example or an optional `extras/idf/` style directory.
- The library must never own the ESP-IDF I2C bus, pins, pullups, clock speed, power rail, or reset GPIO.
- Optional `busReset` and `powerCycle` remain application callbacks. The IDF implementation may use `driver/gpio.h`, a regulator driver, or board-specific code outside the library.
- The application owns bus lifetime:
  - create an `i2c_master_bus_handle_t`;
  - add an `i2c_master_dev_handle_t` for `0x62`;
  - pass an adapter context through `Config.i2cUser`;
  - destroy handles after `driver.end()` and after all driver calls stop.
- Do not call public driver APIs from an ISR. If a future GPIO alert or power event is used, notify a task and call the driver there.

## IDF Transport Adapter Contract

Use ESP-IDF v6.0.1's new I2C master driver only:

```cpp
#include <driver/i2c_master.h>
```

The adapter should provide the existing callback shape:

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

Expected behavior:

- `addr` is a 7-bit address and should be `0x62`.
- `idfWrite()` calls `i2c_master_transmit()`.
- `idfWriteRead()` calls:
  - `i2c_master_receive()` when `txLen == 0`;
  - `i2c_master_transmit()` when `rxLen == 0`;
  - `i2c_master_transmit_receive()` only for explicit combined transactions.
- Many SCD41 reads are intentionally split: write command first, wait, then read with `txLen == 0`. Do not collapse these into a repeated-start transaction unless the driver API requested it.
- `timeoutMs` is passed as the ESP-IDF transfer timeout in milliseconds.
- Clamp or reject `timeoutMs` before passing it to ESP-IDF's signed
  `xfer_timeout_ms`; never allow overflow to become `-1` because `-1` waits
  forever.
- Map `ESP_OK` to `Err::OK`.
- Map `ESP_ERR_TIMEOUT` to `Err::I2C_TIMEOUT`.
- Map `ESP_ERR_INVALID_ARG` to `Err::INVALID_PARAM`.
- Map `ESP_ERR_INVALID_RESPONSE` to an I2C NACK-related status. A normal
  `i2c_master_receive()` / `i2c_master_transmit_receive()` adapter cannot prove
  address-vs-data phase, so use `Err::I2C_ERROR` with the raw `esp_err_t` in
  `Status.detail` unless a custom adapter proves the phase.
- Map address/data NACKs to `Err::I2C_NACK_ADDR` or `Err::I2C_NACK_DATA` only when the adapter can distinguish them. Otherwise use `Err::I2C_ERROR` and place the raw `esp_err_t` value in `Status.detail`.
- Do not register `i2c_master_register_event_callbacks()` on the handle used by
  these callbacks unless the adapter waits for transfer completion before
  returning. The driver expects callbacks to be complete and blocking.
- Do not set `TransportCapability::READ_HEADER_NACK` for a normal IDF adapter.
  Set it only if a custom adapter can reliably distinguish a read-header NACK
  from timeout, arbitration, and bus faults; otherwise expected no-data paths can
  mask real transport failures.
- Wake-up can produce an expected NACK on some flows. Preserve the driver's existing wake-up handling instead of treating every wake-up NACK as a fatal adapter setup error.

## CMake and Component Plan

Minimal core component:

```cmake
idf_component_register(
  SRCS "src/SCD41.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
```

If an IDF adapter is built into an example component, that example should declare:

```cmake
idf_component_register(
  SRCS "main.cpp" "IdfI2cTransport.cpp"
  INCLUDE_DIRS "."
  REQUIRES SCD41 esp_driver_i2c esp_timer
)
```

Do not compile `examples/common/*.h` into ESP-IDF targets.

## IDF and Arduino Example Plan

Arduino examples:

- Keep existing examples using `examples/common/BoardConfig.h` and `I2cTransport.h`.
- Do not replace Arduino `Wire` examples with IDF code.

ESP-IDF examples:

- Keep `examples/idf/basic` as a normal ESP-IDF `main` component.
- Configure SDA, SCL, pullups, and bus frequency in the example only.
- Use `i2c_new_master_bus()`, `i2c_master_bus_add_device()`, and the callback adapter.
- Provide `nowMs`, `nowUs`, and `cooperativeYield` callbacks.
- Build an `SCD41::Config`, set callbacks and timeout, and call `begin(config)`.
- Run the same user-visible CLI contract as the Arduino example: command
  names/aliases, help sections, arguments/defaults/ranges, status/colors,
  diagnostics, health/error reporting, probe/recover/reset workflows,
  self-test/stress/demo flows, and raw command access.
- Advance pending single-shot, periodic, and maintenance operations through
  `tick()` from the example loop.
- Print results from the example, not from the library.

## Test and Validation Plan

- Compile the existing Arduino PlatformIO environments as regression checks
  only. Arduino-ESP32 builds do not prove pure ESP-IDF v6.0.1 compatibility.
- Compile native tests to preserve framework-neutral behavior.
- Run repository contract checks, including `python tools/check_idf_example_contract.py`, to keep Arduino and ESP-IDF CLI behavior aligned.
- Add an ESP-IDF example build for ESP32-S2 and ESP32-S3.
- Hardware smoke test probe and serial-number read at fixed address `0x62`.
- Verify CRC failure injection returns the expected status and does not update measurement state.
- Verify command spacing, 30 ms power-up delay, and long command deadlines.
- Verify `stopPeriodicMeasurement()` honors the 500 ms completion time before new commands.
- Verify wake-up behavior, including expected NACK handling.
- Verify periodic and low-power periodic measurement paths.
- Verify single-shot CO2/RHT and RHT-only paths.
- Verify persist settings and factory reset are explicitly invoked and not triggered by examples accidentally.
- Verify health transitions on injected timeout/NACK and recovery on later success.

## ESP-IDF v6.0.1 Hazards

- Do not include `<driver/i2c.h>` or use legacy APIs such as `i2c_param_config()`, `i2c_driver_install()`, `i2c_cmd_link_create()`, or command-link transactions.
- Use `<driver/i2c_master.h>` and the `esp_driver_i2c` component dependency.
- IDF builds commonly surface warnings that Arduino builds hide. Keep casts explicit for enum, integer-width, `size_t`, and signed/unsigned conversions.
- Avoid global constructors that create ESP-IDF bus handles before the scheduler/runtime is initialized.
- `esp_timer_get_time()` returns microseconds as `int64_t`; down-convert deliberately for existing wrap-safe `uint32_t` logic.
- Long SCD41 operations must stay deadline-driven. Do not add blocking `vTaskDelay()` calls inside the library core.
- EEPROM-affecting commands such as persist settings have wear implications; keep them explicit in examples.
- If the adapter cannot identify read-header NACKs, do not advertise that capability.

## Ordered Checklist

1. Add a framework-neutral timing/yield shim and remove direct framework headers from the core. Done.
2. Add a core component `CMakeLists.txt` for the library. Done.
3. Add an IDF I2C adapter using `<driver/i2c_master.h>` outside the core driver. Done.
4. Add optional IDF bus-reset and power-cycle callback examples outside the library. Documented as application-owned.
5. Add `examples/idf/basic` with bus setup, adapter callbacks, timing callbacks, and a native CLI matching the Arduino bring-up CLI. Done.
   Include top-level and `main` CMake files, component path wiring, and
   `extern "C" void app_main(void)`. Done.
6. Build with ESP-IDF v6.0.1 for ESP32-S2 and ESP32-S3. Pending local ESP-IDF environment.
7. Run Arduino and native builds to confirm existing users are unaffected. Done for native, ESP32-S3 Arduino, and ESP32-S2 Arduino via PlatformIO.
8. Run hardware tests for probe, CRC, periodic measurement, single-shot, wake-up, stop-periodic, maintenance commands, and fault injection. Pending hardware.
9. Update README and changelog for the implemented port. Done.
