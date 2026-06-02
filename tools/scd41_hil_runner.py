#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import sys
import time
from dataclasses import dataclass


DESTRUCTIVE_CONFIRMATION = "I understand EEPROM and calibration risk"


@dataclass(frozen=True)
class Step:
    name: str
    command: str
    expect: str
    timeout_s: float = 8.0
    settle_s: float = 0.25
    destructive: bool = False


SAFE_STEPS: tuple[Step, ...] = (
    Step("help surface", "help", r"SCD41 CLI Help|Common"),
    Step("i2c scan", "scan", r"0x62|SCD41|found|FOUND|device", timeout_s=12.0),
    Step("begin", "begin", r"OK|initialized|Device initialized", timeout_s=12.0),
    Step("serial", "serial", r"serial=0x[0-9A-Fa-f]{12}"),
    Step("variant", "variant", r"variant=SCD41"),
    Step("dataready idle", "dataready", r"data_ready=(yes|no)|Status:"),
    Step("diagnostics", "diag", r"Diagnostics:.*fail=.*0|Diagnostics:", timeout_s=15.0),
    Step("periodic start", "periodic on", r"IN_PROGRESS|OK|Status:"),
    Step("periodic first sample", "read", r"Sample:|CO2=|MEASUREMENT_NOT_READY|pending", timeout_s=15.0, settle_s=6.0),
    Step("periodic cached sample", "sample", r"Sample:|CO2=|has_sample"),
    Step("periodic stop", "periodic off", r"IN_PROGRESS|OK|pending", timeout_s=8.0),
    Step("post-stop status", "status", r"mode=.*IDLE|pending: NONE|pending=NONE", timeout_s=8.0, settle_s=1.0),
    Step("single-shot full start", "single_start full", r"IN_PROGRESS|pending"),
    Step("single-shot full read", "read", r"Sample:|CO2=", timeout_s=15.0, settle_s=6.0),
    Step("single-shot rht start", "single_start rht", r"IN_PROGRESS|pending"),
    Step("single-shot rht read", "read", r"Sample:|CO2=", timeout_s=8.0, settle_s=0.3),
    Step("low-power periodic start", "periodic lp", r"IN_PROGRESS|OK|Status:"),
    Step("low-power periodic sample", "read", r"Sample:|CO2=|MEASUREMENT_NOT_READY|pending", timeout_s=40.0, settle_s=31.0),
    Step("low-power periodic stop", "periodic off", r"IN_PROGRESS|OK|pending", timeout_s=8.0),
    Step("power down", "sleep", r"IN_PROGRESS|OK|pending", timeout_s=8.0, settle_s=1.0),
    Step("wake", "wake", r"IN_PROGRESS|OK|pending", timeout_s=8.0, settle_s=1.0),
    Step("serial after wake", "serial", r"serial=0x[0-9A-Fa-f]{12}", timeout_s=12.0),
    Step("recover", "recover", r"OK|IN_PROGRESS|Status:", timeout_s=12.0),
    Step("final driver health", "drv", r"State:|Driver|READY|DEGRADED|OFFLINE", timeout_s=12.0),
)


DESTRUCTIVE_STEPS: tuple[Step, ...] = (
    Step("persist settings", "persist confirm", r"IN_PROGRESS|OK|pending", timeout_s=12.0, destructive=True),
    Step("forced recalibration", "frc confirm 400", r"IN_PROGRESS|OK|pending", timeout_s=12.0, destructive=True),
    Step("forced recalibration result", "frc_result", r"Forced Recalibration|correction|ready", timeout_s=12.0, settle_s=1.0, destructive=True),
    Step("factory reset", "factory_reset confirm", r"IN_PROGRESS|OK|pending", timeout_s=12.0, destructive=True),
    Step("post-factory-reset serial", "serial", r"serial=0x[0-9A-Fa-f]{12}", timeout_s=15.0, settle_s=2.0, destructive=True),
)


class RunnerError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Optional SCD41 serial HIL runner. Does not fake hardware results."
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM7 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--output-dir", default="hil-results", help="Directory for transcript and summaries")
    parser.add_argument("--read-timeout", type=float, default=0.1, help="Per-read serial timeout in seconds")
    parser.add_argument("--include-destructive", action="store_true", help="Enable EEPROM/calibration destructive steps")
    parser.add_argument(
        "--confirm-destructive",
        default="",
        help=f"Required exact phrase for destructive steps: {DESTRUCTIVE_CONFIRMATION!r}",
    )
    parser.add_argument("--skip-safe", action="store_true", help="Run only destructive steps")
    parser.add_argument("--settle-before", type=float, default=2.0, help="Initial serial settle time")
    return parser.parse_args()


def load_serial_module():
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RunnerError("pyserial is required: install with `python -m pip install pyserial`") from exc
    return serial


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def read_until_match(ser, pattern: re.Pattern[str], timeout_s: float, transcript: list[str]) -> tuple[bool, str]:
    deadline = time.monotonic() + timeout_s
    buffer = ""
    while time.monotonic() < deadline:
        chunk = ser.read(512)
        if chunk:
            text = chunk.decode("utf-8", errors="replace")
            transcript.append(text)
            buffer += text
            if pattern.search(buffer):
                return True, buffer
        else:
            time.sleep(0.02)
    return False, buffer


def run_step(ser, step: Step, transcript: list[str]) -> dict[str, object]:
    if step.settle_s > 0:
        time.sleep(step.settle_s)
    command_line = f"{step.command}\n"
    transcript.append(f"\n>>> {step.command}\n")
    ser.write(command_line.encode("utf-8"))
    ser.flush()

    started = time.monotonic()
    matched, output = read_until_match(ser, re.compile(step.expect, re.DOTALL), step.timeout_s, transcript)
    elapsed_s = time.monotonic() - started
    return {
        "name": step.name,
        "command": step.command,
        "destructive": step.destructive,
        "expect": step.expect,
        "matched": matched,
        "elapsed_s": round(elapsed_s, 3),
        "status": "pass" if matched else "fail",
        "last_output": output[-1200:],
    }


def write_markdown(path: pathlib.Path, summary: dict[str, object]) -> None:
    lines = [
        "# SCD41 HIL Run Summary",
        "",
        f"- Timestamp UTC: `{summary['timestamp_utc']}`",
        f"- Port: `{summary['port']}`",
        f"- Baud: `{summary['baud']}`",
        f"- Include destructive: `{summary['include_destructive']}`",
        f"- Overall status: `{summary['status']}`",
        "",
        "| Step | Command | Destructive | Status | Elapsed s |",
        "| --- | --- | --- | --- | ---: |",
    ]
    for result in summary["results"]:  # type: ignore[index]
        lines.append(
            "| {name} | `{command}` | {destructive} | {status} | {elapsed_s} |".format(**result)
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


def main() -> int:
    args = parse_args()
    if args.include_destructive and args.confirm_destructive != DESTRUCTIVE_CONFIRMATION:
        print("Destructive HIL steps refused: confirmation phrase does not match.")
        print(f"Required: {DESTRUCTIVE_CONFIRMATION}")
        return 2

    try:
        serial = load_serial_module()
    except RunnerError as exc:
        print(f"SCD41 HIL runner FAILED: {exc}")
        return 2

    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    run_id = timestamp()
    transcript_path = output_dir / f"scd41_hil_{run_id}.log"
    json_path = output_dir / f"scd41_hil_{run_id}.json"
    md_path = output_dir / f"scd41_hil_{run_id}.md"

    steps: list[Step] = []
    if not args.skip_safe:
        steps.extend(SAFE_STEPS)
    if args.include_destructive:
        steps.extend(DESTRUCTIVE_STEPS)
    if not steps:
        print("No steps selected.")
        return 2

    transcript: list[str] = []
    results: list[dict[str, object]] = []
    status = "pass"

    try:
        with serial.Serial(args.port, args.baud, timeout=args.read_timeout, write_timeout=2.0) as ser:
            time.sleep(args.settle_before)
            transcript.append(f"# SCD41 HIL transcript {run_id}\n")
            transcript.append(f"# port={args.port} baud={args.baud}\n")
            while ser.in_waiting:
                transcript.append(ser.read(ser.in_waiting).decode("utf-8", errors="replace"))

            for step in steps:
                result = run_step(ser, step, transcript)
                results.append(result)
                print(f"{result['status']}: {step.name} [{step.command}]")
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

    transcript_path.write_text("".join(transcript), encoding="utf-8")
    summary: dict[str, object] = {
        "timestamp_utc": run_id,
        "port": args.port,
        "baud": args.baud,
        "include_destructive": args.include_destructive,
        "status": status,
        "transcript_path": str(transcript_path),
        "results": results,
    }
    json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    write_markdown(md_path, summary)

    print(f"Summary: {json_path}")
    print(f"Report: {md_path}")
    print(f"Transcript: {transcript_path}")
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
