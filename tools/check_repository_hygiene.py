#!/usr/bin/env python3
"""Enforce durable public naming and repository-hygiene decisions."""
from __future__ import annotations

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def main() -> int:
    errors: list[str] = []
    public = read("include/SCD41/SCD41.h")
    status = read("include/SCD41/Status.h")
    core = read("src/SCD41.cpp")
    arduino = read("examples/01_basic_bringup_cli/main.cpp")
    idf = read("examples/idf/basic/main/main.cpp")
    tests = read("test/test_basic.cpp")
    ci = read(".github/workflows/ci.yml")
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

    version = str(manifest.get("version", ""))
    major_minor = ".".join(version.split(".")[:2])
    security = read("SECURITY.md")
    if f"| {major_minor}.x release candidates | yes |" not in security:
        errors.append("SECURITY.md supported line is stale against library.json")
    if f"The `{version}` manifest is currently staged" not in security:
        errors.append("SECURITY.md staged manifest is stale against library.json")

    if not (ROOT / "docs/reports/naming-hygiene-20260808.md").is_file():
        errors.append("durable naming/hygiene report is missing")
    if "python tools/check_repository_hygiene.py" not in ci:
        errors.append("CI does not run the repository hygiene guard")

    if errors:
        print("Repository hygiene guard FAILED:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("Repository hygiene guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
