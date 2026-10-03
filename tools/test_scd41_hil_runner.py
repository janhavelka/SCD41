#!/usr/bin/env python3
from __future__ import annotations

import builtins
import contextlib
import io
import itertools
import json
import pathlib
import sys
import tempfile
import types
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import scd41_hil_runner as hil  # noqa: E402


def assert_equal(actual, expected, message: str) -> None:
    if actual != expected:
        raise AssertionError(f"{message}: expected {expected!r}, got {actual!r}")


def assert_true(value, message: str) -> None:
    if not value:
        raise AssertionError(message)


def assert_false(value, message: str) -> None:
    if value:
        raise AssertionError(message)


def operation_output(kind="ATTACH", request=1, generation=1, payload=None, duration=600, epoch=1):
    if payload is None:
        payload = "identity valid=yes serial=0x100123456789 variant=SCD41 variant_word=0x1000 epoch=1\n"
    callbacks = 4
    if kind == "ATTACH":
        callbacks = 6
    elif kind == "READ_CONFIGURATION":
        callbacks = 14
    elif kind.startswith("READ_") and kind != "READ_IDENTITY" or kind == "SELF_TEST":
        callbacks = 2
    elif kind in ("START_PERIODIC", "START_LOW_POWER_PERIODIC", "STOP_PERIODIC", "POWER_DOWN", "PERSIST_SETTINGS"):
        callbacks = 1
    elif kind.startswith("SET_") or kind in ("SINGLE_SHOT", "SINGLE_SHOT_RHT_ONLY", "WAKE_UP", "REINIT", "FACTORY_RESET"):
        callbacks = 5
    return (f"status=IN_PROGRESS detail=0 msg=Operation admitted\n"
            f"started request={request} generation={generation} op={kind} deadline=20000\n"
            f"result request={request} generation={generation} op={kind} outcome=SUCCEEDED effect=VERIFIED status=OK callbacks={callbacks} reconcile=no detail=0\n"
            f"timing started=100 completed={100 + duration} deadline=20000 phase=COMPLETE epoch={epoch} mode=IDLE evidence=VERIFIED fields=0x0000\n" + payload)


HEALTH_OUTPUT = ("runtime bound=yes attached=yes state=READY mode=IDLE reconcile=no\n"
                 "slot state=IDLE request=0 generation=0 operation=NONE\n"
                 "health transfer_ok=90 transfer_fail=0 consecutive=0 expected_nack=3 protocol_fail=0 crc_fail=0 operation_ok=30 operation_fail=0 cancelled=0\n"
                 "last_errors transfer=OK@0 protocol=OK@0 operation=OK@0 op=NONE request=0 generation=0\n")
CONFIG_OUTPUT = ("config offset_mC=4000 altitude_m=100 pressure_Pa=100000 asc=on target_ppm=400 initial_h=44 standard_h=156 verified=0x007F dirty=0x0000 persistence_indeterminate=no\n")


class ChunkSerial:
    timeout = 0.1

    def __init__(self, chunks):
        self.chunks = iter(chunks)
        self.written = []

    def write(self, payload):
        self.written.append(payload)
        return len(payload)

    def read(self, size):
        value = next(self.chunks, b"")
        assert_true(len(value) <= size, "test serial chunk respects requested size")
        return value


def capture(step, output, state=None, chunk_size=37):
    raw = output.encode("utf-8")
    serial = ChunkSerial([raw[index:index + chunk_size] for index in range(0, len(raw), chunk_size)])
    transcript = []
    with mock.patch.object(hil.time, "monotonic", side_effect=itertools.count(0, 0.001)), \
            mock.patch.object(hil.time, "sleep"):
        result = hil.run_step(serial, step, 0.1, transcript, state)
    return result, transcript


def test_serial_number_parsing() -> None:
    assert_equal(hil.parse_serial_number("serial=0x100123456789"), "100123456789", "hex serial")
    assert_equal(hil.parse_serial_number("serial_number: 100ABCDEF012"), "100ABCDEF012", "plain serial")
    assert_equal(hil.parse_serial_number("serial=0x1234"), None, "short serial rejected")


def test_missing_serial_import_is_runner_error() -> None:
    original_import = builtins.__import__

    def fake_import(name, *args, **kwargs):
        if name == "serial":
            raise ImportError("forced missing pyserial")
        return original_import(name, *args, **kwargs)

    builtins.__import__ = fake_import
    try:
        try:
            hil.load_serial_module()
        except hil.RunnerError as exc:
            assert_true("pyserial is required" in str(exc), "missing pyserial message")
        else:
            raise AssertionError("load_serial_module() unexpectedly succeeded")
    finally:
        builtins.__import__ = original_import


def test_missing_serial_port_is_validation_error() -> None:
    args = hil.parse_args([])
    try:
        hil.validate_args(args)
    except hil.RunnerError as exc:
        assert_true("--port is required" in str(exc), "missing port mentions --port")
    else:
        raise AssertionError("validate_args() unexpectedly accepted missing port")


def test_destructive_confirmation() -> None:
    assert_true(hil.destructive_confirmation_valid(False, ""), "safe run does not need confirmation")
    assert_false(hil.destructive_confirmation_valid(True, ""), "destructive run needs confirmation")
    assert_true(
        hil.destructive_confirmation_valid(True, hil.DESTRUCTIVE_CONFIRMATION),
        "exact destructive confirmation accepted",
    )


def test_safe_step_contract() -> None:
    assert_equal(hil.missing_minimum_safe_steps(hil.SAFE_STEPS), (), "safe HIL steps cover common commands")


def test_help_contract_detection() -> None:
    help_text = """
    version        Print firmware and library version
    scan           Scan the I2C bus
    begin          Bind and attach the SCD41
    probe          Protocol-qualified probe
    recover        Attach/reconcile
    identity       Read sensor identity
    variant        Read sensor variant
    settings       Read settings
    selfcheck      Aggregate checks
    stress         Bounded readiness stress
    stress_mix     Bounded mixed stress
    status         Print driver health
    """
    assert_equal(hil.missing_minimum_help_commands(help_text), (), "help covers common commands")
    assert_equal(
        hil.missing_minimum_help_commands(
            "scan\nbegin\nprobe\nrecover\nidentity\nvariant\nsettings\n"
            "selfcheck\nstress\nstress_mix\nstatus\n"
        ),
        ("version",),
        "missing version is reported",
    )


def test_failure_token_classification() -> None:
    text = "Status: I2C_TIMEOUT; lastError=CRC_MISMATCH; state=OFFLINE; arg=INVALID_PARAM"
    assert_equal(
        hil.classify_failure_tokens(text),
        ("I2C_TIMEOUT", "CRC_MISMATCH", "OFFLINE", "INVALID_PARAM"),
        "failure tokens keep first-seen order",
    )
    assert_equal(
        hil.classify_failure_tokens("Diagnostics: fail=0 Last error: none State: READY"),
        (),
        "diagnostic counters without failing status are not failures",
    )
    assert_equal(
        hil.classify_failure_tokens("Diagnostics: pass=3 fail=1 skip=0"),
        ("FAIL_COUNT",),
        "nonzero diagnostic failure count is a failure",
    )
    assert_equal(
        hil.classify_failure_tokens("=== Stress Summary ===\n  Errors:   2\n"),
        ("ERROR_COUNT",),
        "nonzero stress error count is a failure",
    )
    assert_equal(hil.classify_failure_tokens("health operation_fail=0 cancelled=0"), (),
                 "zero cancellation counter is healthy telemetry")
    for count in ("1", "01", "10"):
        assert_equal(hil.classify_failure_tokens(f"health cancelled={count}"), ("CANCELLED", "HEALTH_FAILURE"),
                     "nonzero cancellation counter remains a failure")
    assert_equal(hil.classify_failure_tokens("status=CANCELLED"), ("CANCELLED",),
                 "cancelled operation remains a failure")


def test_step_pattern_matching() -> None:
    step = hil.Step("serial", "serial", r"serial=0x[0-9A-Fa-f]{12}")
    assert_true(hil.step_output_matches(step, "serial=0x100123456789"), "serial pattern matches")
    assert_false(hil.step_output_matches(step, "serial unavailable"), "serial pattern rejects missing serial")

    scan_step = next(step for step in hil.SAFE_STEPS if step.command == "scan")
    assert_true(
        hil.step_output_matches(scan_step, "  Found device at 0x62  <target>"),
        "scan pattern matches target device",
    )
    assert_false(
        hil.step_output_matches(scan_step, "No I2C devices found"),
        "scan pattern rejects no-device output",
    )
    workflow_step = next(
        step for step in hil.SAFE_STEPS if step.command == "selfcheck"
    )
    assert_true(
        hil.step_output_matches(
            workflow_step,
            "workflow_summary name=selfcheck outcome=\x1b[32mPASS\x1b[0m",
        ),
        "colored workflow summary matches after ANSI removal",
    )


def test_step_pass_rejects_failure_tokens() -> None:
    assert_true(hil.step_passed(True, "Status: OK\nState: READY"), "matched OK output passes")
    assert_false(
        hil.step_passed(True, "Status: I2C_TIMEOUT\nState: READY"),
        "matched output with failure token fails",
    )
    assert_false(
        hil.step_passed(True, "Diagnostics: pass=3 fail=1 skip=0"),
        "matched diagnostics with failures fail",
    )


def test_live_step_matches_colored_chunks_and_keeps_raw_evidence() -> None:
    step = hil.Step("attach", "begin", r"op=ATTACH outcome=SUCCEEDED", timeout_s=1)
    for prefix, expected_status in (
        ("", "pass"),
        ("Status: \x1b[31mI2C_TIMEOUT\x1b[0m\n", "fail"),
    ):
        raw_output = prefix + operation_output().replace("outcome=SUCCEEDED", "outcome=\x1b[32mSUCCEEDED\x1b[0m")
        result, transcript = capture(step, raw_output, chunk_size=7)
        assert_true(result["matched"], "live matching strips fragmented ANSI")
        assert_equal(result["status"], expected_status, "live failure classification")
        assert_equal(result["last_output"], raw_output, "result preserves raw output")
        assert_equal("".join(transcript), "\n>>> begin\n" + raw_output,
                     "transcript preserves raw serial evidence")


def test_build_steps_timeout_override() -> None:
    args = hil.parse_args(["--dry-run", "--timeout-s", "3"])
    hil.validate_args(args)
    steps = hil.build_steps(args)
    assert_true(all(step.timeout_s == 3 for step in steps), "timeout override applies to all steps")


def test_live_selfcheck_allows_sensor_silence_and_keeps_timeouts_bounded() -> None:
    class Clock:
        now = 0.0

        def monotonic(self):
            return self.now

        def sleep(self, seconds):
            self.now += seconds

    class SelfcheckSerial:
        def __init__(self, clock, completes):
            self.clock = clock
            self.completes = completes
            self.started = 0.0
            self.admission_pending = True

        def write(self, data):
            assert_equal(data, b"selfcheck\n", "selfcheck command")
            self.started = self.clock.now
            return len(data)

        def read(self, size):
            if self.admission_pending:
                self.admission_pending = False
                return b"workflow_start name=selfcheck cycles=1 steps=4\n"
            if self.completes and self.clock.now - self.started >= 10.0:
                if not hasattr(self, "pending"):
                    output = "".join(operation_output(kind, request=index + 1, payload=payload, duration=duration)
                                     for index, (kind, payload, duration) in enumerate((
                                         ("READ_IDENTITY", None, 100), ("READ_SENSOR_VARIANT", None, 100),
                                         ("READ_CONFIGURATION", CONFIG_OUTPUT, 100), ("SELF_TEST", "selftest raw=0x0000\n", 10000))))
                    output += "workflow name=selfcheck progress=4/4 cycles=1/1 pass=4 warn=0 fail=0\nworkflow_summary name=selfcheck outcome=\x1b[32mPASS\x1b[0m\n"
                    self.pending = output.encode()
                chunk, self.pending = self.pending[:size], self.pending[size:]
                return chunk
            return b""

    default_idle = hil.parse_args([]).idle_timeout_s
    safe_step = next(step for step in hil.SAFE_STEPS if step.command == "selfcheck")
    cases = (
        (safe_step, True, "pass", 10.0),
        (safe_step, False, "fail", default_idle),
        (hil.replace(safe_step, timeout_s=3.0), False, "fail", 3.0),
    )
    for step, completes, expected_status, expected_elapsed in cases:
        clock = Clock()
        with mock.patch.object(hil.time, "monotonic", clock.monotonic), \
                mock.patch.object(hil.time, "sleep", clock.sleep):
            result = hil.run_step(SelfcheckSerial(clock, completes), step, default_idle, [])
        assert_equal(result["status"], expected_status, "silent selfcheck result")
        assert_true(abs(result["elapsed_s"] - expected_elapsed) < 0.05,
                    "live reader respects completion, idle and absolute deadlines")


def test_live_final_status_reads_health_and_complete_error_line() -> None:
    class SerialChunks:
        def __init__(self, chunks):
            self.chunks = iter(chunks)

        def write(self, data):
            assert_equal(data, b"status\n", "final status command")
            return len(data)

        def read(self, size):
            chunk = next(self.chunks, b"")
            assert_true(len(chunk) <= size, "serial chunk respects requested size")
            return chunk

    step = next(step for step in hil.SAFE_STEPS if step.name == "final driver health")
    runtime = b"runtime bound=yes attached=yes state=\x1b[32mREADY\x1b[0m mode=IDLE reconcile=no\r\n"
    counters = [b"slot state=IDLE operation=NONE\r\nhealth transfer_ok=90 transfer_fail=0 consecutive=0 expected_",
                b"nack=3 protocol_fail=0 crc_fail=0 operation_ok=30 operation_fail=0 cancelled=0\r\n"]
    errors = b"last_errors transfer=OK@0 protocol=OK@0 operation=OK@0 op=NONE request=0 generation=0"
    cases = (
        ([runtime, *counters, errors, b"\r", b"\n"], "pass", True),
        ([runtime, *counters, errors.replace(b"transfer=OK", b"transfer=I2C_TIMEOUT"), b"\r\n"],
         "fail", True),
        ([runtime], "fail", False),
        ([runtime, *counters, errors], "fail", False),
    )
    for chunks, expected_status, expected_match in cases:
        transcript = []
        with mock.patch.object(hil.time, "monotonic", side_effect=itertools.count(0, 0.01)), \
                mock.patch.object(hil.time, "sleep"):
            result = hil.run_step(SerialChunks(chunks), step, 0.1, transcript)
        raw_output = b"".join(chunks).decode("utf-8")
        assert_equal(result["status"], expected_status, "complete final health classification")
        assert_equal(result["matched"], expected_match, "truncated final response cannot match")
        assert_equal(result["last_output"], raw_output, "final result retains all health evidence")
        assert_equal("".join(transcript), "\n>>> status\n" + raw_output,
                     "final transcript retains all serial chunks")
        if expected_status == "fail" and expected_match:
            assert_equal(result["failure_tokens"], ["I2C_TIMEOUT"],
                         "retained error in the last line is classified")


def test_late_serial_response_cannot_pass_an_expired_deadline() -> None:
    class Clock:
        now = 0.0

        def monotonic(self):
            return self.now

    class LateSerial:
        timeout = 0.1

        def __init__(self, clock, delay):
            self.clock = clock
            self.delay = delay

        def read(self, _size):
            self.clock.now += self.delay
            return b"SCD41 version=1.3.2\n"

    version_step = next(step for step in hil.SAFE_STEPS if step.command == "version")
    for delay, deadline, idle, expected_match in (
        (0.05, 1.0, 0.1, True),
        (0.2, 1.0, 0.1, False),
        (1.0, 1.0, 2.0, False),
    ):
        clock = Clock()
        serial = LateSerial(clock, delay)
        transcript = []
        with mock.patch.object(hil.time, "monotonic", clock.monotonic):
            matched, output = hil.read_until_match(
                serial, hil.re.compile(version_step.expect), deadline, idle, transcript, version_step)
        assert_equal(matched, expected_match, "complete response must arrive before both deadlines")
        assert_equal("".join(transcript), output, "late response remains available as failure evidence")
        assert_true("SCD41 version=" in output, "timeout cannot discard the received bytes")
        assert_equal(serial.timeout, 0.1, "bounded read timeout is restored")
    serial = LateSerial(Clock(), 0)
    serial.read = mock.Mock(side_effect=OSError("serial disconnected"))
    try:
        with mock.patch.object(hil.time, "monotonic", return_value=0.0):
            hil.read_until_match(serial, hil.re.compile(version_step.expect), 0.01, 0.01, [], version_step)
    except OSError:
        pass
    else:
        raise AssertionError("serial disconnect was swallowed")
    assert_equal(serial.timeout, 0.1, "read exception also restores the serial timeout")


def test_environment_metadata_distinguishes_clean_checkout() -> None:
    original_git_text = hil.git_text
    original_command_text = hil.command_text

    def fake_git_text(args, default, *, allow_empty=False):
        if args == ["status", "--short"]:
            assert_true(allow_empty, "clean status query permits empty output")
            return ""
        if args == ["branch", "--show-current"]:
            return "main"
        if args == ["rev-parse", "HEAD"]:
            return "0123456789abcdef"
        return default

    def fake_command_text(command, default, *, allow_empty=False):
        del command, default, allow_empty
        return "PlatformIO Core, version test"

    hil.git_text = fake_git_text
    hil.command_text = fake_command_text
    try:
        metadata = hil.environment_metadata("python runner.py --dry-run")
    finally:
        hil.git_text = original_git_text
        hil.command_text = original_command_text

    assert_equal(metadata["repository_status"], "clean", "clean checkout metadata")
    assert_equal(metadata["repository_branch"], "main", "branch metadata")
    assert_equal(
        metadata["repository_commit"], "0123456789abcdef", "commit metadata"
    )
    assert_equal(
        metadata["platformio_version"],
        "PlatformIO Core, version test",
        "PlatformIO metadata",
    )


def test_parser_self_test_mode() -> None:
    hil.run_parser_self_test()


def test_dry_run_writes_not_run_summary_without_pyserial() -> None:
    with tempfile.TemporaryDirectory(prefix="scd41-hil-dry-run-") as tmp:
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            result = hil.main(["--dry-run", "--port", "COM8", "--output-dir", tmp])
        assert_equal(result, 0, "dry-run exits successfully")
        reports = sorted(pathlib.Path(tmp).glob("*.md"))
        summaries = sorted(pathlib.Path(tmp).glob("*.json"))
        transcripts = sorted(pathlib.Path(tmp).glob("*.log"))
        assert_equal(len(reports), 1, "dry-run writes markdown report")
        assert_equal(len(summaries), 1, "dry-run writes json summary")
        assert_equal(len(transcripts), 1, "dry-run writes transcript")
        report_text = reports[0].read_text(encoding="utf-8")
        assert_true("not-run" in report_text, "dry-run report marks steps not-run")
        assert_true("Include configuration writes: `False`" in report_text, "Markdown identifies optional write scope")
        assert_true("Requested soak: `0`" in report_text, "Markdown identifies optional soak scope")
        assert_true("Manual hardware gates (the runner does not perform these):" in report_text,
                    "Markdown does not hide the unperformed hardware gates")
        for gate in ("fault injection", "shared bus latency", "power cycle persistence", "forced recalibration", "accuracy", "clock wrap"):
            assert_true(f"- {gate}: `not-run`" in report_text, f"Markdown retains the {gate} evidence gap")
        summary = json.loads(summaries[0].read_text(encoding="utf-8"))
        for field in (
            "repository_branch",
            "repository_commit",
            "repository_status",
            "invocation",
            "operating_system",
            "python_version",
            "platformio_version",
        ):
            assert_true(bool(summary.get(field)), f"dry-run records {field}")
        assert_true(
            "scd41_hil_runner.py --dry-run" in summary["invocation"],
            "dry-run records the exact runner invocation",
        )
        assert_equal(summary["serial_commands_attempted"], 0, "dry run is never hardware evidence")
        assert_true(all(value == "not-run" for value in summary["manual_gates"].values()), "manual gates are never inferred")


def test_correlated_complete_results_and_semantic_rejections() -> None:
    step = hil.SAFE_STEPS[3]
    valid = operation_output()
    for output in (
        valid.replace("result request=1", "result request=2"),
        valid.replace("result request=1 generation=1", "result request=1 generation=2"),
        valid.replace("started request=1 generation=1 op=ATTACH", "started request=1 generation=1 op=WAKE_UP"),
        valid[valid.index("result "):],
        valid.replace("timing started=100", "timing started=21000"),
        valid.replace("deadline=20000 phase=", "deadline=19999 phase="),
        valid.replace("identity valid=yes", "identity valid=no"),
        valid.replace("serial=0x100123456789", "serial=0x000000000000"),
        valid.replace("variant_word=0x1000", "variant_word=0x0000"),
        valid.replace("variant_word=0x1000", "variant_word=0x11000"),
        valid.replace("variant_word=0x1000 epoch=1", "variant_word=0x1000 epoch=2"),
        valid.replace("epoch=1", "epoch=0"),
        valid.replace("epoch=1", "epoch=4294967296"),
        valid.replace("reconcile=no", "reconcile=yes"),
        valid.replace("callbacks=6", "callbacks=65000"),
    ):
        result, _ = capture(step, output)
        assert_equal(result["status"], "fail", "contradictory operation evidence rejected")
    state = {}
    first, _ = capture(step, valid, state)
    replay, _ = capture(step, valid, state)
    assert_equal(first["status"], "pass", "first operation is accepted")
    assert_equal(replay["status"], "fail", "replayed request/generation rejected")
    for output in (valid[:valid.index("timing ")], valid.rstrip("\n")):
        result, _ = capture(step, output)
        assert_equal(result["status"], "fail", "truncated payload never passes")
        assert_false(result["matched"], "incomplete response remains unmatched")
    # Success prefix must not hide failure in later serial chunks of the line.
    suffix_error = valid.replace("status=OK", "status=I2C_TIMEOUT")
    result, transcript = capture(step, suffix_error, chunk_size=1)
    assert_equal(result["status"], "fail", "later terminal status cannot be dropped")
    assert_true("I2C_TIMEOUT" in "".join(transcript), "late failure retained in evidence")


def test_health_counters_and_help_aliases() -> None:
    for field in hil.HEALTH_FAILURE_FIELDS:
        result, _ = capture(hil.SAFE_STEPS[-1], HEALTH_OUTPUT.replace(f"{field}=0", f"{field}=1"))
        assert_equal(result["status"], "fail", f"nonzero {field} cannot pass")
    for altered in (HEALTH_OUTPUT.replace("reconcile=no", "reconcile=yes"),
                    HEALTH_OUTPUT.replace("slot state=IDLE", "slot state=ACTIVE")):
        result, _ = capture(hil.SAFE_STEPS[-1], altered)
        assert_equal(result["status"], "fail", "runtime contradiction rejected")
    assert_true(hil.help_mentions_command("attach / recover  Reconcile\n", "recover"), "real CLI aliases are recognized")
    assert_false(hil.help_mentions_command("attach  Use recover after failure\n", "recover"), "help prose is not a command declaration")


def sample_output(sequence=1, request=1, captured=5100, flags=15, mode="IDLE", epoch=1):
    payload = f"sample seq={sequence} epoch={epoch} mode={mode} co2=600 temp_mC=25000 rh_mPct=50000 flags=0x{flags:04X} at={captured}\n"
    return operation_output("SINGLE_SHOT", request=request, payload=payload, duration=captured - 100, epoch=epoch)


def test_sample_freshness_validity_and_ranges() -> None:
    step = next(step for step in hil.SAFE_STEPS if step.command == "single full")
    state = {}
    result, _ = capture(step, sample_output(), state)
    assert_equal(result["status"], "pass", "fresh full sample passes")
    cached = sample_output().split("sample seq=")[1]
    result, _ = capture(hil.Step("cached", "sample", r"sample seq="), "sample seq=" + cached, state)
    assert_equal(result["status"], "pass", "cached sample must equal the previously validated sample")
    for output in (
        sample_output(request=2),
        sample_output(request=2, sequence=2).replace("at=5100", "at=5099"),
        sample_output(request=2, sequence=2).replace("at=5100", "at=4294972396"),
        sample_output(request=2, sequence=2).replace("at=5100", "at=-4294962196"),
        sample_output(request=2, sequence=2, flags=7),
        sample_output(request=2, sequence=2).replace("co2=600", "co2=0"),
        sample_output(request=2, sequence=2).replace("co2=600", "co2=50000"),
        sample_output(request=2, sequence=2).replace("temp_mC=25000", "temp_mC=130001"),
        sample_output(request=2, sequence=2).replace("rh_mPct=50000", "rh_mPct=100001"),
        sample_output(request=2, sequence=2, mode="PERIODIC"),
        sample_output(request=2, sequence=2).replace("sample seq=2 epoch=1", "sample seq=2 epoch=2"),
    ):
        result, _ = capture(step, output, {"last_sample": state["last_sample"]})
        assert_equal(result["status"], "fail", "stale, invalid or wrongly attributed sample rejected")
    result, _ = capture(step, sample_output(sequence=2, request=2, captured=10300), state)
    assert_equal(result["status"], "pass", "later sequence and timestamp pass")
    assert_equal(result["evidence"]["sample_interval_ms"], 5200, "actual device cadence is retained")
    rht_step = next(step for step in hil.SAFE_STEPS if step.command == "single rht")
    for flags, expected in ((14, "pass"), (15, "fail")):
        output = sample_output(flags=flags).replace("SINGLE_SHOT", "SINGLE_SHOT_RHT_ONLY")
        result, _ = capture(rht_step, output)
        assert_equal(result["status"], expected, "RHT-only CO2 validity is enforced")


def test_configuration_sweep_and_soak_plans() -> None:
    args = hil.parse_args(["--dry-run", "--include-config-writes", "--soak-samples", "21", "--soak-mode", "low-power"])
    hil.validate_args(args)
    steps = hil.build_steps(args)
    assert_equal(sum(step.name.startswith("soak sample") for step in steps), 21, "bounded requested soak count")
    assert_true(all(step.settle_s >= 30 for step in steps if step.name.startswith("soak sample")), "LP soak waits for physical conversion")
    assert_equal(sum(step.group == "configuration" for step in steps), 17, "seven setters have a restore, final source selection and baseline/final read")
    assert_false(any(step.destructive for step in steps), "volatile writes and soak never add EEPROM commands")
    state = {}
    baseline = hil.configuration_steps()[0]
    result, _ = capture(baseline, operation_output("READ_CONFIGURATION", payload=CONFIG_OUTPUT), state)
    assert_equal(result["status"], "pass", "verified sweep baseline captured")
    assert_equal(state["restore_asc"], 1, "boolean restoration is numeric CLI form")
    assert_equal(state["alternate_target_ppm"], 450, "alternate differs even when baseline equals first test value")
    step = hil.configuration_steps()[1]
    changed = CONFIG_OUTPUT.replace("offset_mC=4000", "offset_mC=1000").replace("dirty=0x0000", "dirty=0x0001")
    result, _ = capture(step, operation_output("SET_TEMPERATURE_OFFSET", request=2, payload="value signed=1000\n" + changed), state)
    assert_equal(result["status"], "pass", "setter verifies requested readback")
    result, _ = capture(step, operation_output("SET_TEMPERATURE_OFFSET", request=3, payload=CONFIG_OUTPUT), state)
    assert_equal(result["status"], "fail", "no-change readback cannot pretend to change requested value")
    result, _ = capture(baseline, operation_output("READ_CONFIGURATION", payload=CONFIG_OUTPUT.replace("dirty=0x0000", "dirty=0x0004")))
    assert_equal(result["status"], "fail", "runtime pressure must never create EEPROM dirtiness")


def test_workflow_sequence_payload_and_callback_contracts() -> None:
    step = next(step for step in hil.SAFE_STEPS if step.command == "selfcheck")
    summary = "workflow name=selfcheck progress=4/4 cycles=1/1 pass=4 warn=0 fail=0\nworkflow_summary name=selfcheck outcome=PASS\n"
    valid = (operation_output("READ_IDENTITY", request=1)
             + operation_output("READ_SENSOR_VARIANT", request=2)
             + operation_output("READ_CONFIGURATION", request=3, payload=CONFIG_OUTPUT)
             + operation_output("SELF_TEST", request=4, payload="selftest raw=0x0000\n", duration=10000) + summary)
    result, _ = capture(step, valid)
    assert_equal(result["status"], "pass", "complete real selfcheck sequence passes")
    wrong_sequence = "".join(operation_output("READ_DATA_READY", request=index + 1, payload="data_ready=yes raw=0x0001\n") for index in range(4)) + summary
    for output in (wrong_sequence, valid.replace("selftest raw=0x0000\n", ""),
                   valid.replace("selftest raw=0x0000", "selftest raw=0x0001"),
                   valid.replace("verified=0x007F", "verified=0x0001")):
        result, _ = capture(step, output)
        assert_equal(result["status"], "fail", "workflow cannot substitute operations or omit verified payloads")
    attach = hil.SAFE_STEPS[3]
    for output in (operation_output().replace("callbacks=6", "callbacks=0"),
                   operation_output().replace("callbacks=6", "callbacks=7"),
                   operation_output(duration=532), operation_output(duration=19900)):
        result, _ = capture(attach, output)
        assert_equal(result["status"], "fail", "successful attach requires exact attempts, waits and exclusive deadline")
    for kind, command, minimum, payload in (
        ("WAKE_UP", "wake", 33, None), ("REINIT", "reinit", 33, None),
        ("PERSIST_SETTINGS", "persist confirm", 800, CONFIG_OUTPUT),
        ("FACTORY_RESET", "factory_reset confirm", 1203, CONFIG_OUTPUT),
    ):
        command_step = hil.Step("minimum wait", command, rf"op={kind} outcome=SUCCEEDED")
        result, _ = capture(command_step, operation_output(kind, payload=payload, duration=minimum - 1))
        assert_equal(result["status"], "fail", "maintenance cannot skip mandatory settle")
    persist_step = hil.Step("no-op persist", "persist confirm", r"op=PERSIST_SETTINGS outcome=SUCCEEDED")
    no_op = operation_output("PERSIST_SETTINGS", payload=CONFIG_OUTPUT, duration=0).replace("callbacks=1", "callbacks=0").replace("effect=VERIFIED", "effect=NOT_ATTEMPTED")
    result, _ = capture(persist_step, no_op)
    assert_equal(result["status"], "pass", "zero-write persistence is explicitly distinguished")
    result, _ = capture(persist_step, no_op.replace("effect=NOT_ATTEMPTED", "effect=ACKNOWLEDGED"))
    assert_equal(result["status"], "fail", "zero callbacks cannot prove EEPROM acknowledgement")
    reset_step = hil.Step("factory identity proof", "factory_reset confirm", r"op=FACTORY_RESET outcome=SUCCEEDED")
    result, _ = capture(reset_step, operation_output("FACTORY_RESET", payload=CONFIG_OUTPUT, duration=1203))
    assert_equal(result["status"], "fail", "factory reset requires identity as well as configuration payload")


def test_input_bounds_stale_input_and_short_writes() -> None:
    for arguments in (("--timeout-s", "nan"), ("--timeout-s", "inf"), ("--read-timeout", "2"),
                      ("--idle-timeout-s", "nan"), ("--boot-settle-s", "inf"), ("--soak-samples", "10001"),
                      ("--skip-safe",), ("--baud", "0")):
        try:
            hil.validate_args(hil.parse_args(["--dry-run", *arguments]))
        except hil.RunnerError:
            pass
        else:
            raise AssertionError(f"unbounded/invalid option accepted: {arguments}")
    serial = ChunkSerial([])
    serial.write = lambda payload: len(payload) - 1
    try:
        hil.run_step(serial, hil.replace(hil.SAFE_STEPS[1], settle_s=0), 1, [])
    except hil.RunnerError as exc:
        assert_true("incomplete" in str(exc), "short write reported")
    else:
        raise AssertionError("short serial write was accepted")
    class NoisySerial:
        in_waiting = 512
        def read(self, size):
            return b"x" * size
    for boot in (False, True):
        transcript = []
        try:
            with mock.patch.object(hil.time, "monotonic", side_effect=itertools.count(0, 0.001)):
                hil.drain_pending(NoisySerial(), transcript, boot=boot)
        except hil.RunnerError:
            pass
        else:
            raise AssertionError("continuous startup noise did not terminate")
        assert_true(sum(map(len, transcript)) <= hil.MAX_BOOT_BYTES, "boot capture has finite memory")
    class StaleSerial:
        pending = b"result request=1 generation=1 op=ATTACH outcome=SUCCEEDED\n"
        @property
        def in_waiting(self):
            return len(self.pending)
        def read(self, size):
            chunk, self.pending = self.pending[:size], self.pending[size:]
            return chunk
    try:
        hil.drain_pending(StaleSerial(), [])
    except hil.RunnerError as exc:
        assert_true("stale" in str(exc), "pending stale result rejected before writing command")
    else:
        raise AssertionError("stale serial output was ignored")


def test_live_failure_and_interruption_preserve_complete_plan() -> None:
    for failure in (hil.RunnerError("pyserial missing"), KeyboardInterrupt()):
        with tempfile.TemporaryDirectory(prefix="scd41-hil-failure-") as tmp:
            with contextlib.redirect_stdout(io.StringIO()), mock.patch.object(hil, "load_serial_module", side_effect=failure):
                result = hil.main(["--port", "COM8", "--output-dir", tmp])
            assert_equal(result, 1, "live runner error cannot exit successfully")
            summary = json.loads(next(pathlib.Path(tmp).glob("*.json")).read_text(encoding="utf-8"))
            assert_equal(summary["status"], "fail", "setup interruption produces failed evidence")
            assert_equal(summary["counts"]["not-run"], len(hil.SAFE_STEPS), "unexecuted gates retained after failure")
            assert_equal(summary["serial_commands_attempted"], 0, "setup failure cannot claim hardware was exercised")


def test_entire_runner_plans_with_serial_fixture() -> None:
    """Exercise orchestration against CLI-shaped records, without hardware."""
    class FirmwareSerial:
        timeout = 0.1
        in_waiting = 0

        def __init__(self, *_args, **_kwargs):
            self.pending = b""
            self.request = 0
            self.now = 100
            self.sequence = 0
            self.mode = "IDLE"
            self.configuration = CONFIG_OUTPUT
            self.sample = ""

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            pass

        def read(self, size):
            chunk, self.pending = self.pending[:min(43, size)], self.pending[min(43, size):]
            return chunk

        def operation(self, kind):
            self.request += 1
            duration = {"SINGLE_SHOT": 5000, "SINGLE_SHOT_RHT_ONLY": 50, "SELF_TEST": 10000, "STOP_PERIODIC": 500,
                        "ATTACH": 600, "PERSIST_SETTINGS": 800, "FACTORY_RESET": 1203}.get(kind, 100)
            if kind in ("START_PERIODIC", "START_LOW_POWER_PERIODIC", "STOP_PERIODIC", "ATTACH", "WAKE_UP", "POWER_DOWN"):
                self.mode = {"START_PERIODIC": "PERIODIC", "START_LOW_POWER_PERIODIC": "LOW_POWER_PERIODIC", "POWER_DOWN": "POWER_DOWN"}.get(kind, "IDLE")
                self.sequence = 0
            if kind in ("SINGLE_SHOT", "SINGLE_SHOT_RHT_ONLY", "FETCH_SAMPLE"):
                self.sequence += 1
                flags = 14 if kind == "SINGLE_SHOT_RHT_ONLY" else 15
                self.sample = f"sample seq={self.sequence} epoch=1 mode={self.mode} co2=600 temp_mC=25000 rh_mPct=50000 flags=0x{flags:04X} at={self.now + duration}\n"
                payload = self.sample
            elif kind == "READ_CONFIGURATION" or kind == "PERSIST_SETTINGS" or kind.startswith("SET_"):
                payload = self.configuration
                if kind.startswith("SET_"):
                    field = next(entry[0] for entry in hil.CONFIGURATION_FIELDS.values() if kind == "SET_" + entry[1])
                    value = hil.records(self.configuration, "config")[0][1][field]
                    scalar = f"bool={'true' if value == 'on' else 'false'}" if field == "asc" else f"{'signed' if field == 'offset_mC' else 'unsigned'}={value}"
                    payload = f"value {scalar}\n" + payload
            elif kind == "SELF_TEST":
                payload = "selftest raw=0x0000\n"
            elif kind == "READ_DATA_READY":
                payload = "data_ready=no raw=0x8000\n"
            elif kind in ("ATTACH", "READ_IDENTITY", "READ_SENSOR_VARIANT", "WAKE_UP", "FACTORY_RESET", "REINIT"):
                payload = "identity valid=yes serial=0x100123456789 variant=SCD41 variant_word=0x1000 epoch=1\n"
                if kind == "FACTORY_RESET":
                    payload += self.configuration
            else:
                payload = ""
            output = operation_output(kind, request=self.request, payload=payload, duration=duration)
            output = output.replace("deadline=20000", f"deadline={self.now + 20000}")
            output = output.replace(f"timing started=100 completed={100 + duration}", f"timing started={self.now} completed={self.now + duration}")
            self.now += duration + 100
            return output

        def write(self, data):
            assert_false(self.pending, "next command must not outrun prior complete response")
            words = data.decode().strip().split()
            command = words[0]
            if command == "help":
                output = "SCD41 Owner-Safe CLI v2\n" + "\n".join(hil.MINIMUM_SAFE_COMMANDS) + "\ncommand write_word <cmd> <word> confirm\n"
            elif command == "version":
                output = "SCD41 version=7.0.0\n"
            elif command == "scan":
                output = "Found device at 0x62\nFound 1 device(s)\n"
            elif command == "status":
                output = HEALTH_OUTPUT.replace("mode=IDLE", f"mode={self.mode}")
            elif command == "sample":
                output = self.sample
            elif command in ("selfcheck", "stress", "stress_mix"):
                kinds = {"selfcheck": ("READ_IDENTITY", "READ_SENSOR_VARIANT", "READ_CONFIGURATION", "SELF_TEST"),
                         "stress": ("READ_DATA_READY",), "stress_mix": ("READ_IDENTITY", "READ_CONFIGURATION", "READ_SENSOR_VARIANT", "READ_DATA_READY")}[command]
                cycles = 1 if command == "selfcheck" else int(words[1])
                total = cycles * len(kinds)
                output = "".join(self.operation(kind) for kind in kinds * cycles)
                output += f"workflow name={command} progress={total}/{total} cycles={cycles}/{cycles} pass={total} warn=0 fail=0\nworkflow_summary name={command} outcome=PASS\n"
            elif command in hil.CONFIGURATION_FIELDS:
                field, operation, *_ = hil.CONFIGURATION_FIELDS[command]
                value = ("on" if words[1] == "1" else "off") if field == "asc" else words[1]
                self.configuration = hil.re.sub(rf"\b{field}=\S+", f"{field}={value}", self.configuration)
                output = self.operation("SET_" + operation)
            else:
                kind = {"begin": "ATTACH", "recover": "ATTACH", "probe": "READ_IDENTITY", "identity": "READ_IDENTITY", "variant": "READ_SENSOR_VARIANT",
                        "settings": "READ_CONFIGURATION", "dataready": "READ_DATA_READY", "read": "FETCH_SAMPLE", "sleep": "POWER_DOWN", "wake": "WAKE_UP",
                        "persist": "PERSIST_SETTINGS", "factory_reset": "FACTORY_RESET", "reinit": "REINIT"}.get(command)
                if command == "periodic":
                    kind = {"on": "START_PERIODIC", "off": "STOP_PERIODIC", "lp": "START_LOW_POWER_PERIODIC"}[words[1]]
                elif command == "single":
                    kind = "SINGLE_SHOT" if words[1] == "full" else "SINGLE_SHOT_RHT_ONLY"
                assert_true(kind is not None, "fixture covers every planned command")
                output = self.operation(kind)
            self.pending = output.encode()
            return len(data)

    variants = ([], ["--include-config-writes", "--soak-samples", "21"],
                ["--soak-samples", "2", "--soak-mode", "low-power"],
                ["--soak-samples", "2", "--soak-mode", "single", "--include-destructive", "--confirm-destructive", hil.DESTRUCTIVE_CONFIRMATION],
                ["--skip-safe", "--include-destructive", "--confirm-destructive", hil.DESTRUCTIVE_CONFIRMATION])
    for options in variants:
        with tempfile.TemporaryDirectory(prefix="scd41-hil-serial-fixture-") as tmp:
            with contextlib.redirect_stdout(io.StringIO()), \
                    mock.patch.object(hil, "load_serial_module", return_value=types.SimpleNamespace(Serial=FirmwareSerial)), \
                    mock.patch.object(hil.time, "sleep"), \
                    mock.patch.object(hil.time, "monotonic", side_effect=itertools.count(0, 0.0001)):
                result = hil.main(["--port", "FAKE_TEST_PORT", "--output-dir", tmp, *options])
            summary = json.loads(next(pathlib.Path(tmp).glob("*.json")).read_text(encoding="utf-8"))
            failures = [entry for entry in summary["results"] if entry["status"] != "pass"]
            assert_equal(result, 0, f"complete selected command plan passes serial fixture: {failures[:1]}")
            assert_equal(summary["counts"]["not-run"], 0, "every selected operation ran")
            assert_true(summary["serial_commands_attempted"] > 0, "serial attempts are explicit evidence metadata")


def main() -> int:
    tests = [
        test_serial_number_parsing,
        test_missing_serial_import_is_runner_error,
        test_missing_serial_port_is_validation_error,
        test_destructive_confirmation,
        test_safe_step_contract,
        test_help_contract_detection,
        test_failure_token_classification,
        test_step_pattern_matching,
        test_step_pass_rejects_failure_tokens,
        test_live_step_matches_colored_chunks_and_keeps_raw_evidence,
        test_build_steps_timeout_override,
        test_live_selfcheck_allows_sensor_silence_and_keeps_timeouts_bounded,
        test_live_final_status_reads_health_and_complete_error_line,
        test_late_serial_response_cannot_pass_an_expired_deadline,
        test_environment_metadata_distinguishes_clean_checkout,
        test_parser_self_test_mode,
        test_dry_run_writes_not_run_summary_without_pyserial,
        test_correlated_complete_results_and_semantic_rejections,
        test_health_counters_and_help_aliases,
        test_sample_freshness_validity_and_ranges,
        test_configuration_sweep_and_soak_plans,
        test_workflow_sequence_payload_and_callback_contracts,
        test_input_bounds_stale_input_and_short_writes,
        test_live_failure_and_interruption_preserve_complete_plan,
        test_entire_runner_plans_with_serial_fixture,
    ]
    for test in tests:
        test()
    print("SCD41 HIL runner tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
