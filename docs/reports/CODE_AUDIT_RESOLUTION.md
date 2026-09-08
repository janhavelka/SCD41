# Code audit verification and resolution

## Revalidation on 2026-09-08

Starting revision: `d491c8ed1a15c597a95a7f9fe163a7ae16082731` on `main`.
The checkout was clean. Fetching all remotes confirmed `origin/main` was the
newest intended branch, and the fast-forward sync was already current.

The requested [docs/CODE_AUDIT.md input at that revision](https://github.com/janhavelka/SCD41/blob/d491c8ed1a15c597a95a7f9fe163a7ae16082731/docs/CODE_AUDIT.md)
reintroduced the August working audit after the September 5 fixes below.
Its 53-test baseline and open-proposal status were therefore stale: the actual
starting tree passed 67 host tests. Three independent reviews covered core
findings, protocol/test coverage, and adapters/tooling; the primary review
checked their evidence, the remaining claims, integration documentation, and
final changes. Every published finding was rechecked, including claims marked
already fixed and rejected. The unpublished candidate findings cannot be
reconstructed from aggregate counts.

### Current finding dispositions

The detailed per-item tables in the earlier review below remain applicable,
with these current conclusions and additions:

| Audit section | Current verification and decision |
| --- | --- |
| What is already correct | All 30 command definitions, CRC framing/vector, exact temperature/humidity conversions, offset and pressure encodings, and operation bounds remain correct against the bundled v1.7 datasheet. Periodic-start 1 ms spacing is driver policy, not a datasheet command-duration entry. |
| Fixed item 1: IDF attach | Refuted for pinned IDF v6.0.1. Rechecked its official synchronous driver and public header: only `ESP_ERR_INVALID_RESPONSE` proves NACK. The current adapter correctly leaves generic errors as bus failures; the audit's broad mapping must not be restored. |
| Fixed item 2: variant gate | Correct and retained. Tests cover SCD40 family-wide low-power/ASC target access and SCD41/SCD43 section 3.11 admission. This does not broaden the supported product claim. |
| Fixed item 3: HIL matching | Correct normalization retained. Added a durable test of actual `run_step()` with mixed-case ANSI-colored output split across chunks, including a split escape/token. It also checks raw transcript preservation and failure-token rejection. |
| Fixed item 4: package paths | Existing top-level and prefixed-directory fixes are correct. Added archive-level regressions for 72 forbidden-path variants, allowed lookalikes, root prefixes, and the required fixed-name IDF wrapper. |
| Fixed item 5: other changes | Rechecked generation zero-skip/admission-only advancement, conservative Wire write/read errors, scanner timeout restoration, zero diagnostic command rejection, deleted unused stubs, native warning flags, raw-source checks, timing zero-tolerance policy, metadata regeneration before CI diff, derived limits, removed dead helper, and fixed component name. All remain valid. The earlier documentation corrections also hold. |
| P1: phase names | Already resolved with existing response phases and aligned wake identity labels. Retain this smaller solution; no new public enum or routing state is needed. |
| P2: read cancellation | Already resolved by `effectfulWriteAttempted`. Preserve attachment and periodic mode for managed reads; retain conservative mutation/diagnostic handling. |
| P3: completion and owner time | Already resolved: callback completion governs scheduling in the same clock domain without replacing owner observations. Cancellation clamps backward owner time. Retained. |
| P4: long idle | Already resolved by observing idle/result-pending polls and expiring old safety gates. The less-than-half-range observation contract remains documented. |
| P5: deadline policy | Already centralized in `_finishOperationFailure()`. Full sensor settling is retained in the transfer executor even when the callback crosses its deadline. Retained. |
| P6: seven coverage gaps | Existing tests cover final phases, non-strict variants, literal sensor waits, diagnostic payload/CRC, zero offline threshold and RHT-only validity. Legitimate paths run with large callback budgets and bounded completion; no public API can force corrupted internal runaway state to hit the 32-transition cap. No artificial production hook was added. |
| P7: reset/reinit dirty state | The prior normal-wait fix was incomplete at cancellation/deadline boundaries. Applied the small terminal-path correction described below and added 24 boundary scenarios. |
| P7: duplicate persistence assignment | Already removed from the failure step. Factory reset's necessary in-progress uncertainty remains. |
| P7: unused constants / `tick()` | Retain documented source compatibility. Unused within this repository does not mean unused by consumers; `tick()` is explicitly status-only. Removing them or adding deprecation noise has no current correctness benefit. |
| P7: CODEOWNERS | Refuted again against GitHub's official documentation: eligible account-associated email addresses are allowed. Account association/write access cannot be proven from the local file. |
| P7: duplicate CLI checker tables | They still agree. Added a parity assertion for tables and parser output; no shared parser abstraction is needed. |
| Deliberately not changed: five claims | Cached `SAMPLE_FRESH` remains record provenance; safety validity denotes a meaningful timestamp; end/begin result backpressure is intentional; rebind preserves owner/epoch continuity; `NOT_STARTED` remains observable through operation results without counting as a hardware failure. Retained. |

Primary-source adapter and CODEOWNERS links are retained in the corresponding
earlier sections. Arduino's separate error vocabulary was also checked against
the pinned [Wire implementation](https://github.com/espressif/arduino-esp32/blob/3.3.11/libraries/Wire/src/Wire.cpp)
and [I2C HAL](https://github.com/espressif/arduino-esp32/blob/3.3.11/cores/esp32/esp32-hal-i2c-ng.c).
Source verification is not a physical ACK/NACK measurement.

### Changes made in this pass

An acknowledged `REINIT` or `FACTORY_RESET` could physically finish its 30 ms
or 1200 ms wait, but cancellation or deadline expiry could publish the result
before `_stepMaintenance()` cleared obsolete dirty fields. This was an
uncovered continuation of P7, not a reason to replace the operation engine.

Nine lines in the existing `_finishOperation()` now discard those fields when
the active operation is an acknowledged reset/reinit in `WAIT_EXECUTION` and
the terminal timestamp has reached its due time. The update happens before
copying configuration into the terminal result. It adds no state, allocation,
API, retry, or I2C. Earlier termination and failed/ambiguous command writes
retain their evidence, and `persistenceIndeterminate` still requires successful
verification. Clearing at command acknowledgement, as the audit proposed,
would still be premature.

The new public-contract regression spans both commands, cancellation/expiry,
one millisecond before/exactly at/one millisecond after settling, and normal
and wrapping time: 24 scenarios. It checks cache/result agreement, retained
uncertainty, zero further callbacks and zero-write persistence after reinit
reconciliation. Against the original implementation the regression failed
with `dirtyMask` expected `0x0000`, actual `0x0002`; the previous 67 tests
passed. With the fix all 68 tests pass.

Tooling regressions now run in CI through the existing HIL test script and
`tools/test_audit_guards.py`. Five mutation checks confirmed detection of the
old dotted-path normalizer, forbidden-path matcher, missing required wrapper,
missing raw-source check, and missing live ANSI normalization.

README, the owner guide, test coverage notes, and changelog now describe the
boundary behavior. The documentation index now acknowledges this explicitly
requested report. Removed the superseded working input after dispositioning
its findings, as its own instructions requested; it remains recoverable from
the linked git revision. This also fixes its stale relative PDF link, which
failed the starting repository-hygiene check. The manifest remains staged at
`1.3.2`; no release tag is created.

### Current validation

| Check | Current local result |
| --- | --- |
| `.\scripts\pio.cmd test -e native` | Baseline 67/67 passed; new regression failed before the fix; final 68/68 passed. |
| `.\scripts\pio.cmd test -e native_ubsan` | Cannot link locally: installed Windows GCC lacks `-lubsan`. Linux CI below passed. |
| `.\scripts\pio.cmd run -e esp32s3dev -e esp32s2dev` | Both passed with the existing `C:\pio` package directory and process-local `PLATFORMIO_OFFLINE=1`. An initial attempt using the longer user-profile package path failed while unpacking a framework header beyond Windows path limits; the existing short-path cache resolved it. No Core installation or machine setting change. |
| `.\scripts\pio.cmd pkg pack . -o .pio/SCD41-reaudit-package.tar.gz` | Passed. |
| Package content check and clean source/packed consumers | All passed, including host compile/link/run. |
| `python tools/check_target_package_consumer.py .pio/SCD41-reaudit-package.tar.gz` | Local build failed when framework headers became unavailable in shared `C:\pio\packages`. The build selected Arduino 3.2.0; inspection after the failure found 3.3.11 at that same package path. This indicates concurrent package replacement; isolated CI below passed. |
| Version check, regeneration, generated-file diff | Passed; all three generated tracked files remain unchanged. |
| Core timing, repository hygiene, Arduino CLI, IDF example guards | All passed. |
| `python tools/test_audit_guards.py` | Seven tests passed, including 72 forbidden archive-path scenarios. |
| `python tools/test_scd41_hil_runner.py` | Passed, including the new live-reader test. |
| HIL parser self-test and `--dry-run --port COM8` | Passed; dry-run output is under ignored `.pio/reaudit-hil-dry-run`. No serial hardware was opened. |
| `doxygen Doxyfile` | Passed without warnings using installed Doxygen 1.13.2. CI pins 1.17.0. |
| `git diff --check` | Passed. |

Physical hardware/HIL was not run; the physical release gate remains open.

### Current completed CI evidence

Implementation commit: `21e60e9cde732efd209e278e227f12ee79bab5c6`.
[CI run 34210160897](https://github.com/janhavelka/SCD41/actions/runs/34210160897)
completed successfully on 2026-09-08. All seven jobs passed; the native and
sanitizer suites share one job. Job logs were fetched and inspected.

| Check | Evidence |
| --- | --- |
| Native public-contract tests | `68 test cases: 68 succeeded`, at 09:28:28 UTC. |
| Native undefined-behavior sanitizer | `68 test cases: 68 succeeded`, at 09:28:30 UTC. This closes the local missing-runtime gap. |
| Arduino ESP32-S3 and ESP32-S2 | Both `platformio-build` jobs succeeded. |
| Native ESP-IDF ESP32-S3 | Pinned v6.0.1 build records image creation and `Project build complete` at 09:30:40 UTC. |
| Native ESP-IDF ESP32-S2 | Pinned v6.0.1 build records image creation and `Project build complete` at 09:30:36 UTC. |
| Package | Content and clean-consumer checks passed. `Exact target package consumer PASSED` at 09:29:15 UTC for board contract `b708f511964db6c51e949e99c67820476f00f9c7`, closing the local shared-package failure. |
| Guards | Exact Doxygen 1.17.0, metadata synchronization, core/repository/CLI guards, both tooling test scripts, HIL parser/dry-run, and generated-file/whitespace checks all passed. |

The follow-up documentation commit records these completed results without
changing the tested implementation. It does not represent physical hardware
validation or authorize a release tag.

## Earlier review and recorded evidence (2026-09-05)

The remainder records the preceding implementation and its validation. Its
commit hashes, counts and environment notes describe that earlier run, not
new runs on the current revision.

Review date: 2026-09-05. Starting revision:
`96e5233f6fd95e0393fe7567022905d365ab484e` on `main`.

The repository was clean. `git fetch --all --prune` confirmed that `origin/main`
was the newest remote branch; `git merge --ff-only origin/main` reported it
already current. There was no `docs/CODE_AUDIT.md` in this revision. The review
therefore used the root
[AUDIT.md at the starting revision](https://github.com/janhavelka/SCD41/blob/96e5233f6fd95e0393fe7567022905d365ab484e/AUDIT.md).
This requested report replaces that working audit. Durable contracts were
updated in their existing headers and integration/porting guides.

Every published finding was checked, including the sections describing earlier
fixes and rejected findings. The audit's unpublished 141 original candidates
cannot be independently reconstructed from its summary counts. Three parallel
review tasks covered earlier fixes, public-contract tests, and adversarial core
review. The implementation retains the public enum values, compatibility APIs,
fixed-memory operation engine, one shared clock domain, and zero hidden retries.
The manifest remains staged at `1.3.2`; this is not a tagged release.

## Previously correct protocol behavior

| Claim | Verification and disposition |
| --- | --- |
| Command words | All 30 entries match Table 9 in the bundled [datasheet v1.7](../reference/vendor/SCD41_datasheet.pdf). Retained. |
| Execution constants | Documented execution durations match. The audit overstates the table comparison for periodic starts: their datasheet duration is not applicable; the driver's 1 ms wait is its conservative spacing policy. |
| CRC | Polynomial `0x31`, initialization `0xFF`, MSB-first framing and vendor `CRC(0xBEEF) == 0x92` are correct. Retained. |
| Temperature/humidity | Exact `65535` denominator and nearest rounding give `-45000`/`130000` mC and `0`/`100000` milli-percent endpoints. Offset `5400` mC encodes to `0x07E6`. Retained. |
| Ambient pressure | Integer `Pa / 100` gives 700, 1200 and 1013 for the quoted inputs. Retained. |
| Operation limits | Callback/wait limits agree with the bounded paths. Existing all-operation tests already exercise these limits; additional numeric sensor-wait tests now avoid deriving expectations from implementation constants. |

## Findings described as already fixed

| Audit item | Current verification and action |
| --- | --- |
| 1. ESP-IDF could never attach | **Refuted for the pinned v6.0.1 driver.** Its synchronous path reports NACK as `ESP_ERR_INVALID_RESPONSE`, which the adapter already recognized before the audit's change. Mapping `ESP_FAIL` and `ESP_ERR_INVALID_STATE` to NACK could instead accept queue/controller failures as expected wake behavior. Removed those mappings and the unused probe-only `ESP_ERR_NOT_FOUND` case. Only `ESP_ERR_INVALID_RESPONSE` becomes NACK; other generic failures remain `BUS_ERROR`, with original detail retained. Corrected the porting guide. |
| 2. Variant admission | The previous fix is valid. Low-power periodic and ASC target belong to the SCD4x-wide command set; section 3.11 admits SCD41/SCD43. Added non-strict SCD40/SCD43 admission regressions. This does not claim full product support for those other sensors. |
| 3. Live HIL matching | Valid. Live and self-test matching strip ANSI and ignore case. Exercised actual `run_step()` with colored, mixed-case output split across serial chunks; the raw transcript remains intact. |
| 4. Forbidden package paths | The top-level fix is valid, but “never detected” is too broad: prefixed paths could match previously. Found and fixed the remaining prefixed empty-directory case, such as `pkg/.github`. Also made the fixed-name IDF component wrapper a required package member. |
| Generation wrap | Zero-skipping, admission-only generation advancement fixes permanent rejection after wrap. Retained. Corrected a comment claiming lifetime uniqueness: finite generations can repeat if callers retain old IDs and reuse request IDs across a complete cycle. |
| Wire write NACK | Conservative `INDETERMINATE` is appropriate because the adapter cannot prove whether address or data bytes were NACKed. Retained. |
| Wire empty/failed read | `FAILED`/`INDETERMINATE` avoids inventing a NACK cause for an unclassified timeout/bus fault. Retained. |
| Scanner timeout | The scanner sets and restores its own bounded timeout. Retained. |
| Diagnostic zero command | Read and write requests consistently reject `0x0000` with `INVALID_PARAM`. Retained. |
| Unused Arduino/Wire stubs | Removed files and include path remain absent; no live references found. No replacement stubs added to production. |
| Native warning flags | `-Wall -Wextra -Werror=return-type` are present in the independent native environment. Retained. |
| Arduino-source reuse guard | Raw text is checked for include paths; a mutation check confirms the forbidden source path is detected. Retained. |
| Empty timing allowlist | Direct zero-tolerance guard replaced the empty allowlist. Retained. |
| CI generated-file diff | Metadata regeneration precedes `git diff --exit-code`, making the check effective. Retained. |
| Derived limit literals | The former 1533/33 literals are constant-derived with equivalent values. Retained. |
| Dead setting helper/self-assignment | Both remain removed. No new abstraction introduced. |
| Fixed IDF component name | `examples/idf/basic/components/SCD41/CMakeLists.txt` supplies the stable component name and resolves the core paths independently of checkout name. Retained and added to required package contents. |

The IDF correction is based on the pinned
[public API](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_driver_i2c/include/driver/i2c_master.h)
and [synchronous driver implementation](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_driver_i2c/i2c_master.c),
and a compiled host harness against the actual adapter. Host SDK stubs validate
the adapter's mapping logic, not physical controller behavior.

The earlier documentation changes also hold: exact fixed-point formulas and
repository layout in `AGENTS.md`; corrected variant scope, `0x21AC`, and spacing
attribution in the protocol reference; HIL sequence matching the runner;
accurate coverage wording; and removal of obsolete dated-report/version guards.
The earlier changelog is retained as history, with the correction recorded above
it in the new unreleased entry.

## Open proposals and implemented alternatives

| Proposal | Finding and final decision |
| --- | --- |
| P1: misleading phases | **Valid, with narrower impact.** Cancelling or timing out during response waits could report `WAIT_WAKE`, `WAIT_STOP`, or `SEND_READ_COMMAND`. Physical read failures already transitioned to response phases, so the audit's examples overstate that part. Removed three artificial wait transitions and wait directly in the existing `READ_READY_RESPONSE`, `READ_RESPONSE`, or `READ_VERIFY_RESPONSE` phase until due. Aligned wake serial/variant labels with attach. This deletes code and avoids the proposed public enum addition and extra routing state. Tests assert wait cancellation and wake response failure phases. |
| P2: read cancellation loses attachment | **Valid.** Reconciliation now depends on `effectfulWriteAttempted`, preserving attachment and periodic mode for cancelled managed reads and setter prereads. Effectful and diagnostic work remains conservative. Tests cover each fetch stage and cancellation before/after setter mutation. Cancellation still cannot restore a consumed sensor sample. |
| P3: callback time replaces owner time | **Valid, but not permanently wedged.** Calls can be rejected until sampled owner time catches up to a callback completion. Removed callback writes to the owner watermark while retaining completion-based scheduling and deadlines in the same clock domain. Cancellation clamps backward owner time to the last accepted owner observation, so it remains a zero-I2C escape path. `start()` and active `poll()` retain backward-time rejection. Tests cover repeated sampled time, cancellation, retained safety and end timestamps. |
| P4: long idle breaks admission | **Valid; the proposal alone is incomplete.** Every valid poll now records owner time before idle/result-pending returns and expires a reached safety gate. Otherwise an old safety timestamp could itself become future-looking after half-range even after fixing the watermark. Documented observations less than `2^31` ms apart, including idle/result retention. Tests span more than half-range and wrap. |
| P5: two deadline policies | **Valid.** `poll()` expiry delegates to `_finishOperationFailure()`. Timeout reconciliation uses any prior effectful attempt, not merely the latest callback; partial configuration results and EEPROM uncertainty use that same terminal path. Removed duplicated terminal publication logic. |
| P5 boundary found during verification | A successful/ambiguous effectful callback could return failure or cross its deadline before its step installed the long sensor wait, leaving only 1 ms spacing. The transfer executor now establishes the complete command settle immediately: attach wake/stop use 30/500 ms, and other typed commands use existing execution durations. `NOT_STARTED` creates no gate; proved `NO_EFFECT` retains only ordinary spacing except expected NACK reconciliation. Tests check callback failure/deadline boundaries and early admission. |
| P2/P3 interaction found during verification | Cancellation copied a logical `nextDueMs` over the physical safety gate. With a callback finishing after the owner's sampled time, that could shorten `completedMs + 1` to `completedMs`. Removed the redundant cancellation copy and six step-level gate copies. The transfer executor now owns gate establishment; polling owns expiry. |
| P7: reinit/reset dirty settings | **Valid, with safer timing.** Clear discarded `dirtyMask` after acknowledged reinit/reset finishes its execution wait, before identity verification. Clearing at acknowledgement would be premature during cancellation inside the settle. Verification failure no longer preserves obsolete runtime dirty work. Failed/ambiguous command writes remain conservative, and `persistenceIndeterminate` stays set until successful reset/reinit verification. |
| P7: duplicate persistence failure assignment | **Valid.** Removed the in-step failure assignment; `_finishOperationFailure()` owns the error rule. Factory reset's in-progress uncertainty remains necessary and is retained. |
| P7: unused public constants | Unused internal references do not justify breaking downstream source compatibility. Retained the size/interval/range/default constants and legacy aliases. Clarified reserved, undefined `SERIAL_VARIANT_SCD42` and the legacy CO2 bound, which is not the raw uint16 representation limit. The report's stated unused-count also does not agree with its listed groups. |
| P7: `tick()` loses slot state | True by design for this documented compatibility wrapper. Retained `poll(nowMs, 1).status`, clarified its status-only result, and kept the existing guidance to use `poll()` for new callers. No deprecation attribute or breaking removal was necessary. |
| P7: CODEOWNERS email invalid | **Refuted.** GitHub supports an email associated with an eligible account; write access is still required. No source change is justified by the syntax claim, and account association is not proven by repository contents. |
| P7: duplicate CLI checker parsers | Duplication exists; current command tables agree. The report recommends extraction if they drift again, and no current drift was found. Retained the checks rather than introducing shared parser infrastructure without a current correctness need. |

GitHub's supported email syntax and account prerequisites are documented in
[About code owners](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners).

## P6 test gaps

The existing public-contract Unity suite was extended using its injected model;
no test-only production hooks or additional scheduling abstraction were added.

| Gap | Resolution |
| --- | --- |
| No `finalPhase` assertions | Added response-wait cancellation and wake serial/variant failure assertions. |
| Non-strict variant policy | Added SCD40/SCD43 cases covering family-wide low-power/ASC target access and all gated section 3.11 operations. |
| Numeric execution waits | Added observed early/at-boundary checks using literal datasheet milliseconds for stop, both single shots, FRC, persist, reinit, reset, self-test and wake. |
| Diagnostic payload | Check requested word counts 1–3 and exact payload values; CRC failure publishes no unverified words. |
| Zero offline threshold | Check repeated failures remain `DEGRADED`, failure counters advance, and successful transfer restores `READY`. |
| RHT-only validity | Model supplies nonzero CO2; result/cache do not claim `SAMPLE_CO2_VALID`, while full single shot does. |
| 32-transition defensive cap | Valid operations were already bounded by callback limits, spacing, waits and no-progress detection. Extended all-operation execution to callback budgets including 255. No legitimate path requires 32 transitions in one call. An intentionally corrupted internal-state runaway cannot be created through the public contract, so no production hook or implementation-mirroring test was added. The defensive cap remains. |

Additional regressions cover P2–P5 interactions, long-idle wrap, early admission
after late/ambiguous writes, and reset/reinit dirty/uncertainty behavior.

## Previously rejected findings

| Finding | Recheck |
| --- | --- |
| Cached `SAMPLE_FRESH` | Retained provenance semantics: the flag describes the completed operation's record. A new no-data operation starts with an empty working value; sample age uses timestamp/sequence. |
| `nextSafeCommandValid` remains true past due | Validity means the timestamp is meaningful, not that a wait is active. Kept that meaning, and separately fixed long-idle ageing under P4. |
| `end()` then `begin()` is busy | Retained exactly-once terminal result backpressure, including across rebind. |
| `begin()` preserves epoch/owner watermark | Retained identity and time continuity across rebind; P3/P4 correct how the watermark is observed. |
| `NOT_STARTED` does not affect transfer health | Retained: no physical attempt occurred, while the operation result still reports the rejection. |

## Validation

| Check | Recorded result |
| --- | --- |
| Original source and tests at `96e5233`, temporary `g++` build | **53/53 passed.** |
| `.\scripts\pio.cmd test -e native` | **67/67 passed**, including 14 new tests. |
| Regression sensitivity: current tests with original `src/SCD41.cpp` | **10 expected failures, 57 passes.** Nine new bug regressions and the changed backward-cancellation assertion fail the original implementation. The five new tests of previously correct behavior pass it. |
| `.\scripts\pio.cmd test -e native_ubsan` | Local link blocked: installed Windows GCC lacks `-lubsan`. No local sanitizer pass claimed. |
| `.\scripts\pio.cmd run -e esp32s3dev -e esp32s2dev` | **Both passed** with process-local managed-core/cache overrides below. S3: 24,680 B RAM / 356,880 B flash; S2: 51,144 B RAM / 346,284 B flash. |
| `.\scripts\pio.cmd pkg pack . -o .pio/SCD41-audit-package.tar.gz` | **Passed.** |
| `python tools/check_package_contents.py .pio/SCD41-audit-package.tar.gz` | **Passed.** |
| `python tools/check_clean_consumer_compile.py .` and packed archive | **Both passed**, including compile/link/run. |
| `python tools/check_target_package_consumer.py .pio/SCD41-audit-package.tar.gz` | Local build failed when the shared Arduino package directory changed during compilation: framework headers became missing/inaccessible, and the directory subsequently contained 3.3.11 despite this build installing the pinned 3.2.0. No local exact-target pass claimed. |
| Version metadata check and sync | **Passed**; generated tracked files remained unchanged. |
| Core timing, repository hygiene, Arduino CLI and IDF example guards | **All passed.** |
| `python tools/test_scd41_hil_runner.py` and parser self-test | **Passed.** |
| HIL runner `--dry-run --port COM8` | **Passed**, with plan/report/transcript under ignored `.pio/audit-hil-dry-run`. No serial hardware was opened. |
| `doxygen Doxyfile` | **Passed without warnings** using installed Doxygen 1.13.2. CI separately pins 1.17.0. |
| IDF adapter host harness | **21 error/direction cases plus one zero-I2C invalid-request case passed**, compiling the actual adapter with temporary SDK stubs. |
| Package predicate harness | **42 forbidden cases and 12 allowed lookalikes passed**, plus normalization and required-wrapper assertions. |
| Live HIL parser and raw-source guard mutation harnesses | **Passed** for colored/split serial output and forbidden Arduino source literals. |
| `git diff --check` | **Passed.** |

The inherited `PLATFORMIO_CORE_DIR=C:\pio` initially caused the current Arduino
platform's Python/uv setup to exit 106. Successful Arduino example builds used
the existing managed environment, without another PlatformIO Core installation
or persistent environment changes:

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path $env:USERPROFILE '.platformio'
$env:PLATFORMIO_OFFLINE = '1'
.\scripts\pio.cmd run -e esp32s3dev -e esp32s2dev
```

There is no local native ESP-IDF installation or usable local sanitizer
alternative. The repository's CI completed these checks successfully after the
implementation push, as recorded below. Physical SCD41/HIL validation was not
performed; that release gate remains open and no release tag was created.

## Completed CI evidence

Implementation commit: `98dd2f9aeaca0211c8569c0eeac74db60171a73d`.
[CI run 33987486144](https://github.com/janhavelka/SCD41/actions/runs/33987486144)
completed successfully on 2026-09-05. All seven jobs passed:

| Job | Evidence |
| --- | --- |
| Native tests | 67/67 passed; job log records `67 test cases: 67 succeeded`. |
| Native undefined-behavior sanitizer, same job | 67/67 passed on Linux with the configured sanitizer flags; the second suite summary also records `67 test cases: 67 succeeded`. This closes the validation gap caused by the local missing runtime. |
| Arduino ESP32-S3 | `platformio-build (esp32s3dev)` succeeded. |
| Arduino ESP32-S2 | `platformio-build (esp32s2dev)` succeeded. |
| Native ESP-IDF ESP32-S2 | Pinned ESP-IDF v6.0.1 build log records `Successfully created ESP32-S2 image` and `Project build complete` at 19:36:27 UTC. |
| Native ESP-IDF ESP32-S3 | Pinned ESP-IDF v6.0.1 build log records `Successfully created ESP32-S3 image` and `Project build complete` at 19:36:36 UTC. |
| Package | Package content and clean package-consumer checks passed; `Exact target package consumer PASSED` for the pinned TunnelMonitor-node board contract `b708f511964db6c51e949e99c67820476f00f9c7`. This closes the local shared-package build gap. |
| Guards | Exact Doxygen 1.17.0, synchronized metadata, core/repository/CLI guards, HIL parser/dry-run checks and generated-file/whitespace checks all passed. |

The two native suites share one job; the table separates their evidence. CI job
logs were fetched and inspected, not inferred from source compatibility. The
subsequent report-only commit records these results without changing the tested
implementation. All source changes and this report are synchronized on `main`.
