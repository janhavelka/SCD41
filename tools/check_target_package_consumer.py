#!/usr/bin/env python3
"""Build isolated public-API package consumers on native and Arduino S2/S3.

Strict dependency compatibility checks ensure packaging does not unnecessarily
restrict the framework-neutral core. No example headers, board fixture, bus,
pins, or application-specific defines are needed by these consumers.
"""
from __future__ import annotations

import configparser
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

from check_clean_consumer_compile import (
    CONSUMER_SOURCE, expand_args, find_library_root, safe_extract,
)

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENVIRONMENTS = ("native_consumer", "esp32s2_consumer", "esp32s3_consumer")

# Run the same zero-I2C public-contract consumer on the host, and compile/link
# it through Arduino's entry points for the reference targets. This is test
# firmware; building it does not claim execution on a physical board.
CONSUMER = CONSUMER_SOURCE.replace("int main()", "int runConsumer()") + r'''
#if defined(ARDUINO)
volatile int consumerResult = 0;
void setup() { consumerResult = runConsumer(); }
void loop() {}
#else
int main() { return runConsumer(); }
#endif
'''


def platformio_configuration() -> str:
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / "platformio.ini", encoding="utf-8")
    platform = config["dependency_pins"]["platform"]
    native = config["env:native"]["platform"]
    return f"""[env]
lib_compat_mode = strict
build_unflags = -std=gnu++11
build_flags = -std=gnu++17

[env:native_consumer]
platform = {native}

[env:esp32s2_consumer]
platform = {platform}
framework = arduino
board = esp32-s2-saola-1

[env:esp32s3_consumer]
platform = {platform}
framework = arduino
board = esp32-s3-devkitc-1
"""


def fail(message: str) -> int:
    print(f"Target package consumers FAILED: {message}")
    return 1


def main(arguments: list[str]) -> int:
    packages = expand_args(arguments)
    if len(packages) != 1 or not packages[0].is_file():
        return fail("provide exactly one packed .tar.gz library")
    with tempfile.TemporaryDirectory(prefix="scd41-package-consumers-") as tmp:
        project = pathlib.Path(tmp)
        unpacked = project / "unpacked"
        unpacked.mkdir()
        with tarfile.open(packages[0], "r:gz") as package:
            safe_extract(package, unpacked)
        library_root = find_library_root(unpacked)
        if library_root is None:
            return fail("package does not contain public headers and src/SCD41.cpp")

        (project / "src").mkdir()
        (project / "lib").mkdir()
        shutil.copytree(library_root, project / "lib" / "SCD41")
        (project / "platformio.ini").write_text(
            platformio_configuration(), encoding="utf-8", newline="\n")
        (project / "src" / "main.cpp").write_text(CONSUMER, encoding="utf-8", newline="\n")

        if os.name == "nt":
            wrapper = ROOT / "scripts" / "pio.cmd"
            if not wrapper.is_file():
                return fail(f"missing prescribed PlatformIO wrapper: {wrapper}")
            command = [os.environ.get("COMSPEC", "cmd.exe"), "/d", "/s", "/c", str(wrapper)]
        else:
            command = [sys.executable, "-m", "platformio"]
        command += ["run", "-d", str(project)]
        for environment in ENVIRONMENTS:
            command += ["-e", environment]
        result = subprocess.run(command, cwd=project, text=True, capture_output=True, timeout=900)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            return fail(f"PlatformIO exited with {result.returncode}")

        executable = project / ".pio/build/native_consumer" / (
            "program.exe" if os.name == "nt" else "program")
        ran = subprocess.run([str(executable)], text=True, capture_output=True, timeout=10)
        if ran.returncode != 0:
            return fail(f"native consumer exited with {ran.returncode}: {ran.stdout}{ran.stderr}")

    print("Target package consumers PASSED (native executed; Arduino S2/S3 compile/link only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
