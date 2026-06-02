#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BRINGUP_MAIN = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
COMMON_DIR = ROOT / "examples" / "common"

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "TransportAdapter.h",
    "BusDiag.h",
    "CliShell.h",
    "CliStyle.h",
    "HealthView.h",
]

MANDATORY_COMMANDS = {
    "help",
    "scan",
    "probe",
    "recover",
    "diag",
    "demo",
    "drv",
    "read",
    "verbose",
    "stress",
}
MANDATORY_TIMING_HOOKS = {
    "gConfig.nowMs": re.compile(r"\bgConfig\s*\.\s*nowMs\s*="),
    "gConfig.nowUs": re.compile(r"\bgConfig\s*\.\s*nowUs\s*="),
    "gConfig.cooperativeYield": re.compile(r"\bgConfig\s*\.\s*cooperativeYield\s*="),
}
MANDATORY_ASYNC_TICK_TOKENS = [
    "const app_driver::Status tickSt = device.tick",
    "printStatus(tickSt)",
]

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
ARDUINO_BOUNDARY_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"](?:Arduino\.h|Wire\.h)[>"]',
    re.MULTILINE,
)
ARDUINO_BOUNDARY_TOKENS = {
    "TwoWire": re.compile(r"\bTwoWire\b"),
    "Serial": re.compile(r"\bSerial\b"),
    "String": re.compile(r"\bString\b"),
}

BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
DESTRUCTIVE_CONFIRMATION_TOKENS = [
    'printHelpItem("persist confirm"',
    'printHelpItem("factory_reset confirm"',
    'printHelpItem("frc confirm <reference_ppm>"',
    "use 'persist confirm' to write EEPROM",
    "use 'factory_reset confirm' to erase/reset settings",
    "use 'frc confirm <reference_ppm>' to update calibration history",
]


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


def strip_non_code(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    text = LINE_COMMENT_RE.sub("", text)
    return STRING_RE.sub('""', text)


def help_items(text: str) -> list[str]:
    return re.findall(r"printHelpItem\(\"([^\"]+)\"", text)


def head_commands(text: str) -> set[str]:
    return set(re.findall(r"\bhead\s*==\s*\"([^\"]+)\"", text))


def aliases_from_help(items: list[str]) -> set[str]:
    aliases: set[str] = set()
    for item in items:
        command_part = item.split(" ", 1)[0]
        for alias in command_part.split("/"):
            alias = alias.strip()
            if alias:
                aliases.add(alias)
    return aliases


def source_files_under(root: pathlib.Path) -> list[pathlib.Path]:
    if not root.exists():
        return []
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )


def ensure_no_arduino_boundary_leak(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    code = strip_non_code(text)
    rel = path.relative_to(ROOT).as_posix()
    if ARDUINO_BOUNDARY_INCLUDE_RE.search(text):
        fail(f"Arduino framework include leaked across boundary: {rel}")
    for label, pattern in ARDUINO_BOUNDARY_TOKENS.items():
        if pattern.search(code):
            fail(f"Arduino framework token '{label}' leaked across boundary: {rel}")


def check_example_boundaries() -> None:
    for dirname in ("src", "include"):
        for path in source_files_under(ROOT / dirname):
            ensure_no_arduino_boundary_leak(path)

    for path in source_files_under(ROOT / "examples" / "idf"):
        ensure_no_arduino_boundary_leak(path)
        text = path.read_text(encoding="utf-8", errors="replace")
        if "examples/common" in strip_non_code(text):
            fail(f"IDF example references examples/common helper boundary: {path.as_posix()}")

    for dirname in ("src", "include"):
        for path in source_files_under(ROOT / dirname):
            text = path.read_text(encoding="utf-8", errors="replace")
            if "examples/common" in strip_non_code(text):
                fail(f"library code references examples/common helper boundary: {path.as_posix()}")


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
    ensure_order(frc, 'confirm', "device.startForcedRecalibration",
                 f"{label} frc confirmation")


def main() -> int:
    common_dir = COMMON_DIR
    bringup_main = BRINGUP_MAIN

    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")

    ensure_missing(ROOT / "examples" / "00_smoke_boot", "deprecated example 00_smoke_boot")
    ensure_missing(
        ROOT / "examples" / "03_feature_walkthrough",
        "deprecated example 03_feature_walkthrough",
    )

    for name in REQUIRED_COMMON:
        ensure_exists(common_dir / name, f"common helper {name}")

    text = bringup_main.read_text(encoding="utf-8", errors="replace")

    commands = head_commands(text)
    commands.update(aliases_from_help(help_items(text)))
    missing = sorted(MANDATORY_COMMANDS - commands)
    if missing:
        fail(f"mandatory CLI commands missing: {missing}")

    if "cfg" not in commands and "settings" not in commands:
        fail("either 'cfg' or 'settings' command must be present")

    for hook, pattern in MANDATORY_TIMING_HOOKS.items():
        if pattern.search(text) is None:
            fail(f"timing hook assignment '{hook}' missing in {bringup_main.as_posix()}")

    for token in MANDATORY_ASYNC_TICK_TOKENS:
        if token not in text:
            fail(f"async tick status handling token '{token}' missing in {bringup_main.as_posix()}")

    check_destructive_confirmations(text, "Arduino CLI")
    check_example_boundaries()

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
