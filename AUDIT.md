# SCD41 Library Audit

Date: 2026-08-29. Target: the staged `1.3.2` manifest.

This is a working document, not part of the documentation set: it lives at the
repository root rather than under `docs/`, and it should be deleted once the
open proposals below are resolved or declined. Durable decisions belong in the
reference, integration, porting, or validation guide that owns them.

Scope: the whole repository, read against the Sensirion SCD4x datasheet v1.7
(April 2025) bundled at [`docs/reference/vendor/SCD41_datasheet.pdf`](docs/reference/vendor/SCD41_datasheet.pdf).

Method: 12 parallel review dimensions raised 141 candidate findings. Each was
then judged by independent adversarial verifiers under distinct lenses
(datasheet/code evidence, reachability through the public API, and whether the
behaviour is deliberate and the proposed fix sound). 72 survived, 33 split, 36
were refuted. Every claim below was re-checked against the source, and the
runnable ones were reproduced.

Everything in [Fixed in this pass](#fixed-in-this-pass) is already applied.
Everything in [Open proposals](#open-proposals) is deliberately **not** applied:
each one changes published behaviour, adds public API, or is a refactor whose
shape is the maintainer's call. Each carries a concrete proposal.

Baseline at the time of writing: 53/53 host tests pass and all seven offline
guards pass, before and after every change.

---

## What is already correct

Worth stating, because it bounds the audit: the protocol core is sound.

- Every command word in `CommandTable.h` matches datasheet Table 9.
- Every execution-time constant matches the datasheet's max-command-duration
  column and the per-section text.
- CRC-8 (`0x31`/`0xFF`, no reflection, no final XOR) matches, including the
  vendor vector `CRC(0xBEEF) == 0x92`.
- Conversions match exactly, verified by execution:
  `T(0) = -45000 mC`, `T(65535) = 130000 mC`, `H(0) = 0`, `H(65535) = 100000`,
  and `encodeTemperatureOffsetMilliC(5400) == 0x07E6`, which is the datasheet's
  own worked example for 5.4 °C.
- Ambient pressure encodes as the exact integer `Pa / 100`: 70000 → 700,
  120000 → 1200, 101300 → 1013.
- The `limits()` table matches a hand-trace of every state-machine path.

---

## Fixed in this pass

### 1. The ESP-IDF example could never attach — *critical*

`examples/idf/basic/main/IdfI2cTransport.cpp` mapped `TransferCode::NACK` only
from `ESP_ERR_NOT_FOUND` and `ESP_ERR_INVALID_RESPONSE`. The installed
`driver/i2c_master.h` documents `ESP_ERR_NOT_FOUND` **only for
`i2c_master_probe`**; `i2c_master_transmit`/`_receive` never return it.

Datasheet 3.11.4 is unconditional: *"the SCD4x does not acknowledge the wake_up
command."* Every `ATTACH` therefore begins with a NACK. That NACK fell into the
adapter's `default:` arm as `BUS_ERROR`, so `expectedNack` was false and
`_stepAttach`'s `SEND_WAKE` phase terminated the operation. **No managed
operation was reachable on ESP-IDF at all.** The Arduino build was unaffected
only because `TwoWire::endTransmission()` reports a NACK as status 2.

Fixed by also mapping `ESP_FAIL` and `ESP_ERR_INVALID_STATE`, and documenting
the reason in `docs/porting/esp-idf.md`.

> **Needs hardware confirmation.** I cannot build or run ESP-IDF in this
> environment. The mapping is derived from the IDF header and driver behaviour,
> not observed. Confirm on a real ESP32 before relying on it.

### 2. The variant gate contradicted the datasheet in both directions

`_validateAdmission` rejected `START_LOW_POWER_PERIODIC`, `READ_ASC_TARGET` and
`SET_ASC_TARGET` for any non-SCD41 identity. The datasheet restricts exactly one
group — section 3.11, *"Single Shot Measurement Mode (SCD41 & SCD43 only)"*.
Low-power periodic is section 3.9 (*"the SCD4x features a low power periodic
measurement mode"*) and the ASC target is section 3.8; both are family-wide. The
gate also excluded SCD43 from the very commands section 3.11 grants it.

The gate now lists only the section 3.11 set and admits SCD41 **and** SCD43.

### 3. The HIL runner could not pass on real hardware

`run_step()` matched `re.compile(step.expect, re.DOTALL)` against the raw serial
buffer, while `--parser-self-test` used `strip_ansi(...)` plus `IGNORECASE`. The
CLI colourizes the exact tokens the expectations match. Reproduced:

```
expect: op=ATTACH outcome=SUCCEEDED
LIVE matcher   (raw buffer, DOTALL only)  -> False
SELFTEST match (strip_ansi + IGNORECASE)  -> True
after fix      (strip_ansi before search) -> True
```

Every `outcome=`-matching step would have been recorded as a failure on hardware
while CI stayed green. Fixed by normalising identically on both paths.

### 4. The package guard never detected a forbidden path

`member_has_forbidden_path()` tested `member == forbidden.rstrip("/")` or
`f"/{forbidden}" in member`; tar members carry no leading slash, so a top-level
`docs/reports/…` or `.github/…` matched neither. `normalize_member()`'s
`lstrip("./")` also ate the leading dot of `.github`. Reproduced:

```
before: docs/reports/ -> False   .github/ -> False   dist/ -> False
after:  docs/reports/ -> True    .github/ -> True    dist/ -> True
```

### 5. Other applied fixes

| Issue | Effect |
| --- | --- |
| Generation counter wrapped to `0` | Every later `start()` failed permanently with `STALE_RESULT`. Now wraps to 1 — safe because only one result is ever retained — and is committed only on successful admission. |
| Wire adapter claimed `NO_EFFECT` for a write NACK | Arduino-ESP32 cannot attribute a NACK to the address phase, so a partially accepted write was reported as definitely-not-applied. Now `INDETERMINATE` whenever bytes were written. |
| Wire adapter reported read timeouts as `NACK` | A stuck bus looked like a missing device during bring-up. Now `FAILED`/`INDETERMINATE`. |
| `i2c::scan()` inherited the driver's last timeout | Could be as low as 1 ms, reporting a present sensor as absent. Now sets and restores its own bound. |
| Diagnostic `command == 0` returned `UNSUPPORTED` | Inconsistent with the read path and pointed at a typed replacement that does not exist. Now `INVALID_PARAM`. |
| `-Itest/stubs`, `test/stubs/*.h` | No test included them; they declared `extern` globals with no definition and could not have linked. Deleted. |
| Native tests built with no warning flags | `[env:native]` did not extend `[env]`. Now builds with `-Wall -Wextra -Werror=return-type`, clean. |
| `"Arduino source reuse"` guard ran on stripped code | A path only ever appears in a string literal, so the rule could never fire. Now matched on raw text — and I verified it fires when the forbidden path is introduced. |
| Timing guard's `ALLOWED_FINDINGS` | Permanently empty. Replaced with a direct statement of the zero-tolerance policy. |
| CI `git diff --exit-code` step | Nothing in that job wrote a tracked file, so it could not fail. Now regenerates version metadata first. |
| `limits()` magic numbers `1533` / `33` | Now derived from the constants they depend on; verified to produce byte-identical values. |
| `_applyVerifiedSetting()`, dead self-assign branch | Removed. |
| ESP-IDF example component name | Came from the checkout directory via `EXTRA_COMPONENT_DIRS`, breaking any clone or release archive not named exactly `SCD41`. Now a fixed-name `components/SCD41` wrapper. |

Documentation fixed: `AGENTS.md` no longer mandates the shift-approximation
fixed-point formulas that the code deliberately replaced (a contributor
following it would have reintroduced a fixed bug); the repository map matches
the tree; the protocol reference drops the wrong SCD41-only claims, lists
`0x21AC` in the command summary, and attributes the 1 ms spacing to this driver
rather than to a datasheet `tIDLE` number; the HIL safe sequence matches the
runner; `feature-coverage.md` drops a claim about a colour check that does not
exist and its product-pinned audit section is generalised; the dated
naming-hygiene report and the guard clause that version-pinned it are gone.

---

## Open proposals

### P1. `OperationPhase` is overloaded, so `finalPhase` lies — *medium*

`_stepMeasurement` uses `WAIT_WAKE` as "waiting for the data-ready response" and
`WAIT_STOP` as "waiting for the sample response". `_stepMaintenance` uses
`SEND_READ_COMMAND` as a pure zero-I2C timer. In `_stepWriteLike`'s wake path
the labels are inverted relative to every other step function:
`READ_VERIFY_RESPONSE` reads the serial number and `READ_RESPONSE` reads the
variant, the opposite of `_stepAttach`.

`OperationResult::finalPhase` is public diagnostic output and both CLIs print
it, so a failed sample fetch reports `WAIT_WAKE` and a failed maintenance verify
reports `SEND_READ_COMMAND`. Nothing is functionally wrong; the published
diagnostics are simply false, which is expensive in a bring-up tool.

**Proposal.** Append one honest value `WAIT_RESPONSE` ("zero-I2C wait for a
pending CRC-protected response") after `WAIT_EXECUTION`, add its
`operationPhaseName()` case, and use it for all three generic waits. Then swap
the four inverted case labels in `_stepWriteLike`'s wake branch
(`SEND_VERIFY_COMMAND`→`SEND_READ_COMMAND`, `READ_VERIFY_RESPONSE`→
`READ_RESPONSE`, and the converse for the variant pair), updating the three
assignments in its `WAIT_EXECUTION` router. Behaviour is unchanged.

Not applied because it adds a public enum value and no test asserts `finalPhase`
today (see P6), so a mistake would be silent. Add the assertions first.

### P2. `cancel()` of a pure read tears down attachment and periodic mode — *medium*

`cancel()` calls `_markReconciliationRequired()` whenever `callbacksUsed > 0`,
regardless of whether any effectful byte was written. Cancelling a
`FETCH_SAMPLE` after its one data-ready read therefore clears `attached`, sets
`operatingMode = UNKNOWN`, and forces a full `ATTACH` — which itself sends
`stop_periodic_measurement` plus a 500 ms settle, destroying the measurement
session the owner deliberately started. A harmless cancelled read costs the node
its periodic mode and half a second of bus time.

**Proposal.** Guard the call: `if (_active.effectfulWriteAttempted) {
_markReconciliationRequired(); }`. Every kind whose bytes can change sensor state
sets that flag on its first effectful attempt, so the conservative behaviour is
preserved exactly where it matters. Add a test that cancels `FETCH_SAMPLE` after
one callback in periodic mode and asserts mode and attachment survive.

Not applied: it relaxes a published conservatism, which is the maintainer's call.

### P3. A transport `completedMs` ahead of the owner clock wedges `poll()` — *medium*

`_attemptTransfer()` writes the adapter's `completedMs` into `_lastOwnerNowMs`,
the watermark that `poll()`, `start()` and `cancel()` all check for backwards
motion. If the adapter timestamps from a finer clock than the owner samples —
e.g. the owner reads a 10 ms RTOS tick once per scheduler pass, exactly as the
README example shows, while the adapter uses a 1 ms timer — every subsequent
call inside that tick is rejected with `INVALID_PARAM`. The operation cannot be
polled and **cannot be cancelled**, because `cancel()` applies the same check.

**Proposal.** Keep the two clocks separate. Stop updating `_lastOwnerNowMs` from
the transport; the local `nowMs` is still advanced to `completedMs` for deadline
and `nextDueMs` arithmetic, so only the owner-facing monotonicity check changes.
This also makes `end()`'s completion timestamp literally "the last owner
timestamp accepted by the driver", as the integration guide already claims.
Additionally, make `cancel()` unfailable on a clock anomaly by clamping rather
than rejecting, so the owner always has a bounded zero-I2C way out.

### P4. `poll()` skips owner-clock housekeeping when idle — *medium*

`poll()` returns early when idle or result-pending, before recording `nowMs`. If
no operation runs for more than 2^31 ms (~24.8 days) while the owner keeps
polling, the next `start()` is rejected with "Owner clock moved backwards" and
stays broken until the 32-bit distance wraps back — another ~24.8 days. A node
that attaches at boot and then samples only on demand hits this.

**Proposal.** Observe owner time on every `poll()` call, before the early
returns, so the monotonicity guard is about poll cadence (bounded and
documented) rather than operation cadence (unbounded):

```cpp
const bool ownerClockOk = !_lastOwnerNowValid || _timeReached(nowMs, _lastOwnerNowMs);
if (ownerClockOk) { _lastOwnerNowMs = nowMs; _lastOwnerNowValid = true; }
```

then branch on `ownerClockOk` where the current check sits. Add an
`ASSUMPTIONS.md` bullet stating that consecutive owner time observations must be
less than 2^31 ms apart.

### P5. Deadline expiry is implemented twice with different rules — *medium*

`poll()` has a bespoke expiry block, and `_finishOperationFailure()` has another
for a timeout that surfaces from a step. They disagree: whether
`reconciliationRequired` is set on an identical physical state depends on which
side of a 1 ms boundary the clock crossed.

**Proposal.** Delete the block in `poll()` and call
`_finishOperationFailure(Status::Error(Err::TIMEOUT, "Operation deadline expired"), driverNowMs)`,
moving the timeout-specific conservatism into `_finishOperationFailure` gated on
`status.code == Err::TIMEOUT`. `_finishOperationFailure` already handles the
`READ_CONFIGURATION` partial case, so that duplication disappears too. One
timeout path, one set of rules.

### P6. Test gaps worth closing

Ranked by risk. None of these are hypothetical — each covers a path that a
confirmed finding above touches.

| Gap | Why it matters |
| --- | --- |
| No test asserts `OperationResult::finalPhase` | The entire published phase vocabulary is unverified; P1 cannot be applied safely without this. |
| `strictVariantCheck = false` is never exercised | The whole non-SCD41 admission gate — the code fixed in §2 — has no test. Attach an SCD40 model and assert low-power periodic and ASC target are admitted while single shot is not. |
| No test pins execution times to the datasheet | `EXECUTION_TIME_*` could drift silently. Assert the observed wait for stop/single-shot/FRC/persist/reset/self-test. |
| Diagnostic read payload never asserted | `rawWords`/`wordCount` are returned but never checked against the injected words. |
| `offlineThreshold == 0` is untested | A documented contract ("zero disables `OFFLINE`") with no coverage. |
| RHT-only CO2-invalid flag untested | The fake bus returns a non-zero CO2 word for it, so a regression that wrongly set `SAMPLE_CO2_VALID` would pass. |
| `poll()`'s 32-transition cap is untested | Neither that it bounds a runaway nor that no legitimate operation exceeds it. |

### P7. Smaller confirmed items, batched

| Item | Proposal |
| --- | --- |
| `REINIT` acknowledged but verification fails leaves `dirtyMask` set | A later `PERSIST_SETTINGS` is admitted and burns an EEPROM cycle writing values `reinit` already reloaded. Clear `dirtyMask` as soon as the reinit/reset write is acknowledged (the failure path returns earlier, so the command is known written). |
| `_stepMaintenance` sets `persistenceIndeterminate` redundantly | `_finishOperationFailure` already decides this. Delete the in-step assignment so one place owns the rule. |
| Unused `CommandTable.h` constants | 14 are referenced nowhere: the four size aliases, both periodic-interval constants, both CO2 range bounds, `ALTITUDE_MIN_M`, the four default-value constants, and all six `SERIAL_VARIANT_*` aliases — of which `SERIAL_VARIANT_SCD42 = 0x2` advertises an encoding the datasheet does not define. These are public API, so removal is a source-breaking change; I left them alone deliberately. Recommend removing them before the first tagged release, or documenting them as reserved. |
| `tick()` cannot distinguish `IDLE` from `RESULT_PENDING` | It returns only `.status`, which is `Ok()` for both. It is unused by both CLIs and the CLI contract guard forbids it. Recommend deprecating it in favour of `poll()`. |
| `CODEOWNERS` uses a bare email | GitHub requires `@user` or `@org/team`; the file currently has no effect. |
| `check_cli_contract.py` and `check_idf_example_contract.py` | Duplicate ~130 lines of parser and command tables verbatim. Extract the shared table if they drift again. |

---

## Deliberately not changed

Findings raised during the audit that verification rejected, recorded so they
are not re-litigated:

- **`SAMPLE_FRESH` is never cleared on the cached sample.** Raised as a defect;
  refuted. On the result path the flag is correct and load-bearing —
  `_beginOperation` zeroes `_workingValue`, so a `NO_DATA` fetch genuinely
  publishes `flags == SAMPLE_NONE`, and that is exactly what both CLIs gate on.
  No shipped consumer reads freshness from a peeked sample, and the header's
  wording ("produced by the completed operation") stays true of a cached record.
  Age belongs to `capturedAtMs`/`sequence`, which the API already carries.
- **`nextSafeCommandValid` stays true after the window elapses.** The flag means
  "the timestamp field is meaningful", not "a wait is in effect" — a wrap-safety
  signal, as the header and integration guide both state. `start()` compares
  wrap-safely on every call.
- **`end()` then `begin()` returns `BUSY` until the result is consumed.** This is
  the documented exactly-once contract, stated on both `end()` and `begin()` and
  covered by an existing test.
- **`begin()` does not reset the owner-clock watermark or the sensor epoch.**
  Intentional: it prevents identity reuse across rebinds.
- **Adapter rejections with `NOT_STARTED` do not touch transfer health.** Also
  intentional — they were never attempted transfers, and they remain visible
  through the operation channel.
