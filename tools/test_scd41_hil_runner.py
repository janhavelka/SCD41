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
        assert_equal(hil.classify_failure_tokens(f"health cancelled={count}"), ("CANCELLED",),
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
    class SerialChunks:
        def __init__(self, chunks):
            self.chunks = iter(chunks)
            self.written = []
            self.flushes = 0

        def write(self, data):
            self.written.append(data)

        def flush(self):
            self.flushes += 1

        def read(self, size):
            chunk = next(self.chunks, b"")
            assert_true(len(chunk) <= size, "serial chunk respects requested size")
            return chunk

    step = hil.Step("attach", "begin", r"op=ATTACH outcome=SUCCEEDED", timeout_s=1)
    for prefix, expected_status in (
        (b"", "pass"),
        (b"Status: \x1b[31mI2C_TIMEOUT\x1b[0m\n", "fail"),
    ):
        # Split both an ANSI escape and the matching token across reads. This
        # exercises the live reader, not only the parser helper used by CI.
        chunks = [prefix + b"op=\x1b[3", b"2mAtTaCh\x1b[0m outcome=\x1b[32mSUC",
                  b"CEEDED\x1b[0m\n"]
        serial = SerialChunks(chunks)
        transcript = []
        with mock.patch.object(hil.time, "monotonic", side_effect=itertools.count(0, 0.01)), \
                mock.patch.object(hil.time, "sleep"):
            result = hil.run_step(serial, step, 0.1, transcript)
        raw_output = b"".join(chunks).decode("utf-8")
        assert_true(result["matched"], "live matching strips ANSI and ignores case")
        assert_equal(result["status"], expected_status, "live failure classification")
        assert_equal(result["last_output"], raw_output, "result preserves raw output")
        assert_equal("".join(transcript), "\n>>> begin\n" + raw_output,
                     "transcript preserves raw serial evidence")
        assert_equal(serial.written, [b"begin\n"], "one command is written")
        assert_equal(serial.flushes, 1, "command is flushed once")


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

        def flush(self):
            pass

        def read(self, size):
            del size
            if self.admission_pending:
                self.admission_pending = False
                return b"accepted op=SELF_TEST\n"
            if self.completes and self.clock.now - self.started >= 10.0:
                return b"workflow_summary name=selfcheck outcome=\x1b[32mPASS\x1b[0m\n"
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

        def flush(self):
            pass

        def read(self, size):
            chunk = next(self.chunks, b"")
            assert_true(len(chunk) <= size, "serial chunk respects requested size")
            return chunk

    step = next(step for step in hil.SAFE_STEPS if step.name == "final driver health")
    runtime = b"runtime bound=yes attached=yes state=\x1b[32mREADY\x1b[0m mode=IDLE\r\n"
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
        test_environment_metadata_distinguishes_clean_checkout,
        test_parser_self_test_mode,
        test_dry_run_writes_not_run_summary_without_pyserial,
    ]
    for test in tests:
        test()
    print("SCD41 HIL runner tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
