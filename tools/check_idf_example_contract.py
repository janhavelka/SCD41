#!/usr/bin/env python3
"""Keep the native ESP-IDF CLI aligned with the Arduino CLI and core boundary."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARDUINO_MAIN = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
IDF_ROOT = ROOT / "examples" / "idf" / "basic"
IDF_MAIN = IDF_ROOT / "main" / "main.cpp"
IDF_TRANSPORT = IDF_ROOT / "main" / "IdfI2cTransport.cpp"
IDF_TRANSPORT_HEADER = IDF_ROOT / "main" / "IdfI2cTransport.h"
IDF_CMAKE = IDF_ROOT / "main" / "CMakeLists.txt"
ARDUINO_COMMAND_HANDLER = ROOT / "examples" / "common" / "CommandHandler.h"
WORKFLOW_HEADER = ROOT / "examples" / "common" / "DiagnosticWorkflow.h"

MANDATORY_COMMANDS = {
    "?", "help", "version", "ver", "scan", "begin", "end", "status", "drv",
    "health", "result", "cancel", "identity", "variant", "periodic",
    "dataready", "read", "single", "sample",
    "settings", "cfg", "sleep", "wake", "toffset", "altitude", "pressure",
    "asc_enabled", "asc_target", "asc_initial", "asc_standard", "reinit",
    "selftest", "frc", "persist", "factory_reset", "command", "probe",
    "attach", "recover", "selfcheck", "stress", "stress_mix",
}
REQUIRED_IDF_TOKENS = (
    "driver/i2c_master.h",
    "esp_timer.h",
    "freertos/task.h",
    'extern "C" void app_main(void)',
    "i2c_new_master_bus",
    "i2c_master_bus_add_device",
    "i2c_master_probe",
    "device.begin(config)",
    "device.start(request, options, id)",
    "device.poll(idfNowMs(), 1U)",
    "device.cancel(runtime.operationId, idfNowMs())",
    "device.takeResult(id, result)",
    "SCD41::SCD41::limits(kind)",
    "SCD41::errorName(value)",
    "OperationRequest::diagnosticWriteCommand(command)",
    "OperationRequest::diagnosticWriteWord(command, word)",
    "OperationRequest::diagnosticReadWords(",
    "runtime.nextSafeCommandValid",
    "!timeReached(nowMs, runtime.nextSafeCommandMs)",
    "static_cast<int32_t>(nowMs - targetMs) >= 0",
    "errno == ERANGE",
    "diagnosticWorkflow.acceptResult(result)",
    "serviceWorkflow();",
    "WorkflowKind::SELFCHECK",
    "WorkflowKind::STRESS_MIX",
    "MAX_STRESS_CYCLES",
)
REQUIRED_TRANSPORT_TOKENS = (
    "SCD41::TransferResult idfI2cTransfer",
    "const SCD41::TransferRequest& request",
    "i2c_master_transmit_receive",
    "ESP_ERR_INVALID_RESPONSE",
    "SCD41::TransferCode::NACK",
    "SCD41::TransferDisposition::INDETERMINATE",
    "SCD41::TransferDisposition::NOT_STARTED",
)
FORBIDDEN_DRIVER_CALLS = (
    ".tick(", ".probe(", ".recover(", ".readMeasurement(",
    ".startPeriodicMeasurement(", ".stopPeriodicMeasurement(",
)
FORBIDDEN_PATTERNS = {
    "Arduino header": re.compile(r'^\s*#\s*include\s*[<"](?:Arduino\.h|Wire\.h)[>"]', re.MULTILINE),
    "legacy I2C header": re.compile(r'^\s*#\s*include\s*<driver/i2c\.h>', re.MULTILINE),
    "Arduino facade": re.compile(r"\b(?:ArduinoCompat|IdfArduinoCompat|TwoWire|Serial|String)\b"),
    "dynamic C++ container": re.compile(r"\bstd::(?:string|vector)\b"),
    "heap allocation": re.compile(r"\b(?:malloc|calloc|realloc|free)\s*\(|\bnew\s+"),
    "Arduino time": re.compile(r"\bmillis\s*\("),
    "Arduino source reuse": re.compile(r"examples/01_basic_bringup_cli/main\.cpp"),
}

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def fail(message: str) -> None:
    print(f"IDF example contract FAILED: {message}")
    raise SystemExit(1)


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        fail(f"missing file: {path.relative_to(ROOT).as_posix()}")
    return path.read_text(encoding="utf-8", errors="replace")


def strip_non_code(text: str) -> str:
    return STRING.sub('""', LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text)))


def help_sections(text: str) -> list[str]:
    return re.findall(r'printHelpSection\("([^"]+)"\)', text)


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


def command_block(text: str, command: str) -> str:
    match = re.search(rf'\bhead\s*==\s*"{re.escape(command)}"', text)
    if match is None:
        fail(f"command handler missing: {command}")
    following = re.search(r'\bhead\s*==\s*"', text[match.end():])
    end = len(text) if following is None else match.end() + following.start()
    return text[match.start():end]


HANDLER_ENTRIES = (
    "scan", "begin", "end", "health", "result", "cancel", "identity",
    "variant", "periodic", "dataready", "read", "single", "sample", "cfg",
    "sleep", "wake", "toffset", "asc_standard", "asc_enabled", "reinit",
    "selftest", "selfcheck", "frc", "persist", "factory_reset", "probe",
    "recover", "stress_mix",
)


def handler_signature(text: str, command: str) -> tuple[frozenset[str], frozenset[str], frozenset[str]]:
    block = command_block(text, command)
    operation_kinds = frozenset(re.findall(r"OperationKind::([A-Z0-9_]+)", block))
    builders = frozenset(re.findall(r"OperationRequest::([A-Za-z0-9_]+)\s*\(", block))
    routing_literals = frozenset(
        value
        for _, value in re.findall(
            r'\b(tail|sub|confirm)\s*(?:==|!=)\s*"([^"]*)"', block
        )
    )
    return operation_kinds, builders, routing_literals


def function_body(text: str, function_name: str) -> str:
    match = re.search(
        rf"\b(?:void|bool)\s+{re.escape(function_name)}\s*\([^)]*\)\s*\{{",
        text,
    )
    if match is None:
        fail(f"missing parser function: {function_name}")
    depth = 1
    index = match.end()
    while index < len(text) and depth > 0:
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
        index += 1
    if depth != 0:
        fail(f"unterminated parser function: {function_name}")
    return text[match.end():index - 1]


def check_handler_parity(arduino: str, idf: str) -> None:
    for command in HANDLER_ENTRIES:
        arduino_signature = handler_signature(arduino, command)
        idf_signature = handler_signature(idf, command)
        if arduino_signature != idf_signature:
            fail(
                f"handler semantics differ for '{command}': "
                f"arduino={arduino_signature}, idf={idf_signature}"
            )

    arduino_parser = read(ARDUINO_COMMAND_HANDLER)
    arduino_bool_tokens = set(
        re.findall(r'"([^"]+)"', function_body(arduino_parser, "parseBool01"))
    )
    idf_bool_tokens = set(
        re.findall(r'"([^"]+)"', function_body(idf, "parseBool"))
    )
    if arduino_bool_tokens != idf_bool_tokens:
        fail(
            "boolean parser vocabulary differs: "
            f"arduino={sorted(arduino_bool_tokens)}, "
            f"idf={sorted(idf_bool_tokens)}"
        )


def require_before(block: str, guard: str, operation: str, label: str) -> None:
    guard_pos = block.find(guard)
    operation_pos = block.find(operation)
    if guard_pos < 0 or operation_pos < 0 or guard_pos > operation_pos:
        fail(f"{label} confirmation guard is missing or follows the operation")


def check_confirmations(text: str) -> None:
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


def main() -> int:
    arduino = read(ARDUINO_MAIN)
    idf = read(IDF_MAIN)
    transport = read(IDF_TRANSPORT) + "\n" + read(IDF_TRANSPORT_HEADER)
    cmake = read(IDF_CMAKE)
    workflow = read(WORKFLOW_HEADER)

    if help_sections(arduino) != help_sections(idf):
        fail("Arduino and IDF help sections differ")
    if help_items(arduino) != help_items(idf):
        fail("Arduino and IDF help command lines differ")
    arduino_commands = command_names(arduino)
    idf_commands = command_names(idf)
    if arduino_commands != idf_commands:
        fail(f"top-level commands differ: missing={sorted(arduino_commands-idf_commands)}, extra={sorted(idf_commands-arduino_commands)}")
    missing = sorted(MANDATORY_COMMANDS - idf_commands)
    if missing:
        fail(f"mandatory commands missing: {missing}")
    missing_help = sorted(MANDATORY_COMMANDS - help_command_names(idf))
    if missing_help:
        fail(f"mandatory commands missing from help: {missing_help}")
    check_handler_parity(arduino, idf)

    for source_name, source in (("Arduino", arduino), ("IDF", idf)):
        process = function_body(source, "processCommand")
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
                fail(f"{source_name} executable workflow incomplete: {label}")

    combined = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in sorted(IDF_ROOT.rglob("*"))
        if path.is_file() and path.suffix.lower() in {".c", ".cc", ".cpp", ".h", ".hpp"}
    )
    code = strip_non_code(combined)
    for label, pattern in FORBIDDEN_PATTERNS.items():
        target = combined if "header" in label else code
        if pattern.search(target):
            fail(f"IDF example uses forbidden {label}")
    workflow_code = strip_non_code(workflow)
    for label, pattern in FORBIDDEN_PATTERNS.items():
        target = workflow if "header" in label else workflow_code
        if pattern.search(target):
            fail(f"shared diagnostic workflow uses forbidden {label}")
    for token in REQUIRED_IDF_TOKENS:
        if token not in combined:
            fail(f"IDF example missing owner-safe token: {token}")
    for token in REQUIRED_TRANSPORT_TOKENS:
        if token not in transport:
            fail(f"IDF transport missing token: {token}")
    for token in FORBIDDEN_DRIVER_CALLS:
        if token in combined:
            fail(f"obsolete direct/blocking driver call present: {token}")
    for dependency in ("SCD41", "esp_driver_i2c", "esp_timer", "freertos"):
        if dependency not in cmake:
            fail(f"CMake dependency missing: {dependency}")
    if re.search(r"\bArduino\b", cmake):
        fail("IDF example must not depend on Arduino")

    check_confirmations(arduino)
    check_confirmations(idf)
    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
