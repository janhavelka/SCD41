#!/usr/bin/env python3
from __future__ import annotations

import builtins
import contextlib
import io
import pathlib
import sys


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


def test_missing_serial_port_is_argparse_error() -> None:
    stderr = io.StringIO()
    with contextlib.redirect_stderr(stderr):
        try:
            hil.parse_args([])
        except SystemExit as exc:
            assert_equal(exc.code, 2, "missing port exits with argparse error")
        else:
            raise AssertionError("parse_args([]) unexpectedly succeeded")
    assert_true("--port" in stderr.getvalue(), "missing port mentions --port")


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
    probe          Probe SCD41 without updating health
    settings       Read settings
    drv            Print driver health
    """
    assert_equal(hil.missing_minimum_help_commands(help_text), (), "help covers common commands")
    assert_equal(
        hil.missing_minimum_help_commands("scan\nprobe\nsettings\ndrv\n"),
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


def main() -> int:
    tests = [
        test_serial_number_parsing,
        test_missing_serial_import_is_runner_error,
        test_missing_serial_port_is_argparse_error,
        test_destructive_confirmation,
        test_safe_step_contract,
        test_help_contract_detection,
        test_failure_token_classification,
        test_step_pattern_matching,
        test_step_pass_rejects_failure_tokens,
    ]
    for test in tests:
        test()
    print("SCD41 HIL runner tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
