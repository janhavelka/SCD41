#!/usr/bin/env python3
from __future__ import annotations

import builtins
import contextlib
import io
import pathlib
import sys
import tempfile


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
    identity       Read sensor identity
    variant        Read sensor variant
    settings       Read settings
    status         Print driver health
    """
    assert_equal(hil.missing_minimum_help_commands(help_text), (), "help covers common commands")
    assert_equal(
        hil.missing_minimum_help_commands("scan\nbegin\nidentity\nvariant\nsettings\nstatus\n"),
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


def test_build_steps_timeout_override() -> None:
    args = hil.parse_args(["--dry-run", "--timeout-s", "3"])
    hil.validate_args(args)
    steps = hil.build_steps(args)
    assert_true(all(step.timeout_s == 3 for step in steps), "timeout override applies to all steps")


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
        test_build_steps_timeout_override,
        test_parser_self_test_mode,
        test_dry_run_writes_not_run_summary_without_pyserial,
    ]
    for test in tests:
        test()
    print("SCD41 HIL runner tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
