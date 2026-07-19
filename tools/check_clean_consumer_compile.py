#!/usr/bin/env python3
"""Compile, link, and run a public-API consumer from source or package."""
from __future__ import annotations

import glob
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

CONSUMER_SOURCE = r'''
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include <SCD41/CommandTable.h>
#include <SCD41/Config.h>
#include <SCD41/SCD41.h>
#include <SCD41/Status.h>
#include <SCD41/Version.h>

static_assert(SCD41::VERSION_CODE ==
                  (SCD41::VERSION_MAJOR * 10000U + SCD41::VERSION_MINOR * 100U +
                   SCD41::VERSION_PATCH),
              "version metadata must be coherent");
static_assert(SCD41::cmd::I2C_ADDRESS == 0x62, "address must be public");
static_assert(!std::is_copy_constructible<SCD41::SCD41>::value,
              "driver owns operation identity and must not be copied");

static unsigned transferCalls = 0;

SCD41::TransferResult transfer(const SCD41::TransferRequest& request, void*) {
  ++transferCalls;
  return SCD41::TransferResult::Ok(request.writeLength + request.readLength, 101U);
}

int main() {
  SCD41::Config config;
  config.transfer = transfer;
  config.transferTimeoutMs = 20U;

  SCD41::SCD41 sensor;
  if (!sensor.begin(config).ok() || transferCalls != 0U || !sensor.isBound()) {
    return 1;
  }

  const SCD41::OperationRequest request =
      SCD41::OperationRequest::make(SCD41::OperationKind::ATTACH);
  const SCD41::OperationLimits limits = SCD41::SCD41::limits(request.kind);
  if (limits.maxCallbacks == 0U || limits.maxWaitMs == 0U) {
    return 2;
  }

  SCD41::OperationOptions options;
  options.requestId = 41U;
  options.nowMs = 100U;
  options.deadlineMs = options.nowMs + limits.maxWaitMs +
      static_cast<uint32_t>(limits.maxCallbacks) * config.transferTimeoutMs + 1000U;
  SCD41::OperationId id;
  const SCD41::Status started = sensor.start(request, options, id);
  if (!started.inProgress() || transferCalls != 0U || id.requestId != 41U) {
    std::fprintf(stderr, "start code=%u calls=%u request=%lu detail=%ld msg=%s\n",
                 static_cast<unsigned>(started.code), transferCalls,
                 static_cast<unsigned long>(id.requestId),
                 static_cast<long>(started.detail), started.msg);
    return 3;
  }

  if (!sensor.cancel(id, 100U).ok() || transferCalls != 0U) {
    return 4;
  }

  SCD41::OperationResult result;
  if (!sensor.takeResult(id, result).ok() ||
      result.id != id || result.kind != SCD41::OperationKind::ATTACH ||
      result.outcome != SCD41::OperationOutcome::CANCELLED || transferCalls != 0U) {
    return 5;
  }
  if (sensor.takeResult(id, result).ok()) {
    return 6;
  }

  sensor.end();
  return transferCalls == 0U ? 0 : 7;
}
'''


def fail(message: str) -> int:
    print(f"Clean consumer check FAILED: {message}")
    return 1


def expand_args(args: list[str]) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    for argument in args:
        matches = glob.glob(argument)
        paths.extend(pathlib.Path(match) for match in matches)
        if not matches:
            paths.append(pathlib.Path(argument))
    return paths


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
    if (root / "include" / "SCD41" / "SCD41.h").is_file() and (root / "src" / "SCD41.cpp").is_file():
        return root
    for header in root.rglob("include/SCD41/SCD41.h"):
        candidate = header.parent.parent.parent
        if (candidate / "src" / "SCD41.cpp").is_file():
            return candidate
    return None


def compiler_command() -> list[str] | None:
    configured = os.environ.get("CXX")
    for candidate in ([configured] if configured else []) + ["c++", "g++", "clang++"]:
        if candidate and shutil.which(candidate):
            return [candidate]
    return None


def compile_link_run(library_root: pathlib.Path) -> int:
    compiler = compiler_command()
    if compiler is None:
        return fail("no C++ compiler found; set CXX or install c++/g++/clang++")

    with tempfile.TemporaryDirectory(prefix="scd41-consumer-") as tmp:
        tmp_path = pathlib.Path(tmp)
        source = tmp_path / "consumer.cpp"
        executable = tmp_path / ("consumer.exe" if os.name == "nt" else "consumer")
        source.write_text(CONSUMER_SOURCE, encoding="utf-8", newline="\n")
        command = compiler + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(library_root / "include"),
            str(source), str(library_root / "src" / "SCD41.cpp"),
            "-o", str(executable),
        ]
        built = subprocess.run(command, text=True, capture_output=True)
        if built.returncode != 0:
            sys.stdout.write(built.stdout)
            sys.stderr.write(built.stderr)
            return fail(f"compiler/linker exited with {built.returncode}")
        ran = subprocess.run([str(executable)], text=True, capture_output=True)
        if ran.returncode != 0:
            sys.stdout.write(ran.stdout)
            sys.stderr.write(ran.stderr)
            return fail(f"consumer exited with {ran.returncode}")
    return 0


def check_path(path: pathlib.Path) -> int:
    if path.is_file() and path.name.endswith(".tar.gz"):
        with tempfile.TemporaryDirectory(prefix="scd41-package-") as tmp:
            tmp_path = pathlib.Path(tmp)
            with tarfile.open(path, "r:gz") as package:
                safe_extract(package, tmp_path)
            library_root = find_library_root(tmp_path)
            if library_root is None:
                return fail(f"package lacks public headers or src/SCD41.cpp: {path}")
            result = compile_link_run(library_root)
            if result == 0:
                print(f"Clean package consumer check PASSED: {path.name}")
            return result

    library_root = find_library_root(path.resolve())
    if library_root is None:
        return fail(f"library root not found under {path.resolve()}")
    result = compile_link_run(library_root)
    if result == 0:
        print(f"Clean source consumer check PASSED: {library_root}")
    return result


def main(args: list[str]) -> int:
    for path in expand_args(args) if args else [ROOT]:
        result = check_path(path)
        if result != 0:
            return result
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
