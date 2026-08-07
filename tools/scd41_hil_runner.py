#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import sys
import time
from dataclasses import dataclass, replace
from typing import Optional


DESTRUCTIVE_CONFIRMATION = "I understand EEPROM and calibration risk"
MINIMUM_SAFE_COMMANDS = (
    "version",
    "scan",
    "begin",
    "probe",
    "recover",
    "identity",
    "variant",
    "settings",
    "selfcheck",
    "stress",
    "stress_mix",
    "status",
)
HEALTH_COMMAND_ALIASES = ("status", "health", "drv")
DEFAULT_IDLE_TIMEOUT_S = 8.0

SERIAL_NUMBER_RE = re.compile(
    r"\bserial(?:_number)?\s*[:=]\s*(?:0x)?([0-9A-Fa-f]{12})\b",
    re.IGNORECASE,
)
FAILURE_TOKEN_RE = re.compile(
    r"\b("
    r"NOT_INITIALIZED|INVALID_CONFIG|INVALID_PARAM|RESULT_NOT_READY|STALE_RESULT|"
    r"CRC_MISMATCH|DEVICE_NOT_FOUND|OFFLINE|I2C_ERROR|I2C_NACK|I2C_TIMEOUT|"
    r"I2C_BUS|I2C_SHORT_TRANSFER|COMMAND_FAILED|UNSUPPORTED|TIMEOUT|CANCELLED|"
    r"PARTIAL|INDETERMINATE|RECONCILIATION_REQUIRED|FAILED|FAILURE"
    r")\b",
    re.IGNORECASE,
)
ANSI_ESCAPE_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
DIAGNOSTIC_FAILURE_RE = re.compile(r"\bfail\s*=\s*([1-9][0-9]*)\b", re.IGNORECASE)
STRESS_ERROR_RE = re.compile(r"\b(?:Errors:|errors=)\s*([1-9][0-9]*)\b", re.IGNORECASE)


@dataclass(frozen=True)
class Step:
    name: str
    command: str
    expect: str
    timeout_s: float = 8.0
    settle_s: float = 0.25
    destructive: bool = False


SAFE_STEPS: tuple[Step, ...] = (
    Step("help surface", "help", r"SCD41 Owner-Safe CLI v2"),
    Step("version", "version", r"SCD41 version=[0-9]+\.[0-9]+\.[0-9]+"),
    Step("i2c scan", "scan", r"Found device at 0x62", timeout_s=12.0),
    Step("bind and attach", "begin", r"op=ATTACH outcome=SUCCEEDED", timeout_s=12.0),
    Step("protocol-qualified probe", "probe", r"op=READ_IDENTITY outcome=SUCCEEDED.*variant=SCD41", timeout_s=12.0),
    Step("explicit attach recovery", "recover", r"op=ATTACH outcome=SUCCEEDED.*variant=SCD41", timeout_s=12.0),
    Step("attached health", "status", r"runtime bound=yes attached=yes state=READY"),
    Step("identity", "identity", r"op=READ_IDENTITY outcome=SUCCEEDED.*serial=0x[0-9A-Fa-f]{12}.*variant=SCD41", timeout_s=12.0),
    Step("sensor variant", "variant", r"op=READ_SENSOR_VARIANT outcome=SUCCEEDED.*variant=SCD41.*variant_word=0x[0-9A-Fa-f]{4}", timeout_s=12.0),
    Step("settings", "settings", r"op=READ_CONFIGURATION outcome=SUCCEEDED.*config offset_mC=", timeout_s=15.0),
    Step("aggregate selfcheck", "selfcheck", r"workflow_summary name=selfcheck outcome=PASS", timeout_s=25.0),
    Step("bounded readiness stress", "stress 5", r"workflow_summary name=stress outcome=PASS", timeout_s=15.0),
    Step("bounded mixed stress", "stress_mix 2", r"workflow_summary name=stress_mix outcome=PASS", timeout_s=15.0),
    Step("dataready idle", "dataready", r"op=READ_DATA_READY outcome=SUCCEEDED.*data_ready=(yes|no)", timeout_s=12.0),
    Step("periodic start", "periodic on", r"op=START_PERIODIC outcome=SUCCEEDED", timeout_s=12.0),
    Step("periodic sample", "read", r"op=FETCH_SAMPLE outcome=SUCCEEDED.*sample seq=", timeout_s=15.0, settle_s=6.0),
    Step("periodic cached sample", "sample", r"sample seq="),
    Step("periodic stop", "periodic off", r"op=STOP_PERIODIC outcome=SUCCEEDED", timeout_s=12.0),
    Step("post-stop status", "status", r"runtime .*mode=IDLE.*operation=NONE", timeout_s=8.0),
    Step("single-shot full", "single full", r"op=SINGLE_SHOT outcome=SUCCEEDED.*sample seq=", timeout_s=15.0),
    Step("single-shot rht", "single rht", r"op=SINGLE_SHOT_RHT_ONLY outcome=SUCCEEDED.*sample seq=", timeout_s=8.0),
    Step("low-power periodic start", "periodic lp", r"op=START_LOW_POWER_PERIODIC outcome=SUCCEEDED", timeout_s=12.0),
    Step("low-power periodic sample", "read", r"op=FETCH_SAMPLE outcome=SUCCEEDED.*sample seq=", timeout_s=45.0, settle_s=31.0),
    Step("low-power periodic stop", "periodic off", r"op=STOP_PERIODIC outcome=SUCCEEDED", timeout_s=12.0),
    Step("power down", "sleep", r"op=POWER_DOWN outcome=SUCCEEDED", timeout_s=12.0),
    Step("wake", "wake", r"op=WAKE_UP outcome=SUCCEEDED", timeout_s=12.0),
    Step("identity after wake", "identity", r"op=READ_IDENTITY outcome=SUCCEEDED.*serial=0x[0-9A-Fa-f]{12}", timeout_s=12.0),
    Step("final driver health", "status", r"runtime bound=yes attached=yes state=READY", timeout_s=12.0),
)


DESTRUCTIVE_STEPS: tuple[Step, ...] = (
    Step("persist dirty settings or confirm no-op", "persist confirm", r"op=PERSIST_SETTINGS outcome=SUCCEEDED", timeout_s=15.0, destructive=True),
    Step("factory reset", "factory_reset confirm", r"op=FACTORY_RESET outcome=SUCCEEDED", timeout_s=15.0, destructive=True),
    Step("post-reset attach", "begin", r"op=ATTACH outcome=SUCCEEDED", timeout_s=15.0, settle_s=2.0, destructive=True),
)


class RunnerError(RuntimeError):
    pass


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Optional SCD41 serial HIL runner. Does not fake hardware results."
    )
    parser.add_argument("--port", default="", help="Serial port, for example COM8 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--output-dir", default="hil-results", help="Directory for transcript and summaries")
    parser.add_argument("--board", default="NOT_RECORDED", help="Board type/revision for the run metadata")
    parser.add_argument("--fixture", default="NOT_RECORDED", help="Sensor fixture, wiring, supply, and pullup notes")
    parser.add_argument("--operator", default="NOT_RECORDED", help="Operator name or initials for the run metadata")
    parser.add_argument("--read-timeout", type=float, default=0.1, help="Per-read serial timeout in seconds")
    parser.add_argument("--timeout-s", type=float, default=None, help="Override every step timeout in seconds")
    parser.add_argument(
        "--idle-timeout-s",
        type=float,
        default=DEFAULT_IDLE_TIMEOUT_S,
        help="Fail a step after this many seconds without serial data",
    )
    parser.add_argument("--include-destructive", action="store_true", help="Enable EEPROM/factory-reset steps; FRC remains manual")
    parser.add_argument(
        "--confirm-destructive",
        default="",
        help=f"Required exact phrase for destructive steps: {DESTRUCTIVE_CONFIRMATION!r}",
    )
    parser.add_argument("--skip-safe", action="store_true", help="Run only destructive steps")
    parser.add_argument(
        "--boot-settle-s",
        "--settle-before",
        dest="settle_before",
        type=float,
        default=2.0,
        help="Initial serial boot/reset settle time",
    )
    parser.add_argument("--verbose", action="store_true", help="Print matched output excerpts and failure tokens")
    parser.add_argument("--dry-run", action="store_true", help="Write a NOT RUN plan without opening serial")
    parser.add_argument("--parser-self-test", action="store_true", help="Run parser checks and exit without serial")
    return parser.parse_args(argv)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RunnerError(message)


def validate_args(args: argparse.Namespace) -> None:
    if args.timeout_s is not None:
        require(args.timeout_s > 0.0, "--timeout-s must be > 0")
    require(args.read_timeout > 0.0, "--read-timeout must be > 0")
    require(args.idle_timeout_s > 0.0, "--idle-timeout-s must be > 0")
    require(args.settle_before >= 0.0, "--boot-settle-s must be >= 0")
    if args.parser_self_test or args.dry_run:
        return
    require(bool(args.port), "--port is required unless --dry-run or --parser-self-test is used")


def load_serial_module():
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RunnerError("pyserial is required: install with `python -m pip install pyserial`") from exc
    return serial


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def strip_ansi(text: str) -> str:
    return ANSI_ESCAPE_RE.sub("", text)


def parse_serial_number(text: str) -> Optional[str]:
    match = SERIAL_NUMBER_RE.search(text)
    if match is None:
        return None
    return match.group(1).upper()


def classify_failure_tokens(text: str) -> tuple[str, ...]:
    clean = strip_ansi(text)
    tokens: list[str] = []
    for match in FAILURE_TOKEN_RE.finditer(clean):
        token = match.group(1).upper()
        if token not in tokens:
            tokens.append(token)
    if DIAGNOSTIC_FAILURE_RE.search(clean) and "FAIL_COUNT" not in tokens:
        tokens.append("FAIL_COUNT")
    if STRESS_ERROR_RE.search(clean) and "ERROR_COUNT" not in tokens:
        tokens.append("ERROR_COUNT")
    return tuple(tokens)


def destructive_confirmation_valid(include_destructive: bool, confirmation: str) -> bool:
    return (not include_destructive) or confirmation == DESTRUCTIVE_CONFIRMATION


def step_output_matches(step: Step, output: str) -> bool:
    return re.search(
        step.expect, strip_ansi(output), re.IGNORECASE | re.DOTALL
    ) is not None


def step_passed(matched: bool, output: str) -> bool:
    return matched and not classify_failure_tokens(output)


def _command_from_step(step: Step) -> str:
    return step.command.strip().split()[0]


def missing_minimum_safe_steps(steps: tuple[Step, ...]) -> tuple[str, ...]:
    commands = {_command_from_step(step) for step in steps}
    missing = [command for command in MINIMUM_SAFE_COMMANDS if command not in commands]
    if not any(command in commands for command in HEALTH_COMMAND_ALIASES):
        missing.append("health|drv|state")
    return tuple(missing)


def help_mentions_command(help_text: str, command: str) -> bool:
    clean = strip_ansi(help_text)
    pattern = rf"(^|\n)\s*{re.escape(command)}(?:\s|/|$)"
    return re.search(pattern, clean, re.IGNORECASE) is not None


def missing_minimum_help_commands(help_text: str) -> tuple[str, ...]:
    missing = [
        command
        for command in MINIMUM_SAFE_COMMANDS
        if not help_mentions_command(help_text, command)
    ]
    if not any(help_mentions_command(help_text, command) for command in HEALTH_COMMAND_ALIASES):
        missing.append("health|drv|state")
    return tuple(missing)


def build_steps(args: argparse.Namespace) -> list[Step]:
    steps: list[Step] = []
    if not args.skip_safe:
        steps.extend(SAFE_STEPS)
    if args.include_destructive:
        steps.extend(DESTRUCTIVE_STEPS)
    if args.timeout_s is not None:
        steps = [replace(step, timeout_s=args.timeout_s) for step in steps]
    return steps


def run_parser_self_test() -> None:
    require(parse_serial_number("serial=0x100123456789") == "100123456789", "serial parser rejected hex form")
    require(parse_serial_number("serial_number: 100ABCDEF012") == "100ABCDEF012", "serial parser rejected plain form")
    require(parse_serial_number("serial=0x1234") is None, "serial parser accepted short serial")
    require(missing_minimum_safe_steps(SAFE_STEPS) == (), "safe HIL step contract is incomplete")
    help_text = (
        "version\nscan\nbegin\nprobe\nrecover\nidentity\nvariant\nsettings\n"
        "selfcheck\nstress\nstress_mix\nstatus\n"
    )
    require(missing_minimum_help_commands(help_text) == (), "help command contract parser failed")
    require(
        classify_failure_tokens("Status: I2C_TIMEOUT; fail=1; errors=2")
        == ("I2C_TIMEOUT", "FAIL_COUNT", "ERROR_COUNT"),
        "failure-token classifier failed",
    )
    require(step_passed(True, "Status: OK\nState: READY"), "OK step classification failed")
    require(not step_passed(True, "Status: I2C_TIMEOUT"), "failure step classification failed")


def read_until_match(
    ser,
    pattern: re.Pattern[str],
    timeout_s: float,
    idle_timeout_s: float,
    transcript: list[str],
) -> tuple[bool, str]:
    deadline = time.monotonic() + timeout_s
    idle_deadline = time.monotonic() + idle_timeout_s
    buffer = ""
    while time.monotonic() < deadline:
        chunk = ser.read(512)
        if chunk:
            text = chunk.decode("utf-8", errors="replace")
            transcript.append(text)
            buffer += text
            idle_deadline = time.monotonic() + idle_timeout_s
            if pattern.search(buffer):
                return True, buffer
        else:
            if time.monotonic() >= idle_deadline:
                return False, buffer
            time.sleep(0.02)
    return False, buffer


def run_step(ser, step: Step, idle_timeout_s: float, transcript: list[str]) -> dict[str, object]:
    if step.settle_s > 0:
        time.sleep(step.settle_s)
    command_line = f"{step.command}\n"
    transcript.append(f"\n>>> {step.command}\n")
    ser.write(command_line.encode("utf-8"))
    ser.flush()

    started = time.monotonic()
    matched, output = read_until_match(
        ser,
        re.compile(step.expect, re.DOTALL),
        step.timeout_s,
        idle_timeout_s,
        transcript,
    )
    elapsed_s = time.monotonic() - started
    passed = step_passed(matched, output)
    return {
        "name": step.name,
        "command": step.command,
        "destructive": step.destructive,
        "expect": step.expect,
        "matched": matched,
        "failure_tokens": list(classify_failure_tokens(output)),
        "elapsed_s": round(elapsed_s, 3),
        "status": "pass" if passed else "fail",
        "last_output": output[-1200:],
    }


def not_run_result(step: Step, reason: str) -> dict[str, object]:
    return {
        "name": step.name,
        "command": step.command,
        "destructive": step.destructive,
        "expect": step.expect,
        "matched": False,
        "failure_tokens": [],
        "elapsed_s": 0,
        "status": "not-run",
        "last_output": reason,
    }


def result_counts(results: list[dict[str, object]]) -> dict[str, int]:
    counts = {"pass": 0, "fail": 0, "unknown": 0, "not-run": 0}
    for result in results:
        status = str(result.get("status", "unknown"))
        counts[status if status in counts else "unknown"] += 1
    return counts


def write_markdown(path: pathlib.Path, summary: dict[str, object]) -> None:
    counts = summary["counts"]  # type: ignore[index]
    lines = [
        "# SCD41 HIL Run Summary",
        "",
        f"- Timestamp UTC: `{summary['timestamp_utc']}`",
        f"- Port: `{summary['port']}`",
        f"- Baud: `{summary['baud']}`",
        f"- Board: `{summary['board']}`",
        f"- Fixture: `{summary['fixture']}`",
        f"- Operator: `{summary['operator']}`",
        f"- Include destructive: `{summary['include_destructive']}`",
        f"- Overall status: `{summary['status']}`",
        f"- Result counts: pass `{counts['pass']}`, fail `{counts['fail']}`, unknown `{counts['unknown']}`, not-run `{counts['not-run']}`",
        "",
        "| Step | Command | Expected | Destructive | Status | Elapsed s | Failure tokens |",
        "| --- | --- | --- | --- | --- | ---: | --- |",
    ]
    for result in summary["results"]:  # type: ignore[index]
        lines.append(
            "| {name} | `{command}` | `{expect}` | {destructive} | {status} | {elapsed_s} | {failure_tokens} |".format(
                **{
                    **result,
                    "failure_tokens": ", ".join(result.get("failure_tokens", [])) or "-",
                }
            )
        )
    lines.extend(
        [
            "",
            f"Raw transcript: `{summary['transcript_path']}`",
            "",
            "A passing runner result is hardware evidence only for the connected board/sensor, firmware, wiring, and environment recorded in the transcript.",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_summary_files(
    args: argparse.Namespace,
    run_id: str,
    output_dir: pathlib.Path,
    transcript: list[str],
    results: list[dict[str, object]],
    status: str,
) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    transcript_path = output_dir / f"scd41_hil_{run_id}.log"
    json_path = output_dir / f"scd41_hil_{run_id}.json"
    md_path = output_dir / f"scd41_hil_{run_id}.md"

    transcript_path.write_text("".join(transcript), encoding="utf-8")
    summary: dict[str, object] = {
        "timestamp_utc": run_id,
        "port": args.port or "NOT_SET",
        "baud": args.baud,
        "board": args.board,
        "fixture": args.fixture,
        "operator": args.operator,
        "include_destructive": args.include_destructive,
        "status": status,
        "counts": result_counts(results),
        "transcript_path": str(transcript_path),
        "results": results,
    }
    json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    write_markdown(md_path, summary)
    return transcript_path, json_path, md_path


def main(argv: Optional[list[str]] = None) -> int:
    args = parse_args(argv)
    try:
        validate_args(args)
        if not destructive_confirmation_valid(args.include_destructive, args.confirm_destructive):
            raise RunnerError(
                "destructive HIL steps refused: confirmation phrase does not match; "
                f"required {DESTRUCTIVE_CONFIRMATION!r}"
            )

        missing_safe_steps = missing_minimum_safe_steps(SAFE_STEPS)
        if missing_safe_steps:
            raise RunnerError(
                "SCD41 HIL runner safe-step contract is incomplete: "
                + ", ".join(missing_safe_steps)
            )
        if args.parser_self_test:
            run_parser_self_test()
            print("SCD41 HIL runner parser self-test PASSED")
            return 0
    except RunnerError as exc:
        print(f"SCD41 HIL runner FAILED: {exc}")
        return 2

    output_dir = pathlib.Path(args.output_dir)
    run_id = timestamp()

    steps = build_steps(args)
    if not steps:
        print("No steps selected.")
        return 2

    if args.dry_run:
        transcript = [
            f"# SCD41 HIL dry-run plan {run_id}\n",
            "# No serial port was opened and no hardware evidence was produced.\n",
        ]
        results = [not_run_result(step, "dry-run: no serial command executed") for step in steps]
        transcript_path, json_path, md_path = write_summary_files(
            args, run_id, output_dir, transcript, results, "not-run"
        )
        print(f"Dry-run plan: {json_path}")
        print(f"Report: {md_path}")
        print(f"Transcript: {transcript_path}")
        return 0

    try:
        serial = load_serial_module()
    except RunnerError as exc:
        print(f"SCD41 HIL runner FAILED: {exc}")
        return 2

    transcript: list[str] = []
    results: list[dict[str, object]] = []
    status = "pass"

    try:
        with serial.Serial(args.port, args.baud, timeout=args.read_timeout, write_timeout=2.0) as ser:
            time.sleep(args.settle_before)
            transcript.append(f"# SCD41 HIL transcript {run_id}\n")
            transcript.append(f"# port={args.port} baud={args.baud}\n")
            transcript.append(f"# board={args.board}\n")
            transcript.append(f"# fixture={args.fixture}\n")
            transcript.append(f"# operator={args.operator}\n")
            while ser.in_waiting:
                transcript.append(ser.read(ser.in_waiting).decode("utf-8", errors="replace"))

            for step in steps:
                result = run_step(ser, step, args.idle_timeout_s, transcript)
                results.append(result)
                print(f"{result['status']}: {step.name} [{step.command}]")
                if args.verbose:
                    print(f"  elapsed_s={result['elapsed_s']} failure_tokens={result['failure_tokens']}")
                    excerpt = strip_ansi(str(result["last_output"]))[-300:].strip()
                    if excerpt:
                        print(f"  output: {excerpt}")
                if result["status"] != "pass":
                    status = "fail"
                    break
    except Exception as exc:
        status = "fail"
        results.append({
            "name": "runner exception",
            "command": "",
            "destructive": False,
            "expect": "",
            "matched": False,
            "elapsed_s": 0,
            "status": "fail",
            "last_output": str(exc),
        })

    transcript_path, json_path, md_path = write_summary_files(
        args, run_id, output_dir, transcript, results, status
    )

    print(f"Summary: {json_path}")
    print(f"Report: {md_path}")
    print(f"Transcript: {transcript_path}")
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
