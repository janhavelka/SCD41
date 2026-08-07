#!/usr/bin/env python3
"""Build a packed SCD41 consumer for TunnelMonitor-node's exact ESP32 target."""
from __future__ import annotations

import glob
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

PLATFORM_URL = "https://github.com/pioarduino/platform-espressif32/releases/download/54.03.20/platform-espressif32.zip"
ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD_FIXTURE = ROOT / "tools" / "fixtures" / "esp32-s3-wroom-n16r8.json"
BOARD_FIXTURE_SOURCE_REVISION = "b708f511964db6c51e949e99c67820476f00f9c7"

PLATFORMIO_INI = f"""[platformio]
default_envs = target_consumer

[env:target_consumer]
platform = {PLATFORM_URL}
board = esp32-s3-wroom-n16r8
framework = arduino
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi
board_build.psram_type = opi
build_unflags =
  -std=gnu++11
build_flags =
  -std=gnu++17
  -Ilib/SCD41/examples
  -DTUNNELMONITOR_DIAGNOSTICS_ENGLISH=1
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_LOOP_STACK_SIZE=24576
  -DBOARD_HAS_PSRAM
  -DTUNNELMONITOR_TARGET_ESP32S3=1
  -DTUNNELMONITOR_REQUIRE_PSRAM=1
  -DTUNNELMONITOR_ENABLE_I2C=1
  -DTUNNELMONITOR_ENABLE_MEASUREMENT=1
"""

CONSUMER = r'''
#include <Arduino.h>
#include <Wire.h>
#include <SCD41/SCD41.h>
#include "common/I2cTransport.h"

namespace {
SCD41::SCD41 sensor;
volatile int consumerResult = 0;
}  // namespace

void setup() {
  if (!transport::initWire(8, 9, 400000U, 20U)) {
    consumerResult = 1;
    return;
  }
  SCD41::Config config;
  config.transfer = transport::wireTransfer;
  config.transferUser = &Wire;
  config.transferTimeoutMs = 20U;
  if (!sensor.begin(config).ok()) {
    consumerResult = 2;
    return;
  }

  const auto request =
      SCD41::OperationRequest::make(SCD41::OperationKind::ATTACH);
  const auto limits = SCD41::SCD41::limits(request.kind);
  SCD41::OperationOptions options;
  options.requestId = 1U;
  options.nowMs = millis();
  options.deadlineMs = options.nowMs + limits.maxWaitMs +
      static_cast<uint32_t>(limits.maxCallbacks) * config.transferTimeoutMs + 1000U;
  SCD41::OperationId id;
  if (!sensor.start(request, options, id).inProgress()) {
    consumerResult = 3;
    return;
  }
  if (!sensor.cancel(id, options.nowMs).ok()) {
    consumerResult = 4;
    return;
  }
  SCD41::OperationResult result;
  consumerResult = sensor.takeResult(id, result).ok() ? 0 : 5;
}

void loop() {}
'''


def fail(message: str) -> int:
    print(f"Exact target package consumer FAILED: {message}")
    return 1


def safe_extract(package: tarfile.TarFile, destination: pathlib.Path) -> None:
    root = destination.resolve()
    for member in package.getmembers():
        target = (root / member.name).resolve()
        try:
            target.relative_to(root)
        except ValueError as exc:
            raise RuntimeError(f"unsafe archive member: {member.name}") from exc
    try:
        package.extractall(root, filter="data")
    except TypeError:
        package.extractall(root)


def find_library_root(root: pathlib.Path) -> pathlib.Path | None:
    if (root / "include" / "SCD41" / "SCD41.h").is_file():
        return root
    for header in root.rglob("include/SCD41/SCD41.h"):
        return header.parent.parent.parent
    return None


def main(arguments: list[str]) -> int:
    matches: list[pathlib.Path] = []
    for argument in arguments:
        expanded = glob.glob(argument)
        matches.extend(pathlib.Path(path) for path in expanded)
    if len(matches) != 1 or not matches[0].is_file():
        return fail("provide exactly one packed .tar.gz library")
    if not BOARD_FIXTURE.is_file():
        return fail(f"missing exact-target board fixture: {BOARD_FIXTURE}")
    with tempfile.TemporaryDirectory(prefix="scd41-target-consumer-") as tmp:
        root = pathlib.Path(tmp)
        unpacked = root / "unpacked"
        unpacked.mkdir()
        with tarfile.open(matches[0], "r:gz") as package:
            safe_extract(package, unpacked)
        library_root = find_library_root(unpacked)
        if library_root is None:
            return fail("package does not contain include/SCD41/SCD41.h")

        project = root / "project"
        (project / "src").mkdir(parents=True)
        (project / "lib").mkdir()
        (project / "boards").mkdir()
        shutil.copytree(library_root, project / "lib" / "SCD41")
        shutil.copy2(BOARD_FIXTURE, project / "boards" / BOARD_FIXTURE.name)
        (project / "platformio.ini").write_text(PLATFORMIO_INI, encoding="utf-8", newline="\n")
        (project / "src" / "main.cpp").write_text(CONSUMER, encoding="utf-8", newline="\n")

        if os.name == "nt":
            wrapper = ROOT / "scripts" / "pio.cmd"
            if not wrapper.is_file():
                return fail(f"missing prescribed PlatformIO wrapper: {wrapper}")
            command = [
                os.environ.get("COMSPEC", "cmd.exe"),
                "/d",
                "/s",
                "/c",
                str(wrapper),
                "run",
                "-d",
                str(project),
                "-e",
                "target_consumer",
            ]
        else:
            command = [
                sys.executable,
                "-m",
                "platformio",
                "run",
                "-d",
                str(project),
                "-e",
                "target_consumer",
            ]
        result = subprocess.run(command, cwd=project, text=True, capture_output=True)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            return fail(f"PlatformIO exited with {result.returncode}")

    print(
        "Exact target package consumer PASSED "
        f"(board contract from TunnelMonitor-node {BOARD_FIXTURE_SOURCE_REVISION})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
