# Native Test Layout

`test_basic.cpp` is a native Unity regression suite for the framework-neutral
SCD41 core plus the Arduino example transport shim.

The file is intentionally organized by runner functions:

- `runConfigStatusLifecycleTests`
- `runMeasurementAndTimingTests`
- `runVariantAndSettingsTests`
- `runCompensationAndSettingsTests`
- `runRawProtocolHealthTests`
- `runMaintenanceTests`
- `runExampleTransportTests`

Prefer public API assertions for new tests. The suite keeps a narrow white-box
include of `SCD41.h` because protocol CRC helpers, raw command spacing,
probe-side-effect restoration, and health wrapper behavior need exact internal
state assertions that are not part of the production API.
