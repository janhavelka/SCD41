#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

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

MANDATORY_COMMANDS = ["help", "scan", "probe", "recover", "diag", "demo", "drv", "read", "verbose", "stress"]
MANDATORY_TIMING_HOOKS = ["gConfig.nowMs", "gConfig.nowUs", "gConfig.cooperativeYield"]
MANDATORY_ASYNC_TICK_TOKENS = [
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


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


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
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"

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

    for cmd in MANDATORY_COMMANDS:
        if re.search(rf"\b{re.escape(cmd)}\b", text) is None:
            fail(f"mandatory command '{cmd}' missing in {bringup_main.as_posix()}")

    if re.search(r"\bcfg\b", text) is None and re.search(r"\bsettings\b", text) is None:
        fail("either 'cfg' or 'settings' command must be present")

    for hook in MANDATORY_TIMING_HOOKS:
        if hook not in text:
            fail(f"timing hook assignment '{hook}' missing in {bringup_main.as_posix()}")

    for token in MANDATORY_ASYNC_TICK_TOKENS:
        if token not in text:
            fail(f"async tick status handling token '{token}' missing in {bringup_main.as_posix()}")

    check_destructive_confirmations(text, "Arduino CLI")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
