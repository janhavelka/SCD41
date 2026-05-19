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
]


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

    combined_idf = idf + "\n" + transport
    for token in REQUIRED_IDF_TOKENS:
        if token not in combined_idf:
            fail(f"IDF example missing required token: {token}")

    if "driver/i2c.h" in combined_idf:
        fail("IDF example must use driver/i2c_master.h, not legacy driver/i2c.h")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
