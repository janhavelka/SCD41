#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import pathlib
import platform
import re
import subprocess
import sys
import time
from dataclasses import dataclass, replace
from typing import Optional


ROOT = pathlib.Path(__file__).resolve().parents[1]
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
# Safe selfcheck includes a 10-second sensor self-test without CLI heartbeats.
# Allow that legitimate silent interval while retaining a bounded idle timeout.
DEFAULT_IDLE_TIMEOUT_S = 15.0
MAX_STEP_BYTES = 65536
MAX_BOOT_BYTES = 65536
MAX_TRANSCRIPT_CHUNKS = 131072
MAX_SOAK_SAMPLES = 10000
UINT32_MASK = 0xFFFFFFFF
HEALTH_FAILURE_FIELDS = ("transfer_fail", "consecutive", "protocol_fail", "crc_fail", "operation_fail", "cancelled")
CONFIGURATION_FIELDS = {
    "toffset": ("offset_mC", "TEMPERATURE_OFFSET", 0x01, 1000, 2000),
    "altitude": ("altitude_m", "SENSOR_ALTITUDE", 0x02, 100, 200),
    "pressure": ("pressure_Pa", "AMBIENT_PRESSURE", 0x04, 100000, 101000),
    "asc_enabled": ("asc", "ASC_ENABLED", 0x08, 0, 1),
    "asc_target": ("target_ppm", "ASC_TARGET", 0x10, 400, 450),
    "asc_initial": ("initial_h", "ASC_INITIAL_PERIOD", 0x20, 44, 48),
    "asc_standard": ("standard_h", "ASC_STANDARD_PERIOD", 0x40, 156, 160),
}
# Successful callback counts and mandatory minimum elapsed times, from the
# owner engine's SCD41::limits()/phase sequences. Zero callbacks is legal only
# for persistence with no dirty fields. ATTACH excludes any remaining initial
# power-up interval: a later recover can begin after that interval has elapsed.
OPERATION_EVIDENCE = {
    "ATTACH": ((6,), 533),
    "READ_IDENTITY": ((4,), 3),
    "READ_SENSOR_VARIANT": ((2,), 1),
    "READ_DATA_READY": ((2,), 1),
    "READ_CONFIGURATION": ((14,), 13),
    "START_PERIODIC": ((1,), 1),
    "START_LOW_POWER_PERIODIC": ((1,), 1),
    "STOP_PERIODIC": ((1,), 500),
    "FETCH_SAMPLE": ((4,), 3),
    "SINGLE_SHOT": ((5,), 5000),
    "SINGLE_SHOT_RHT_ONLY": ((5,), 50),
    "POWER_DOWN": ((1,), 1),
    "WAKE_UP": ((5,), 33),
    "REINIT": ((5,), 33),
    "SELF_TEST": ((2,), 10000),
    "PERSIST_SETTINGS": ((0, 1), 800),
    "FACTORY_RESET": ((5,), 1203),
}
for _field, _kind, *_rest in CONFIGURATION_FIELDS.values():
    OPERATION_EVIDENCE[f"READ_{_kind}"] = ((2,), 1)
    OPERATION_EVIDENCE[f"SET_{_kind}"] = ((2, 5), 1)

SERIAL_NUMBER_RE = re.compile(
    r"\bserial(?:_number)?\s*[:=]\s*(?:0x)?([0-9A-Fa-f]{12})\b",
    re.IGNORECASE,
)
FAILURE_TOKEN_RE = re.compile(
    r"\b("
    r"NOT_INITIALIZED|INVALID_CONFIG|INVALID_PARAM|RESULT_NOT_READY|STALE_RESULT|"
    r"CRC_MISMATCH|DEVICE_NOT_FOUND|OFFLINE|I2C_ERROR|I2C_NACK|I2C_TIMEOUT|"
    # A zero-valued health counter is not a cancelled operation. Nonzero
    # counters and standalone CANCELLED statuses must still fail the run.
    r"I2C_BUS|I2C_SHORT_TRANSFER|COMMAND_FAILED|UNSUPPORTED|TIMEOUT|CANCELLED(?!\s*=\s*0\b)|"
    r"PARTIAL|INDETERMINATE|RECONCILIATION_REQUIRED|FAILED|FAILURE|DEGRADED|"
    r"VERIFY_MISMATCH|DEVICE_VARIANT_MISMATCH|MEASUREMENT_NOT_READY|BUSY|NO_DATA|"
    r"Guru Meditation|Brownout|Traceback|ESP-ROM:|rst:0x[0-9a-f]+"
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
    group: str = "safe"
    sample_mode: str = ""


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
    Step("periodic sample", "read", r"op=FETCH_SAMPLE outcome=SUCCEEDED.*sample seq=", timeout_s=15.0, settle_s=6.0, sample_mode="PERIODIC"),
    Step("periodic cached sample", "sample", r"sample seq="),
    Step("periodic stop", "periodic off", r"op=STOP_PERIODIC outcome=SUCCEEDED", timeout_s=12.0),
    Step("post-stop status", "status", r"runtime .*mode=IDLE.*operation=NONE", timeout_s=8.0),
    Step("single-shot full", "single full", r"op=SINGLE_SHOT outcome=SUCCEEDED.*sample seq=", timeout_s=15.0, sample_mode="IDLE"),
    Step("single-shot rht", "single rht", r"op=SINGLE_SHOT_RHT_ONLY outcome=SUCCEEDED.*sample seq=", timeout_s=8.0, sample_mode="IDLE"),
    Step("low-power periodic start", "periodic lp", r"op=START_LOW_POWER_PERIODIC outcome=SUCCEEDED", timeout_s=12.0),
    Step("low-power periodic sample", "read", r"op=FETCH_SAMPLE outcome=SUCCEEDED.*sample seq=", timeout_s=45.0, settle_s=31.0, sample_mode="LOW_POWER_PERIODIC"),
    Step("low-power periodic stop", "periodic off", r"op=STOP_PERIODIC outcome=SUCCEEDED", timeout_s=12.0),
    Step("power down", "sleep", r"op=POWER_DOWN outcome=SUCCEEDED", timeout_s=12.0),
    Step("wake", "wake", r"op=WAKE_UP outcome=SUCCEEDED", timeout_s=12.0),
    Step("identity after wake", "identity", r"op=READ_IDENTITY outcome=SUCCEEDED.*serial=0x[0-9A-Fa-f]{12}", timeout_s=12.0),
    # Read the full final response before closing serial: the runtime prefix
    # can arrive before health counters and retained errors in later chunks.
    Step("final driver health", "status",
         r"runtime bound=yes attached=yes state=READY\b.*\n"
         r"health transfer_ok=\d+ transfer_fail=\d+ consecutive=\d+ expected_nack=\d+ "
         r"protocol_fail=\d+ crc_fail=\d+ operation_ok=\d+ operation_fail=\d+ cancelled=\d+\r?\n"
         r"last_errors transfer=\S+ protocol=\S+ operation=\S+ op=\S+ request=\d+ generation=\d+\r?\n",
         timeout_s=12.0),
)


DESTRUCTIVE_STEPS: tuple[Step, ...] = (
    Step("persist dirty settings or confirm no-op", "persist confirm", r"op=PERSIST_SETTINGS outcome=SUCCEEDED", timeout_s=15.0, destructive=True),
    Step("factory reset", "factory_reset confirm", r"op=FACTORY_RESET outcome=SUCCEEDED", timeout_s=15.0, destructive=True),
    Step("post-reset attach", "begin", r"op=ATTACH outcome=SUCCEEDED", timeout_s=15.0, settle_s=2.0, destructive=True),
    Step("post-reset reinit", "reinit", r"op=REINIT outcome=SUCCEEDED", timeout_s=15.0, destructive=True),
)


def configuration_steps(final_compensation: str = "altitude") -> list[Step]:
    """Restore readback values and explicitly select the final compensation source."""
    steps = [Step("configuration baseline", "settings", r"op=READ_CONFIGURATION outcome=SUCCEEDED.*config offset_mC=", group="configuration")]
    for command, (field, operation, _mask, _first, _second) in CONFIGURATION_FIELDS.items():
        for action in ("alternate", "restore"):
            steps.append(Step(f"{action} {field}", f"{command} {{{action}_{field}}}",
                              rf"op=SET_{operation} outcome=SUCCEEDED.*config offset_mC=",
                              group="configuration"))
    field, operation, *_unused = CONFIGURATION_FIELDS[final_compensation]
    steps.append(Step(f"select final {final_compensation} compensation", f"{final_compensation} {{restore_{field}}}",
                      rf"op=SET_{operation} outcome=SUCCEEDED.*config offset_mC=", group="configuration"))
    steps.append(Step("configuration values restored", "settings", r"op=READ_CONFIGURATION outcome=SUCCEEDED.*config offset_mC=", group="configuration"))
    return steps


def soak_steps(samples: int, mode: str) -> list[Step]:
    steps: list[Step] = []
    if mode != "single":
        low_power = mode == "low-power"
        steps.append(Step("soak start", "periodic lp" if low_power else "periodic on",
                          r"op=START_.*PERIODIC outcome=SUCCEEDED", group="soak"))
    for index in range(samples):
        steps.append(Step(f"soak sample {index + 1}/{samples}",
                          "single full" if mode == "single" else "read",
                          r"op=(FETCH_SAMPLE|SINGLE_SHOT) outcome=SUCCEEDED.*sample seq=",
                          timeout_s=15.0,
                          settle_s=0.25 if mode == "single" else (30.5 if mode == "low-power" else 5.2),
                          group="soak", sample_mode={"single": "IDLE", "periodic": "PERIODIC", "low-power": "LOW_POWER_PERIODIC"}[mode]))
        if (index + 1) % 20 == 0:
            steps.append(replace(SAFE_STEPS[-1], name="soak health", group="soak"))
    if mode != "single":
        steps.append(Step("soak stop", "periodic off", r"op=STOP_PERIODIC outcome=SUCCEEDED", group="soak"))
    return steps


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
    parser.add_argument("--firmware-commit", default="NOT_RECORDED", help="Commit actually flashed to the MCU (host checkout metadata is recorded separately)")
    parser.add_argument("--build-command", default="NOT_RECORDED", help="Exact command used to build the flashed firmware")
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
    parser.add_argument("--include-config-writes", action="store_true", help="Exercise all volatile setters and restore readback values; final compensation defaults to altitude; never persists")
    parser.add_argument("--final-compensation", choices=("altitude", "pressure"), default="altitude", help="Compensation source selected after configuration writes; the initial source is not readable")
    parser.add_argument("--soak-samples", type=int, default=0, help=f"Additional fresh samples, 0..{MAX_SOAK_SAMPLES}; no automatic retry")
    parser.add_argument("--soak-mode", choices=("periodic", "low-power", "single"), default="periodic")
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
        require(math.isfinite(args.timeout_s) and 0.0 < args.timeout_s <= 300, "--timeout-s must be finite and in (0, 300]")
    require(math.isfinite(args.read_timeout) and 0.0 < args.read_timeout <= 1, "--read-timeout must be finite and in (0, 1]")
    require(math.isfinite(args.idle_timeout_s) and 0.0 < args.idle_timeout_s <= 300, "--idle-timeout-s must be finite and in (0, 300]")
    require(math.isfinite(args.settle_before) and 0.0 <= args.settle_before <= 60, "--boot-settle-s must be finite and in [0, 60]")
    require(args.baud > 0, "--baud must be > 0")
    require(0 <= args.soak_samples <= MAX_SOAK_SAMPLES, f"--soak-samples must be 0..{MAX_SOAK_SAMPLES}")
    require(not args.skip_safe or (args.include_destructive and not args.include_config_writes and args.soak_samples == 0),
            "--skip-safe requires --include-destructive and cannot accompany configuration/soak groups")
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
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


def command_text(
    command: list[str], default: str, *, allow_empty: bool = False
) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return default
    if result.returncode != 0:
        return default
    output = result.stdout.strip()
    return output if output or allow_empty else default


def git_text(args: list[str], default: str, *, allow_empty: bool = False) -> str:
    return command_text(["git", *args], default, allow_empty=allow_empty)


def platformio_command(*args: str) -> list[str]:
    if platform.system() == "Windows":
        wrapper = ROOT / "scripts" / "pio.cmd"
        return ["cmd.exe", "/d", "/c", str(wrapper), *args]
    return [sys.executable, "-m", "platformio", *args]


def environment_metadata(invocation: str) -> dict[str, str]:
    dirty = git_text(["status", "--short"], "unknown", allow_empty=True)
    if dirty == "unknown":
        dirty_summary = "unknown"
    elif not dirty:
        dirty_summary = "clean"
    else:
        dirty_summary = f"dirty ({len(dirty.splitlines())} paths)"
    return {
        "repository_branch": git_text(["branch", "--show-current"], "unknown"),
        "repository_commit": git_text(["rev-parse", "HEAD"], "unknown"),
        "repository_status": dirty_summary,
        "invocation": invocation,
        "operating_system": platform.platform(),
        "python_version": platform.python_version(),
        "platformio_version": command_text(
            platformio_command("--version"), "not available"
        ),
    }


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
    for field in HEALTH_FAILURE_FIELDS:
        if re.search(rf"\b{field}=0*[1-9][0-9]*\b", clean) and "HEALTH_FAILURE" not in tokens:
            tokens.append("HEALTH_FAILURE")
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
    pattern = rf"(^|\n)\s*(?:[a-z_?]+\s*/\s*)*{re.escape(command)}(?:\s|/|$)"
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
        if args.include_config_writes:
            steps.extend(configuration_steps(args.final_compensation))
        if args.soak_samples:
            steps.extend(soak_steps(args.soak_samples, args.soak_mode))
    if args.include_destructive:
        if args.skip_safe:
            steps.extend(SAFE_STEPS[:4])
        steps.extend(replace(step, group="destructive") for step in DESTRUCTIVE_STEPS)
    if args.include_config_writes or args.soak_samples or args.include_destructive:
        steps.append(SAFE_STEPS[-1])
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


def records(output: str, name: str) -> list[tuple[int, dict[str, str]]]:
    """Parse only complete records, including the CLI's un-echoed prompt prefix."""
    pattern = rf"(?m)^[ \t]*(?:>[ \t]*)?{re.escape(name)} ([^\r\n]*)\r?\n"
    return [(match.start(), dict(re.findall(r"(\w+)=([^\s]+)", match.group(1))))
            for match in re.finditer(pattern, strip_ansi(output))]


def number(record: dict[str, str], key: str) -> int:
    value = record.get(key, "")
    require(re.fullmatch(r"-?\d+|0x[0-9A-Fa-f]+", value) is not None,
            f"missing or invalid numeric field {key}")
    return int(value, 16 if value.startswith("0x") else 10)


def response_complete(step: Step, output: str) -> bool:
    clean = strip_ansi(output)
    command = _command_from_step(step)
    if command == "help":
        return re.search(r"command write_word[^\n]*\n", clean) is not None
    if command == "scan":
        return re.search(r"(?:Found \d+ device\(s\)|No I2C devices found)[^\n]*\n", clean) is not None
    if command == "version":
        return re.search(r"SCD41 version=[^\n]*\n", clean) is not None
    if command == "status":
        return bool(records(clean, "last_errors"))
    if command in ("selfcheck", "stress", "stress_mix"):
        return bool(records(clean, "workflow_summary"))
    if command == "sample":
        return bool(records(clean, "sample"))
    terminal = records(clean, "result")
    if not terminal:
        return False
    position, result = terminal[-1]
    operation = result.get("op", "")
    tail = clean[position:]
    if not records(tail, "timing"):
        return False
    # Failed commands may omit value records; the full result and timing suffice
    # to stop immediately and report failure without retrying an effectful write.
    if result.get("outcome") != "SUCCEEDED":
        return True
    if operation == "FACTORY_RESET":
        return bool(records(tail, "identity")) and bool(records(tail, "config"))
    if operation == "PERSIST_SETTINGS" or operation == "READ_CONFIGURATION" or any(
            operation in (f"READ_{entry[1]}", f"SET_{entry[1]}") for entry in CONFIGURATION_FIELDS.values()):
        return bool(records(tail, "config"))
    if operation in ("ATTACH", "READ_IDENTITY", "READ_SENSOR_VARIANT", "WAKE_UP", "REINIT"):
        return bool(records(tail, "identity"))
    if operation in ("FETCH_SAMPLE", "SINGLE_SHOT", "SINGLE_SHOT_RHT_ONLY"):
        return bool(records(tail, "sample"))
    if operation == "READ_DATA_READY":
        return re.search(r"data_ready=(yes|no) raw=0x[0-9A-Fa-f]{4}\r?\n", tail) is not None
    if operation == "SELF_TEST":
        return bool(records(tail, "selftest"))
    return True


def validate_evidence(step: Step, output: str, state: dict) -> dict[str, object]:
    """Reject plausible-looking but stale, truncated or contradictory evidence."""
    command = _command_from_step(step)
    evidence: dict[str, object] = {}
    clean = strip_ansi(output)
    if command == "help":
        require(not missing_minimum_help_commands(clean), "firmware help is missing required commands")
    starts = records(clean, "started")
    terminals = records(clean, "result")
    timings = records(clean, "timing")
    workflow = command in ("selfcheck", "stress", "stress_mix")
    synchronous = command in ("help", "scan", "version", "status", "sample")
    require(len(starts) == len(terminals) == len(timings), "unmatched admission/result/timing records")
    if synchronous:
        require(not terminals, "unrelated asynchronous result arrived during synchronous command")
    else:
        require(len(terminals) > 0, "missing correlated operation result")
        require(workflow or len(terminals) == 1, "multiple results for one command")
    seen = state.setdefault("seen_ids", set())
    operations = []
    for index, ((start_at, start), (result_at, result), (timing_at, timing)) in enumerate(zip(starts, terminals, timings)):
        require(start_at < result_at < timing_at, "result preceded its admission or timing is out of order")
        if index + 1 < len(starts):
            require(timing_at < starts[index + 1][0], "overlapping CLI operations")
        identity = (number(result, "request"), number(result, "generation"))
        require(all(0 < part <= UINT32_MASK for part in identity), "invalid operation identity")
        require(identity == (number(start, "request"), number(start, "generation")), "request/generation mismatch")
        require(identity not in seen, "replayed operation result")
        require(start.get("op") == result.get("op"), "operation kind changed between admission and result")
        require(result.get("outcome") == "SUCCEEDED" and result.get("status") == "OK" and result.get("reconcile") == "no",
                "terminal result is unsuccessful or requires reconciliation")
        require(number(start, "deadline") == number(timing, "deadline"), "operation deadline changed")
        require(all(0 <= number(timing, field) <= UINT32_MASK for field in ("started", "completed", "deadline")), "invalid 32-bit operation timestamp")
        duration = (number(timing, "completed") - number(timing, "started")) & UINT32_MASK
        budget = (number(timing, "deadline") - number(timing, "started")) & UINT32_MASK
        require(duration < budget < 0x80000000, "successful result reached or exceeded its deadline")
        require(result["op"] in OPERATION_EVIDENCE, "unrecognized operation evidence contract")
        callbacks = number(result, "callbacks")
        allowed_callbacks, minimum_wait = OPERATION_EVIDENCE[result["op"]]
        require(callbacks in allowed_callbacks, "callback count contradicts operation phase sequence")
        if result["op"].startswith("SET_") and callbacks == 5:
            minimum_wait = 4
        if result["op"] == "PERSIST_SETTINGS" and callbacks == 0:
            minimum_wait = 0
            require(result.get("effect") == "NOT_ATTEMPTED", "zero-callback persist falsely reports a write")
        require(duration >= minimum_wait, "successful operation skipped its mandatory sensor wait")
        number(result, "detail")
        block_end = starts[index + 1][0] if index + 1 < len(starts) else len(clean)
        block = clean[result_at:block_end]
        require(response_complete(Step("operation payload", "operation", ""), block), "successful operation is missing its payload")
        if result["op"] == "READ_CONFIGURATION":
            require(number(records(block, "config")[0][1], "verified") == 0x7F, "configuration operation has unverified fields")
        if result["op"] == "PERSIST_SETTINGS":
            require(number(records(block, "config")[0][1], "dirty") == 0, "successful persistence left dirty fields")
        seen.add(identity)
        operations.append({**result, "duration_ms": duration, "epoch": number(timing, "epoch")})
        if result["op"] in ("ATTACH", "REINIT", "FACTORY_RESET", "WAKE_UP", "START_PERIODIC", "START_LOW_POWER_PERIODIC", "STOP_PERIODIC"):
            state.pop("last_sample", None)
    if operations:
        evidence["operations"] = operations
    if workflow:
        counts = records(clean, "workflow")
        require(bool(counts), "workflow summary lacks completion counters")
        final = counts[-1][1]
        expected = 4 if command == "selfcheck" else int(step.command.split()[1]) * (4 if command == "stress_mix" else 1)
        require(len(terminals) == expected and number(final, "pass") == expected and number(final, "warn") == 0 and number(final, "fail") == 0,
                "workflow did not complete every expected operation")
        require(final.get("progress") == f"{expected}/{expected}", "workflow progress is incomplete")
        sequence = {"selfcheck": ["READ_IDENTITY", "READ_SENSOR_VARIANT", "READ_CONFIGURATION", "SELF_TEST"],
                    "stress": ["READ_DATA_READY"],
                    "stress_mix": ["READ_IDENTITY", "READ_CONFIGURATION", "READ_SENSOR_VARIANT", "READ_DATA_READY"]}[command]
        cycles = 1 if command == "selfcheck" else int(step.command.split()[1])
        require([operation["op"] for operation in operations] == sequence * cycles, "workflow skipped or substituted an operation")
        require(final.get("name") == command and final.get("cycles") == f"{cycles}/{cycles}", "workflow completion counters identify another workflow")
    for _position, identity in records(clean, "identity"):
        serial = identity.get("serial", "")
        require(identity.get("valid") == "yes" and parse_serial_number(f"serial={serial}") is not None,
                "identity is not verified")
        require(number(identity, "serial") not in (0, 0xFFFFFFFFFFFF), "invalid sensor serial")
        require(identity.get("variant") == "SCD41" and (number(identity, "variant_word") & 0xF000) == 0x1000,
                "dedicated variant word does not identify SCD41")
        require(state.get("serial", serial) == serial, "sensor identity changed during run")
        state["serial"] = serial
        evidence["identity"] = identity
    for _position, configuration in records(clean, "config"):
        mask = number(configuration, "verified")
        dirty = number(configuration, "dirty")
        require(configuration.get("persistence_indeterminate") == "no", "persistence effect is uncertain")
        require(configuration.get("asc") in ("on", "off"), "invalid ASC enable value")
        require((dirty & ~0x7B) == 0, "non-persistable field marked dirty")
        values = {field: (int(configuration.get(field) == "on") if field == "asc" else number(configuration, field))
                  for field, *_unused in CONFIGURATION_FIELDS.values()}
        if command == "settings":
            require(mask == 0x7F, "configuration snapshot has unverified fields")
        if step.name == "configuration baseline":
            state["baseline_configuration"] = values
            state["baseline_dirty"] = dirty
            for _cmd, (field, _op, _bit, first, second) in CONFIGURATION_FIELDS.items():
                state[f"restore_{field}"] = values[field]
                state[f"alternate_{field}"] = second if values[field] == first else first
        if command in CONFIGURATION_FIELDS and len(step.command.split()) == 2:
            field, _op, bit, *_unused = CONFIGURATION_FIELDS[command]
            requested = int(step.command.split()[1])
            tolerance = 2 if field == "offset_mC" else 0  # Raw offset quantizes in 175000/65535 mC steps.
            require(mask & bit != 0 and abs(values[field] - requested) <= tolerance,
                    f"setter readback did not verify {field}={requested}")
            scalar_records = records(clean, "value")
            require(len(scalar_records) == 1, "setter omitted its typed result value")
            scalar = scalar_records[0][1]
            if field == "asc":
                require(scalar.get("bool") in ("true", "false"), "invalid ASC boolean result")
                scalar_value = int(scalar["bool"] == "true")
            else:
                scalar_value = number(scalar, "signed" if field == "offset_mC" else "unsigned")
            require(scalar_value == values[field], "typed setter result disagrees with configuration readback")
        if step.name == "configuration values restored":
            require(values == state.get("baseline_configuration"), "configuration was not restored to baseline")
        evidence["configuration"] = configuration
    for match in re.finditer(r"data_ready=(yes|no) raw=(0x[0-9A-Fa-f]{4})\r?\n", clean):
        require((match.group(1) == "yes") == ((int(match.group(2), 16) & 0x07FF) != 0), "readiness flag contradicts raw status word")
    for _position, selftest in records(clean, "selftest"):
        require(number(selftest, "raw") == 0, "sensor self-test reports a fault")
    for _position, sample in records(clean, "sample"):
        require(0 < number(sample, "seq") <= UINT32_MASK and number(sample, "epoch") > 0, "sample lacks provenance")
        require(-45000 <= number(sample, "temp_mC") <= 130000 and 0 <= number(sample, "rh_mPct") <= 100000,
                "sample exceeds representable sensor range")
        flags = number(sample, "flags")
        expected_flags = 0x0E if step.command == "single rht" else 0x0F
        require(flags == expected_flags, "sample validity/freshness flags contradict measurement mode")
        if flags & 1:
            require(0 < number(sample, "co2") <= 40000, "CO2 fails nonzero smoke policy or exceeds 40000 ppm output range")
        if step.sample_mode:
            require(sample.get("mode") == step.sample_mode, "sample mode differs from requested measurement")
        previous = state.get("last_sample")
        if command == "sample":
            require(previous is not None and sample == previous, "cached sample differs from last fresh result")
        else:
            require(len(terminals) == 1 and number(sample, "epoch") == number(timings[-1][1], "epoch"), "sample epoch differs from operation")
            sample_delay = (number(timings[-1][1], "completed") - number(sample, "at")) & UINT32_MASK
            require(sample_delay == 0, "sample timestamp is stale relative to operation completion")
            if previous is not None and previous["epoch"] == sample["epoch"] and previous["mode"] == sample["mode"]:
                require(number(sample, "seq") > number(previous, "seq"), "sample sequence did not advance")
                delta = (number(sample, "at") - number(previous, "at")) & UINT32_MASK
                require(0 < delta < 0x80000000, "sample timestamp did not advance")
                evidence["sample_interval_ms"] = delta
            state["last_sample"] = sample
        evidence["sample"] = sample
    if command == "status":
        runtime = records(clean, "runtime")
        slots = records(clean, "slot")
        health = records(clean, "health")
        require(len(runtime) == len(slots) == len(health) == len(records(clean, "last_errors")) == 1, "incomplete health snapshot")
        require(runtime[0][1].get("state") == "READY" and runtime[0][1].get("attached") == "yes" and runtime[0][1].get("reconcile") == "no", "driver is not attached and ready")
        require(slots[0][1].get("state") == "IDLE" and slots[0][1].get("operation") == "NONE", "driver still has outstanding work")
        require(all(number(health[0][1], field) == 0 for field in HEALTH_FAILURE_FIELDS), "health counters contain failures")
        last_errors = records(clean, "last_errors")[0][1]
        require(all(re.fullmatch(r"OK@\d+", last_errors.get(field, "")) for field in ("transfer", "protocol", "operation")), "retained health error is not OK")
        evidence["health"] = health[0][1]
    return evidence


def read_until_match(
    ser,
    pattern: re.Pattern[str],
    timeout_s: float,
    idle_timeout_s: float,
    transcript: list[str],
    step: Optional[Step] = None,
) -> tuple[bool, str]:
    deadline = time.monotonic() + timeout_s
    idle_deadline = time.monotonic() + idle_timeout_s
    read_timeout = getattr(ser, "timeout", 0.1)
    buffer = ""
    while time.monotonic() < deadline:
        ser.timeout = max(0.0, min(read_timeout, deadline - time.monotonic(), idle_deadline - time.monotonic()))
        chunk = ser.read(512)
        if chunk:
            text = chunk.decode("utf-8", errors="replace")
            transcript.append(text)
            require(len(transcript) <= MAX_TRANSCRIPT_CHUNKS, "serial transcript exceeded its bounded capture limit")
            buffer += text
            if len(buffer.encode("utf-8")) > MAX_STEP_BYTES:
                raise RunnerError("serial response exceeded the bounded step buffer")
            idle_deadline = time.monotonic() + idle_timeout_s
            # The CLI colourizes the very tokens the expectations match, so the
            # buffer must be normalized exactly like step_output_matches().
            complete = response_complete(step, buffer) if step is not None else buffer.endswith("\n")
            if complete:
                ser.timeout = read_timeout
                return pattern.search(strip_ansi(buffer)) is not None, buffer
        else:
            if time.monotonic() >= idle_deadline:
                ser.timeout = read_timeout
                return False, buffer
            time.sleep(0.02)
    ser.timeout = read_timeout
    return False, buffer


def drain_pending(ser, transcript: list[str], *, boot: bool = False) -> None:
    """Bound startup noise; later unsolicited records cannot satisfy a new step."""
    deadline = time.monotonic() + 2.0
    captured = ""
    count = 0
    while getattr(ser, "in_waiting", 0):
        require(time.monotonic() < deadline and count < MAX_BOOT_BYTES, "serial input did not become quiet")
        chunk = ser.read(min(512, ser.in_waiting, MAX_BOOT_BYTES - count))
        count += len(chunk)
        text = chunk.decode("utf-8", errors="replace")
        transcript.append(text)
        captured += text
    if not boot:
        require(re.fullmatch(r"[\s>]*", strip_ansi(captured)) is not None, "unsolicited output before command; stale result refused")


def run_step(ser, step: Step, idle_timeout_s: float, transcript: list[str], state: Optional[dict] = None) -> dict[str, object]:
    if state is None:
        state = {}
    step = replace(step, command=step.command.format_map(state))
    if step.settle_s > 0:
        time.sleep(step.settle_s)
    drain_pending(ser, transcript)
    command_line = f"{step.command}\n"
    transcript.append(f"\n>>> {step.command}\n")
    payload = command_line.encode("utf-8")
    # pyserial.write_timeout bounds write(); flush()/tcdrain can wait indefinitely.
    require(ser.write(payload) == len(payload), "serial command write was incomplete")

    started = time.monotonic()
    matched, output = read_until_match(
        ser,
        re.compile(step.expect, re.IGNORECASE | re.DOTALL),
        step.timeout_s,
        idle_timeout_s,
        transcript,
        step,
    )
    elapsed_s = time.monotonic() - started
    passed = step_passed(matched, output)
    validation_errors = [] if matched else ["response incomplete or expected command outcome absent"]
    evidence: dict[str, object] = {}
    if passed:
        try:
            evidence = validate_evidence(step, output, state)
        except RunnerError as exc:
            passed = False
            validation_errors.append(str(exc))
    return {
        "name": step.name,
        "command": step.command,
        "destructive": step.destructive,
        "group": step.group,
        "expect": step.expect,
        "matched": matched,
        "failure_tokens": list(classify_failure_tokens(output)),
        "elapsed_s": round(elapsed_s, 3),
        "status": "pass" if passed else "fail",
        "validation_errors": validation_errors,
        "evidence": evidence,
        "last_output": output[-1200:],
    }


def not_run_result(step: Step, reason: str) -> dict[str, object]:
    return {
        "name": step.name,
        "command": step.command,
        "destructive": step.destructive,
        "group": step.group,
        "expect": step.expect,
        "matched": False,
        "failure_tokens": [],
        "elapsed_s": 0,
        "status": "not-run",
        "validation_errors": [],
        "evidence": {},
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
        f"- Flashed firmware commit (operator supplied): `{summary['firmware_commit']}`",
        f"- Firmware build command: `{summary['build_command']}`",
        f"- Repository branch: `{summary['repository_branch']}`",
        f"- Repository commit: `{summary['repository_commit']}`",
        f"- Repository status: `{summary['repository_status']}`",
        f"- Invocation: `{summary['invocation']}`",
        f"- Operating system: `{summary['operating_system']}`",
        f"- Python: `{summary['python_version']}`",
        f"- PlatformIO: `{summary['platformio_version']}`",
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
                    # Expectations are regexes; an unescaped `|` would split the row.
                    "expect": str(result["expect"]).replace("|", "\\|"),
                    "failure_tokens": ", ".join(result.get("failure_tokens", []) + result.get("validation_errors", [])).replace("|", "\\|") or "-",
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
        "firmware_commit": args.firmware_commit,
        "build_command": args.build_command,
        **environment_metadata(args.invocation),
        "include_destructive": args.include_destructive,
        "include_config_writes": args.include_config_writes,
        "configuration_final_compensation_requested": args.final_compensation if args.include_config_writes else "not-selected",
        "soak_samples_requested": args.soak_samples,
        "soak_mode": args.soak_mode,
        "serial_commands_attempted": sum(chunk.startswith("\n>>> ") for chunk in transcript),
        "groups": {group: result_counts([result for result in results if result.get("group") == group])
                   for group in ("safe", "configuration", "soak", "destructive")},
        "manual_gates": {gate: "not-run" for gate in ("fault_injection", "shared_bus_latency", "power_cycle_persistence", "forced_recalibration", "accuracy", "clock_wrap")},
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
    invocation_args = list(argv) if argv is not None else sys.argv[1:]
    args.invocation = subprocess.list2cmdline(
        [sys.executable, str(ROOT / "tools" / "scd41_hil_runner.py"), *invocation_args]
    )
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

    transcript: list[str] = [f"# SCD41 HIL transcript {run_id}\n",
                             f"# port={args.port} baud={args.baud}\n",
                             f"# board={args.board}\n# fixture={args.fixture}\n# operator={args.operator}\n",
                             f"# firmware_commit={args.firmware_commit}\n# build_command={args.build_command}\n"]
    results: list[dict[str, object]] = []
    status = "pass"
    state: dict = {}
    next_index = 0
    active_step: Optional[Step] = None

    try:
        serial = load_serial_module()
        with serial.Serial(args.port, args.baud, timeout=args.read_timeout, write_timeout=2.0) as ser:
            time.sleep(args.settle_before)
            drain_pending(ser, transcript, boot=True)

            for index, step in enumerate(steps):
                active_step = step
                next_index = index + 1
                result = run_step(ser, step, args.idle_timeout_s, transcript, state)
                results.append(result)
                active_step = None
                print(f"{result['status']}: {step.name} [{result['command']}]")
                if args.verbose:
                    print(f"  elapsed_s={result['elapsed_s']} failure_tokens={result['failure_tokens']} validation_errors={result['validation_errors']}")
                    excerpt = strip_ansi(str(result["last_output"]))[-300:].strip()
                    if excerpt:
                        print(f"  output: {excerpt}")
                if result["status"] != "pass":
                    status = "fail"
                    break
    except (Exception, KeyboardInterrupt) as exc:
        status = "fail"
        reason = str(exc) or type(exc).__name__
        result = not_run_result(active_step or Step("runner setup", "", ""), reason)
        result.update(status="fail", validation_errors=[reason])
        results.append(result)
        transcript.append(f"\n# runner stopped: {reason}\n")
    if status != "pass":
        results.extend(not_run_result(step, "not executed after earlier failure; no automatic retry or recovery") for step in steps[next_index:])

    transcript_path, json_path, md_path = write_summary_files(
        args, run_id, output_dir, transcript, results, status
    )

    print(f"Summary: {json_path}")
    print(f"Report: {md_path}")
    print(f"Transcript: {transcript_path}")
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
