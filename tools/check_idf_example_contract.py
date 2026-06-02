#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARDUINO_MAIN = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
IDF_MAIN = ROOT / "examples" / "idf" / "basic" / "main" / "main.cpp"
IDF_TRANSPORT = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.cpp"

MANDATORY_COMMANDS = {
    "?",
    "help",
    "version",
    "ver",
    "info",
    "scan",
    "begin",
    "end",
    "probe",
    "recover",
    "diag",
    "demo",
    "drv",
    "drv1",
    "state",
    "cfg",
    "settings",
    "status",
    "read",
    "fetch",
    "sample",
    "last",
    "raw",
    "comp",
    "dataready",
    "watch",
    "verbose",
    "stress",
    "single",
    "single_start",
    "convert",
    "mode",
    "periodic",
    "sleep",
    "wake",
    "serial",
    "variant",
    "toffset",
    "altitude",
    "pressure",
    "asc_enabled",
    "asc_target",
    "asc_initial",
    "asc_standard",
    "persist",
    "reinit",
    "factory_reset",
    "selftest",
    "selftest_result",
    "frc",
    "frc_result",
    "command",
}

MANDATORY_RAW_SUBCOMMANDS = {"write", "write_data", "read", "read_word", "read_words"}

REQUIRED_IDF_TOKENS = [
    "driver/i2c_master.h",
    "i2c_new_master_bus",
    "i2c_master_bus_add_device",
    "i2c_master_probe",
    "printPrompt",
    "printStatus",
    "printHealthDiff",
    "printPendingWorkView",
    "LOG_COLOR_GREEN",
    "LOG_COLOR_YELLOW",
    "LOG_COLOR_RED",
    "const app_driver::Status tickSt = device.tick",
    "printStatus(tickSt)",
]
DESTRUCTIVE_CONFIRMATION_TOKENS = [
    'printHelpItem("persist confirm"',
    'printHelpItem("factory_reset confirm"',
    'printHelpItem("frc confirm <reference_ppm>"',
    "use 'persist confirm' to write EEPROM",
    "use 'factory_reset confirm' to erase/reset settings",
    "use 'frc confirm <reference_ppm>' to update calibration history",
]

FORBIDDEN_IDF_PATTERNS = {
    "Arduino.h": re.compile(r"#\s*include\s*[<\"]Arduino\.h[>\"]"),
    "Wire.h": re.compile(r"#\s*include\s*[<\"]Wire\.h[>\"]"),
    "ArduinoCompat": re.compile(r"\bArduinoCompat\b"),
    "IdfArduinoCompat": re.compile(r"\bIdfArduinoCompat\b"),
    "TwoWire": re.compile(r"\bTwoWire\b"),
    "String": re.compile(r"\bString\b"),
    "Serial": re.compile(r"\bSerial\b"),
    "<string>": re.compile(r"#\s*include\s*<string>"),
    "std::string": re.compile(r"\bstd::string\b"),
    "std::vector": re.compile(r"\bstd::vector\b"),
    "operator new": re.compile(r"\bnew\b"),
    "malloc": re.compile(r"\bmalloc\s*\("),
    "calloc": re.compile(r"\bcalloc\s*\("),
    "realloc": re.compile(r"\brealloc\s*\("),
    "free": re.compile(r"\bfree\s*\("),
    "millis()": re.compile(r"\bmillis\s*\("),
    "Arduino CLI source": re.compile(r"examples/01_basic_bringup_cli/main\.cpp"),
    "setup": re.compile(r"\bsetup\s*\(\s*\)\s*;"),
    "loop": re.compile(r"\bloop\s*\(\s*\)\s*;"),
}


def fail(msg: str) -> None:
    print(f"IDF example contract FAILED: {msg}")
    raise SystemExit(1)


def read(path: pathlib.Path) -> str:
    if not path.exists():
        fail(f"missing file: {path.relative_to(ROOT).as_posix()}")
    return path.read_text(encoding="utf-8", errors="replace")


def help_sections(text: str) -> list[str]:
    return re.findall(r"printHelpSection\(\"([^\"]+)\"\)", text)


def help_items(text: str) -> list[str]:
    return re.findall(r"printHelpItem\(\"([^\"]+)\"", text)


def head_commands(text: str) -> set[str]:
    return set(re.findall(r"\bhead\s*==\s*\"([^\"]+)\"", text))


def raw_subcommands(text: str) -> set[str]:
    return set(re.findall(r"\bsub\s*==\s*\"([^\"]+)\"", text))


def aliases_from_help(items: list[str]) -> set[str]:
    aliases: set[str] = set()
    for item in items:
        command_part = item.split(" ", 1)[0]
        for alias in command_part.split("/"):
            aliases.add(alias.strip())
    return {alias for alias in aliases if alias}


def command_block(text: str, command: str) -> str:
    marker = f'if (head == "{command}")'
    start = text.find(marker)
    if start < 0:
        fail(f"command handler for '{command}' missing")
    next_start = text.find('\n  if (head == "', start + len(marker))
    if next_start < 0:
        next_start = len(text)
    return text[start:next_start]


def ensure_order(block: str, before: str, after: str, label: str) -> None:
    before_pos = block.find(before)
    after_pos = block.find(after)
    if before_pos < 0:
        fail(f"{label}: missing '{before}'")
    if after_pos < 0:
        fail(f"{label}: missing '{after}'")
    if before_pos > after_pos:
        fail(f"{label}: '{before}' must appear before '{after}'")


def check_destructive_confirmations(text: str, label: str) -> None:
    for token in DESTRUCTIVE_CONFIRMATION_TOKENS:
        if token not in text:
            fail(f"{label}: destructive confirmation token missing: {token}")

    persist = command_block(text, "persist")
    ensure_order(persist, 'tail != "confirm"', "device.startPersistSettings()",
                 f"{label} persist confirmation")

    factory_reset = command_block(text, "factory_reset")
    ensure_order(factory_reset, 'tail != "confirm"', "device.startFactoryReset()",
                 f"{label} factory_reset confirmation")
    ensure_order(factory_reset, 'tail != "confirm"', "clearSettingsCache()",
                 f"{label} factory_reset cache clear")

    frc = command_block(text, "frc")
    ensure_order(frc, "confirm", "device.startForcedRecalibration",
                 f"{label} frc confirmation")


def main() -> int:
    arduino = read(ARDUINO_MAIN)
    idf = read(IDF_MAIN)
    transport = read(IDF_TRANSPORT)

    arduino_sections = help_sections(arduino)
    idf_sections = help_sections(idf)
    if arduino_sections != idf_sections:
        fail(f"help sections differ: Arduino={arduino_sections}, IDF={idf_sections}")

    arduino_items = help_items(arduino)
    idf_items = help_items(idf)
    if arduino_items != idf_items:
        missing = [item for item in arduino_items if item not in idf_items]
        extra = [item for item in idf_items if item not in arduino_items]
        fail(f"help items differ: missing={missing}, extra={extra}")

    arduino_commands = head_commands(arduino)
    idf_commands = head_commands(idf)
    arduino_commands.update(aliases_from_help(arduino_items))
    idf_commands.update(aliases_from_help(idf_items))

    missing_mandatory = sorted(MANDATORY_COMMANDS - idf_commands)
    if missing_mandatory:
        fail(f"IDF CLI missing mandatory commands: {missing_mandatory}")

    if arduino_commands != idf_commands:
        missing = sorted(arduino_commands - idf_commands)
        extra = sorted(idf_commands - arduino_commands)
        fail(f"top-level command sets differ: missing={missing}, extra={extra}")

    arduino_raw = raw_subcommands(arduino)
    idf_raw = raw_subcommands(idf)
    if not MANDATORY_RAW_SUBCOMMANDS.issubset(idf_raw):
        fail(f"IDF CLI missing raw subcommands: {sorted(MANDATORY_RAW_SUBCOMMANDS - idf_raw)}")
    if arduino_raw != idf_raw:
        fail(f"raw subcommand sets differ: Arduino={sorted(arduino_raw)}, IDF={sorted(idf_raw)}")

    check_destructive_confirmations(arduino, "Arduino CLI")
    check_destructive_confirmations(idf, "IDF CLI")

    combined_idf = idf + "\n" + transport
    for label, pattern in FORBIDDEN_IDF_PATTERNS.items():
        if pattern.search(combined_idf):
            fail(f"IDF example uses forbidden Arduino/compat token: {label}")

    for token in REQUIRED_IDF_TOKENS:
        if token not in combined_idf:
            fail(f"IDF example missing required token: {token}")

    if "driver/i2c.h" in combined_idf:
        fail("IDF example must use driver/i2c_master.h, not legacy driver/i2c.h")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
