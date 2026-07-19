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

MANDATORY_COMMANDS = {
    "?", "help", "version", "scan", "begin", "end", "status", "result",
    "cancel", "identity", "periodic", "dataready", "read", "single", "sample",
    "settings", "sleep", "wake", "toffset", "altitude", "pressure",
    "asc_enabled", "asc_target", "asc_initial", "asc_standard", "reinit",
    "selftest", "frc", "persist", "factory_reset", "command",
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


def command_names(text: str) -> set[str]:
    commands = set(re.findall(r'\bhead\s*==\s*"([^"]+)"', text))
    for item in help_items(text):
        commands.update(alias for alias in item.split(" ", 1)[0].split("/") if alias)
    return commands


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
        fail(f"{label} confirmation guard is missing or follows the operation")


def check_confirmations(text: str) -> None:
    require_before(command_block(text, "persist"), 'tail != "confirm"',
                   "OperationRequest::persistSettings()", "persist")
    require_before(command_block(text, "factory_reset"), 'tail != "confirm"',
                   "OperationRequest::factoryReset()", "factory_reset")
    require_before(command_block(text, "frc"), 'confirm != "confirm"',
                   "OperationRequest::forcedRecalibration", "forced recalibration")


def main() -> int:
    arduino = read(ARDUINO_MAIN)
    idf = read(IDF_MAIN)
    transport = read(IDF_TRANSPORT) + "\n" + read(IDF_TRANSPORT_HEADER)
    cmake = read(IDF_CMAKE)

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
