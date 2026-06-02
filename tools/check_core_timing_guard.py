#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys
from typing import Dict

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_DIRS = ("src", "include")
VALID_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

FORBIDDEN_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]'
    r"(?:Arduino\.h|Wire\.h|driver/[^>\"]+|esp_[^>\"]+|freertos/[^>\"]+|"
    r"sdkconfig\.h|soc/[^>\"]+|hal/[^>\"]+)"
    r'[>"]',
    re.MULTILINE,
)

FORBIDDEN_TIMING_CALLS = {
    "delay": re.compile(r"\bdelay\s*\("),
    "delayMicroseconds": re.compile(r"\bdelayMicroseconds\s*\("),
    "millis": re.compile(r"\bmillis\s*\("),
    "micros": re.compile(r"\bmicros\s*\("),
    "yield": re.compile(r"\byield\s*\("),
    "vTaskDelay": re.compile(r"\bvTaskDelay\s*\("),
    "vTaskDelayUntil": re.compile(r"\bvTaskDelayUntil\s*\("),
    "xTaskGetTickCount": re.compile(r"\bxTaskGetTickCount\s*\("),
    "esp_timer_get_time": re.compile(r"\besp_timer_get_time\s*\("),
    "sleep": re.compile(r"\bsleep\s*\("),
    "usleep": re.compile(r"\busleep\s*\("),
    "std::this_thread::sleep_for": re.compile(r"\bstd::this_thread::sleep_for\s*\("),
}

FORBIDDEN_FRAMEWORK_TOKENS = {
    "TwoWire": re.compile(r"\bTwoWire\b"),
    "HardwareSerial": re.compile(r"\bHardwareSerial\b"),
    "Stream": re.compile(r"\bStream\b"),
    "Print": re.compile(r"\bPrint\b"),
    "esp_err_t": re.compile(r"\besp_err_t\b"),
    "TickType_t": re.compile(r"\bTickType_t\b"),
    "TaskHandle_t": re.compile(r"\bTaskHandle_t\b"),
    "SemaphoreHandle_t": re.compile(r"\bSemaphoreHandle_t\b"),
}

FORBIDDEN_HEAP_TOKENS = {
    "new": re.compile(r"\bnew\b"),
    "malloc": re.compile(r"\bmalloc\s*\("),
    "calloc": re.compile(r"\bcalloc\s*\("),
    "realloc": re.compile(r"\brealloc\s*\("),
    "free": re.compile(r"\bfree\s*\("),
    "std::vector": re.compile(r"\bstd::vector\b"),
    "std::string": re.compile(r"\bstd::string\b"),
    "std::function": re.compile(r"\bstd::function\b"),
    "std::unique_ptr": re.compile(r"\bstd::unique_ptr\b"),
    "std::shared_ptr": re.compile(r"\bstd::shared_ptr\b"),
}

FORBIDDEN_LOGGING_TOKENS = {
    "Serial": re.compile(r"\bSerial\b"),
    "printf": re.compile(r"\bprintf\s*\("),
    "puts": re.compile(r"\bputs\s*\("),
    "std::cout": re.compile(r"\bstd::cout\b"),
    "std::cerr": re.compile(r"\bstd::cerr\b"),
    "ESP_LOG": re.compile(r"\bESP_LOG[A-Z_]*\s*\("),
    "ets_printf": re.compile(r"\bets_printf\s*\("),
    "LOG_*": re.compile(r"\bLOG_[A-Z0-9_]+\b"),
}

BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')

ALLOWED_FINDINGS: Dict[str, Dict[str, int]] = {}


def strip_non_code(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    text = LINE_COMMENT_RE.sub("", text)
    return STRING_RE.sub('""', text)


def collect_sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for dirname in SCAN_DIRS:
        root = ROOT / dirname
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in VALID_SUFFIXES:
                files.append(path)
    return files


def record_findings(findings: dict[str, dict[str, int]], rel: str,
                    category: str, patterns: dict[str, re.Pattern[str]], text: str) -> None:
    for label, pattern in patterns.items():
        count = len(pattern.findall(text))
        if count > 0:
            findings.setdefault(rel, {})[f"{category}:{label}"] = count


def main() -> int:
    observed: dict[str, dict[str, int]] = {}

    for path in collect_sources():
        rel = path.relative_to(ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = strip_non_code(raw)

        include_count = len(FORBIDDEN_INCLUDE_RE.findall(raw))
        if include_count > 0:
            observed.setdefault(rel, {})["include:framework"] = include_count

        record_findings(observed, rel, "timing", FORBIDDEN_TIMING_CALLS, code)
        record_findings(observed, rel, "framework", FORBIDDEN_FRAMEWORK_TOKENS, code)
        record_findings(observed, rel, "heap", FORBIDDEN_HEAP_TOKENS, code)
        record_findings(observed, rel, "logging", FORBIDDEN_LOGGING_TOKENS, code)

    errors: list[str] = []
    for rel, findings in observed.items():
        expected = ALLOWED_FINDINGS.get(rel, {})
        for label, count in findings.items():
            exp = expected.get(label, 0)
            if count != exp:
                errors.append(f"{rel}: {label} observed={count}, expected={exp}")

    for rel, expected in ALLOWED_FINDINGS.items():
        actual = observed.get(rel, {})
        for label, exp in expected.items():
            count = actual.get(label, 0)
            if count != exp:
                errors.append(f"{rel}: {label} observed={count}, expected={exp}")

    if errors:
        print("Core timing guard FAILED:")
        for err in errors:
            print(f"- {err}")
        return 1

    print("Core timing guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
