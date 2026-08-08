#!/usr/bin/env python3
"""Enforce durable public naming and repository-hygiene decisions."""
from __future__ import annotations

import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
MOJIBAKE_MARKERS = ("\u00c3", "\u00e2", "\ufffd")
FORBIDDEN_PREFIXES = (
    ".doxygen/",
    "dist/",
    "docs/doxygen/",
    "docs/prompts/",
    "hil-results/",
    "prompts/",
)
FORBIDDEN_SUFFIXES = (
    ".log",
    ".pyc",
    ".runner.md",
    ".serial.txt",
    ".transcript.txt",
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def repository_files() -> set[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return {
        line.strip().replace("\\", "/")
        for line in result.stdout.splitlines()
        if line.strip()
    }


def check_local_links(files: set[str]) -> list[str]:
    errors: list[str] = []
    for relative in sorted(path for path in files if path.endswith(".md")):
        source = ROOT / relative
        if not source.is_file():
            continue
        for line_number, line in enumerate(
            source.read_text(encoding="utf-8").splitlines(), start=1
        ):
            for match in MARKDOWN_LINK.finditer(line):
                target = match.group(1).strip().strip("<>").split("#", 1)[0]
                if not target or "://" in target or target.startswith("mailto:"):
                    continue
                target = target.split(maxsplit=1)[0]
                if not (source.parent / target).resolve().exists():
                    errors.append(
                        f"{relative}:{line_number}: missing local link: {target}"
                    )
    return errors


def check_artifacts_and_encoding(files: set[str]) -> list[str]:
    errors: list[str] = []
    text_suffixes = {
        ".c", ".cc", ".cpp", ".h", ".hpp", ".ini", ".json", ".md",
        ".py", ".txt", ".yml", ".yaml",
    }
    for relative in sorted(files):
        lower = relative.lower()
        name = pathlib.PurePosixPath(lower).name
        if lower.startswith(FORBIDDEN_PREFIXES):
            errors.append(f"generated/one-time path is tracked: {relative}")
        if lower.endswith(FORBIDDEN_SUFFIXES):
            errors.append(f"generated/duplicate artifact is tracked: {relative}")
        if "not-run" in name or "not_run" in name:
            errors.append(f"NOT-RUN-only artifact path is tracked: {relative}")
        if "prompt" in name:
            errors.append(f"completed/task prompt artifact is tracked: {relative}")
        if name.startswith("scd41_hil_") and pathlib.PurePosixPath(name).suffix in {
            ".json", ".log", ".md",
        }:
            errors.append(f"generated HIL evidence artifact is tracked: {relative}")
        path = ROOT / relative
        if path.is_file() and path.suffix.lower() in text_suffixes:
            try:
                content = path.read_text(encoding="utf-8")
            except UnicodeDecodeError as exc:
                errors.append(f"{relative}: invalid UTF-8: {exc}")
                continue
            for marker in MOJIBAKE_MARKERS:
                if marker in content:
                    errors.append(
                        f"{relative}: contains mojibake marker {marker!r}"
                    )
                    break
    return errors


def main() -> int:
    errors: list[str] = []
    files = repository_files()
    public = read("include/SCD41/SCD41.h")
    status = read("include/SCD41/Status.h")
    core = read("src/SCD41.cpp")
    arduino = read("examples/01_basic_bringup_cli/main.cpp")
    idf = read("examples/idf/basic/main/main.cpp")
    tests = read("test/test_basic.cpp")
    ci = read(".github/workflows/ci.yml")
    doxyfile = read("Doxyfile")
    gitignore = read(".gitignore")
    hil = read("tools/scd41_hil_runner.py")
    idf_component = read("idf_component.yml")
    manifest = json.loads(read("library.json"))

    required_status = (
        "CRC_ERROR = CRC_MISMATCH",
        "constexpr const char* errorName(Err code)",
        "constexpr const char* toString(Err code)",
    )
    for token in required_status:
        if token not in status:
            errors.append(f"Status.h missing compatibility contract: {token}")

    enum_helpers = {
        "DriverState": "driverStateName",
        "SensorVariant": "sensorVariantName",
        "OperatingMode": "operatingModeName",
        "ModeEvidence": "modeEvidenceName",
        "OperationKind": "operationKindName",
        "OperationState": "operationStateName",
        "OperationOutcome": "operationOutcomeName",
        "EffectState": "effectStateName",
        "OperationPhase": "operationPhaseName",
    }
    for enum_type, helper in enum_helpers.items():
        if f"constexpr const char* {helper}({enum_type}" not in public:
            errors.append(f"public header missing {helper}({enum_type})")
        if f"constexpr const char* toString({enum_type}" not in public:
            errors.append(f"public header missing toString({enum_type})")

    required_accessors = (
        "bool isInitialized() const { return isBound(); }",
        "bool isOnline() const",
        "uint32_t lastOkMs() const",
        "uint32_t lastErrorMs() const",
        "Status lastError() const",
        "uint8_t consecutiveFailures() const",
        "uint32_t totalFailures() const",
        "uint32_t totalSuccess() const",
    )
    for token in required_accessors:
        if token not in public:
            errors.append(f"public header missing health compatibility accessor: {token}")

    for cli_name, cli in (("Arduino", arduino), ("ESP-IDF", idf)):
        for helper in enum_helpers.values():
            namespace = "app_driver" if cli_name == "Arduino" else "SCD41"
            qualified = f"{namespace}::{helper}("
            if qualified not in cli:
                errors.append(f"{cli_name} CLI does not use core {helper}()")
        error_call = "app_driver::errorName(" if cli_name == "Arduino" else "SCD41::errorName("
        if error_call not in cli:
            errors.append(f"{cli_name} CLI does not use core errorName()")

    obsolete_paths = (
        "examples/common/BusDiag.h",
        "examples/common/DriverCompat.h",
    )
    for relative in obsolete_paths:
        if (ROOT / relative).exists():
            errors.append(f"obsolete example wrapper restored: {relative}")

    active_text = "\n".join((public, core, arduino, idf))
    obsolete_tokens = (
        "_finishTransferFailure",
        "bus_diag::",
        "app_driver::Device",
        "app_driver::errToString",
    )
    for token in obsolete_tokens:
        if token in active_text:
            errors.append(f"obsolete parallel naming path restored: {token}")

    local_mapper = re.compile(
        r"(?:const\s+char\s*\*|const\s+char\s+\*)\s*"
        r"(?:errName|stateName|operationStateName|evidenceName|modeName|"
        r"operationName|outcomeName|effectName|variantName)\s*\("
    )
    for cli_name, cli in (("Arduino", arduino), ("ESP-IDF", idf)):
        if local_mapper.search(cli):
            errors.append(f"{cli_name} CLI restored a local enum-name mapper")

    for test_name in (
        "test_public_enum_name_helpers_are_exhaustive",
        "test_public_health_compatibility_accessors_match_transfer_channel",
    ):
        if f"void {test_name}()" not in tests or f"RUN_TEST({test_name});" not in tests:
            errors.append(f"missing registered regression: {test_name}")

    current_windows_docs = (
        "README.md",
        "CONTRIBUTING.md",
        "docs/porting/esp-idf.md",
        "tools/check_package_contents.py",
    )
    for relative in current_windows_docs:
        text = read(relative)
        if "python -m platformio" in text:
            errors.append(f"{relative} bypasses the Windows PlatformIO wrapper")
        if "scripts\\pio.cmd" not in text:
            errors.append(f"{relative} does not name scripts\\pio.cmd")

    description = str(manifest.get("description", ""))
    if "production-grade" in description.lower():
        errors.append("library.json description implies unproven production validation")
    if "production-grade" in idf_component.lower():
        errors.append("idf_component.yml implies unproven production validation")

    version = str(manifest.get("version", ""))
    major_minor = ".".join(version.split(".")[:2])
    security = read("SECURITY.md")
    if f"| {major_minor}.x release candidates | yes |" not in security:
        errors.append("SECURITY.md supported line is stale against library.json")
    if f"The `{version}` manifest is currently staged" not in security:
        errors.append("SECURITY.md staged manifest is stale against library.json")

    if not (ROOT / "docs/reports/naming-hygiene-20260808.md").is_file():
        errors.append("durable naming/hygiene report is missing")
    report = read("docs/reports/naming-hygiene-20260808.md")
    if f"Target version: {version}" not in report:
        errors.append("durable naming/hygiene report target version is stale")
    if f"staged {version} line" not in report:
        errors.append("durable naming/hygiene report metadata statement is stale")
    for required_input in (
        "docs/README.md",
        "docs/reports/naming-hygiene-20260808.md",
    ):
        if required_input not in doxyfile:
            errors.append(f"Doxygen input omits {required_input}")
    if "OUTPUT_DIRECTORY       = .doxygen" not in doxyfile:
        errors.append("Doxygen output owner is not the ignored .doxygen directory")
    if "hil-results/" not in gitignore:
        errors.append("transient HIL output directory is not ignored")
    for token in (
        "environment_metadata(args.invocation)",
        "platformio_command(\"--version\")",
        "allow_empty=True",
        '"repository_commit"',
        '"repository_status"',
        '"platformio_version"',
    ):
        if token not in hil:
            errors.append(f"HIL evidence metadata contract missing: {token}")
    if "python tools/check_repository_hygiene.py" not in ci:
        errors.append("CI does not run the repository hygiene guard")
    checkout_pin = (
        "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 "
        "# v7.0.1"
    )
    if ci.count(checkout_pin) != 5:
        errors.append("CI checkout jobs are not all pinned to audited v7.0.1")
    for token in (
        'DOXYGEN_VERSION: "1.17.0"',
        'DOXYGEN_SHA256: "75419ef4f446fc1c24ef12514b574e66e898ee6f527c6ae2ad84f91a905823c2"',
        'doxygen-${DOXYGEN_VERSION}.linux.bin.tar.gz',
        "sha256sum --check --strict",
    ):
        if token not in ci:
            errors.append(f"CI exact Doxygen toolchain contract missing: {token}")

    errors.extend(check_artifacts_and_encoding(files))
    errors.extend(check_local_links(files))

    if errors:
        print("Repository hygiene guard FAILED:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("Repository hygiene guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
