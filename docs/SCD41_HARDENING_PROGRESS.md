# SCD41 Hardening Progress

## Current Chunk

- Prompt: `04_protocol_safety_wake_crc_raw_truncation_no_data`
- Branch: `hardening/scd41-industry-readiness`
- Starting commit: `59cbe93`
- Scope: M-01 wake-up expected-NACK handling, L-01 CRC health policy, M-02 raw helper safety, M-03 truncated-response contract, and M-07 no-data context mapping.
- Explicitly out of scope: variant gating and destructive CLI confirmations, which remain deferred to Prompt 05.

## Baseline State

- `git branch --show-current`: `hardening/scd41-industry-readiness`
- `git status --short`: no output before Prompt 04 edits
- `git rev-parse --short HEAD`: `59cbe93`

## Subagent Reviews

- Protocol correctness: confirmed generic wake-up `I2C_ERROR` was being suppressed with precise NACKs, raw byte reads could bypass CRC validation, and arbitrary raw no-data mapping was too broad.
- Transport contract: recommended documenting that callback OK means exact requested bytes transferred, mapping short reads/writes to non-OK statuses, and keeping normal IDF adapters from advertising read-header NACK capability.
- Health policy: recommended separating protocol/CRC telemetry from I2C health unless the driver intentionally expands `DriverState`; selected separate protocol counters for this chunk.
- Tests: identified focused native coverage for wake-up precise NACK versus timeout/bus/generic errors, CRC mismatch telemetry, unsafe raw reads, CRC-checked raw words, and managed no-data mapping.
- Integration review: recommended making unsafe raw byte access explicit in public API docs and CLI help while preserving source-compatible helpers.

## Findings Addressed In This Chunk

- M-01: `wakeUp()` now suppresses only precise `I2C_NACK_ADDR` and `I2C_NACK_DATA` statuses as expected wake-up behavior. Generic `I2C_ERROR`, `I2C_TIMEOUT`, and `I2C_BUS` remain tracked transport failures.
- M-01: expected wake-up NACK is health-neutral; it does not count as a transport success or failure.
- L-01: CRC mismatches now update protocol telemetry through `totalProtocolFailures()`, `totalCrcFailures()`, `consecutiveProtocolFailures()`, `lastProtocolError()`, and `lastProtocolErrorMs()`.
- L-01: protocol telemetry is documented as separate from I2C health. CRC mismatches do not increment `totalFailures()` or move `DriverState` to `DEGRADED`/`OFFLINE`; a later successful CRC-checked word read resets the consecutive protocol-failure counter.
- M-02: `readCommand()` remains source-compatible but rejects known word-returning SCD41 commands so callers cannot accidentally bypass CRC validation.
- M-02: `readCommandUnsafe()` is the explicit diagnostic opt-in for raw byte dumps without CRC validation or protocol telemetry.
- M-02: `writeCommand()` rejects known word-payload and word-returning commands, and `writeCommandWithData()` rejects known read-only word commands, so valid command words are not sent with the wrong transaction shape.
- M-02: `writeCommand(..., allowExpectedNack=true)` now returns `UNSUPPORTED`; expected-NACK handling is managed only by `wakeUp()`.
- M-03: `Config.h`, README, and port docs now state that transport `Status::Ok()` means exact requested transfer completion, with short writes/reads reported as non-OK and actual byte counts placed in `Status::detail` when available.
- M-03: the bundled Arduino example transport was verified to enforce write counts, zero-byte reads, and short reads; the README quick-start transport now shows the same checks. The IDF adapter preserves exact-transfer semantics for its callback paths.
- M-07: raw `allowNoData` compatibility no longer maps arbitrary read-header NACK to `MEASUREMENT_NOT_READY`; managed measurement paths keep that mapping only where the SCD41 protocol permits not-ready behavior.
- Examples: Arduino and ESP-IDF CLI help label `command read` as an unsafe raw byte read, and both CLIs call `readCommandUnsafe()` for that command.
- Docs: README, Doxygen, ASSUMPTIONS, and ESP-IDF port notes document wake-up NACK policy, CRC telemetry, unsafe raw helpers, no-data context, and transport exact-transfer requirements.

## Files Changed

- `README.md`
- `ASSUMPTIONS.md`
- `docs/IDF_PORT.md`
- `docs/IDF_PORT_IMPLEMENTATION.md`
- `docs/SCD41_HARDENING_PROGRESS.md`
- `examples/01_basic_bringup_cli/main.cpp`
- `examples/idf/basic/main/IdfI2cTransport.cpp`
- `examples/idf/basic/main/main.cpp`
- `include/SCD41/Config.h`
- `include/SCD41/SCD41.h`
- `include/SCD41/Status.h`
- `src/SCD41.cpp`
- `test/test_basic.cpp`

## Checks Run

- `python scripts/generate_version.py check`
  - Result: passed; `include\SCD41\Version.h` is up to date.
- `python tools/check_core_timing_guard.py`
  - Result: passed; `Core timing guard PASSED`.
- `python tools/check_cli_contract.py`
  - Result: passed; `CLI contract PASSED`.
- `python tools/check_idf_example_contract.py`
  - Result: passed; `IDF example contract PASSED`.
- `python -m platformio test -e native`
  - Result: passed; 69 test cases, 69 succeeded in `00:00:00.529`.
- `python -m platformio run -e esp32s3dev`
  - Result: passed; `esp32s3dev` success in `00:00:03.793`.
- `python -m platformio run -e esp32s2dev`
  - Result: passed; `esp32s2dev` success in `00:00:03.378`.

## Checks Not Run

- Native ESP-IDF `idf.py` builds: not in this prompt's validation list and still require a configured ESP-IDF environment.
- Hardware/HIL validation: explicitly out of scope for this chunk and no hardware evidence was collected.

## Previously Completed Chunks

- Prompt: `03_surface_async_tick_completion_errors`
  - Commit: `59cbe93`
  - Addressed observable asynchronous `tick()` completion failures and async status telemetry.
- Prompt: `02_fix_timing_hooks_and_clock_model`
  - Commit: `dcff765`
  - Addressed required `nowMs`/`nowUs` hooks, Arduino timing defaults, one-clock scheduling, and divergent-clock tests.
- Prompt: `01_branch_agents_baseline_and_low_risk_contracts`
  - Commit: `e2f7776`
  - Addressed copy/move deletion, public thread/ISR/reentrant callback contracts, and project discipline in `AGENTS.md`.

## Remaining Findings For Later Prompts

- Variant gating, temperature-offset scale review, destructive CLI confirmations, and opt-in HIL command handling.
- Latency/state-machine cleanup, stale sample handling, and probe side effects.
- Tests, CI/package validation, ESP-IDF build automation, and example parity beyond this protocol-safety pass.
- Hardware/HIL matrix, final release gate, and evidence-backed validation records.
- Refresh or supersede resolved findings in `docs/SCD41_INDUSTRY_READINESS_EXPLORATION_REPORT.md` after the full hardening sequence is complete.
