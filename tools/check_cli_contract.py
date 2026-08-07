#!/usr/bin/env python3
"""Static contract checks for the Arduino owner-safe CLI example."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
COMMON = ROOT / "examples" / "common"

REQUIRED_COMMON = {
    "BoardConfig.h",
    "BusDiag.h",
    "CliShell.h",
    "CliStyle.h",
    "CommandHandler.h",
    "DriverCompat.h",
    "I2cTransport.h",
    "Log.h",
}
MANDATORY_COMMANDS = {
    "?", "help", "version", "ver", "scan", "begin", "end", "status", "drv",
    "health", "result", "cancel", "identity", "variant", "periodic",
    "dataready", "read", "single", "sample",
    "settings", "cfg", "sleep", "wake", "toffset", "altitude", "pressure",
    "asc_enabled", "asc_target", "asc_initial", "asc_standard", "reinit",
    "selftest", "frc", "persist", "factory_reset", "command", "probe",
    "attach", "recover", "selfcheck", "stress", "stress_mix",
}
REQUIRED_OWNER_TOKENS = (
    "config.transfer = transport::wireTransfer",
    "device.begin(config)",
    "device.start(request, options, id)",
    "device.poll(millis(), 1U)",
    "device.cancel(runtime.operationId, millis())",
    "device.takeResult(id, result)",
    "Device::limits(kind)",
    "OperationRequest::diagnosticReadWords(",
    "OperationRequest::diagnosticWriteCommand(command)",
    "OperationRequest::diagnosticWriteWord(command, word)",
    "runtime.nextSafeCommandValid",
    "!timeReached(nowMs, runtime.nextSafeCommandMs)",
    "static_cast<int32_t>(nowMs - targetMs) >= 0",
    "diagnosticWorkflow.acceptResult(result)",
    "serviceWorkflow();",
    "WorkflowKind::SELFCHECK",
    "WorkflowKind::STRESS_MIX",
    "MAX_STRESS_CYCLES",
)
FORBIDDEN_DRIVER_CALLS = (
    ".tick(", ".probe(", ".recover(", ".readMeasurement(",
    ".startPeriodicMeasurement(", ".stopPeriodicMeasurement(",
)

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
ARDUINO_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](?:Arduino\.h|Wire\.h)[>"]', re.MULTILINE)
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def fail(message: str) -> None:
    print(f"CLI contract FAILED: {message}")
    raise SystemExit(1)


def strip_non_code(text: str) -> str:
    return STRING.sub('""', LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text)))


def help_items(text: str) -> list[str]:
    return re.findall(r'printHelpItem\("([^"]+)"', text)


def help_command_names(text: str) -> set[str]:
    items = help_items(text)
    return {
        command
        for command in MANDATORY_COMMANDS
        if any(
            re.search(rf"(?:^|[\s/]){re.escape(command)}(?:$|[\s<\[])", item)
            for item in items
        )
    }


def command_names(text: str) -> set[str]:
    return set(re.findall(r'\bhead\s*==\s*"([^"]+)"',
                          function_body(text, "processCommand")))


def function_body(text: str, function_name: str) -> str:
    match = re.search(
        rf"\b(?:void|bool)\s+{re.escape(function_name)}\s*\([^)]*\)\s*\{{",
        text,
    )
    if match is None:
        fail(f"missing function: {function_name}")
    depth = 1
    index = match.end()
    while index < len(text) and depth > 0:
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
        index += 1
    if depth != 0:
        fail(f"unterminated function: {function_name}")
    return text[match.end():index - 1]


def command_block(text: str, command: str) -> str:
    match = re.search(rf'\bhead\s*==\s*"{re.escape(command)}"', text)
    if match is None:
        fail(f"command handler missing: {command}")
    following = re.search(r'\bhead\s*==\s*"', text[match.end():])
    end = len(text) if following is None else match.end() + following.start()
    return text[match.start():end]


def require_before(block: str, guard: str, operation: str, label: str) -> None:
    guard_pos = block.find(guard)
    operation_pos = block.find(operation)
    if guard_pos < 0 or operation_pos < 0 or guard_pos > operation_pos:
        fail(f"{label} must check '{guard}' before '{operation}'")


def check_confirmation_guards(text: str) -> None:
    require_before(command_block(text, "persist"), 'tail != "confirm"',
                   "OperationRequest::persistSettings()", "persist")
    require_before(command_block(text, "factory_reset"), 'tail != "confirm"',
                   "OperationRequest::factoryReset()", "factory_reset")
    require_before(command_block(text, "frc"), 'confirm != "confirm"',
                   "OperationRequest::forcedRecalibration", "forced recalibration")
    raw = command_block(text, "command")
    require_before(raw, 'confirm != "confirm"',
                   "OperationRequest::diagnosticReadWords", "diagnostic word read")
    require_before(raw, 'confirm != "confirm"',
                   "OperationRequest::diagnosticWriteCommand", "diagnostic command write")
    require_before(raw, 'confirm != "confirm"',
                   "OperationRequest::diagnosticWriteWord", "diagnostic word write")


def check_framework_boundaries() -> None:
    for dirname in ("include", "src"):
        for path in (ROOT / dirname).rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            code = strip_non_code(text)
            if ARDUINO_INCLUDE.search(text) or re.search(r"\b(?:TwoWire|Serial|String)\b", code):
                fail(f"Arduino framework dependency leaked into {path.relative_to(ROOT).as_posix()}")

    for path in (ROOT / "examples" / "idf").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_non_code(text)
        if ARDUINO_INCLUDE.search(text) or re.search(r"\b(?:TwoWire|Serial|String)\b", code):
            fail(f"Arduino framework dependency leaked into {path.relative_to(ROOT).as_posix()}")


def main() -> int:
    if not MAIN.is_file():
        fail(f"missing example: {MAIN.relative_to(ROOT).as_posix()}")
    missing_helpers = sorted(name for name in REQUIRED_COMMON if not (COMMON / name).is_file())
    if missing_helpers:
        fail(f"missing common helpers: {missing_helpers}")

    text = MAIN.read_text(encoding="utf-8", errors="replace")
    missing_commands = sorted(MANDATORY_COMMANDS - command_names(text))
    if missing_commands:
        fail(f"mandatory commands missing: {missing_commands}")
    missing_help = sorted(MANDATORY_COMMANDS - help_command_names(text))
    if missing_help:
        fail(f"mandatory commands missing from help: {missing_help}")
    for token in REQUIRED_OWNER_TOKENS:
        if token not in text:
            fail(f"owner-safe lifecycle token missing: {token}")
    parser_text = (COMMON / "CommandHandler.h").read_text(
        encoding="utf-8", errors="replace"
    )
    for token in ("errno == ERANGE", "numeric_limits<uint32_t>::max()"):
        if token not in parser_text:
            fail(f"bounded numeric parser token missing: {token}")
    for token in FORBIDDEN_DRIVER_CALLS:
        if token in text:
            fail(f"obsolete direct/blocking driver call present: {token}")

    process = function_body(text, "processCommand")
    executable_workflows = {
        "probe": ("OperationKind::READ_IDENTITY", "OperationKind::ATTACH"),
        "attach/recover": ('head == "attach"', 'head == "recover"',
                           "OperationKind::ATTACH"),
        "selfcheck": ('head == "selfcheck"', "WorkflowKind::SELFCHECK"),
        "stress": ('head == "stress"', "WorkflowKind::STRESS"),
        "stress_mix": ('head == "stress_mix"', "WorkflowKind::STRESS_MIX"),
    }
    for label, tokens in executable_workflows.items():
        if any(token not in process for token in tokens):
            fail(f"executable workflow handler incomplete: {label}")

    check_confirmation_guards(text)
    check_framework_boundaries()
    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
