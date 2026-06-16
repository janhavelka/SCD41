#!/usr/bin/env python3
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

#include <SCD41/CommandTable.h>
#include <SCD41/Config.h>
#include <SCD41/SCD41.h>
#include <SCD41/Status.h>
#include <SCD41/Version.h>

static_assert(SCD41::VERSION_CODE ==
                  (SCD41::VERSION_MAJOR * 10000U + SCD41::VERSION_MINOR * 100U +
                   SCD41::VERSION_PATCH),
              "Version metadata must compile");
static_assert(SCD41::cmd::I2C_ADDRESS == 0x62, "SCD41 address must be public");

int main() {
  SCD41::Config cfg;
  SCD41::Status st = SCD41::Status::Ok();
  SCD41::RawSample raw;
  SCD41::CompensatedSample compensated;
  SCD41::SCD41 sensor;

  (void)cfg;
  (void)raw;
  (void)compensated;
  return sensor.isInitialized() || !st.ok() ? 1 : 0;
}
'''


def fail(message: str) -> int:
  print(f"Clean consumer compile check FAILED: {message}")
  return 1


def expand_args(args: list[str]) -> list[pathlib.Path]:
  expanded: list[pathlib.Path] = []
  for arg in args:
    matches = glob.glob(arg)
    if matches:
      expanded.extend(pathlib.Path(match) for match in matches)
    else:
      expanded.append(pathlib.Path(arg))
  return expanded


def safe_extract(package: tarfile.TarFile, destination: pathlib.Path) -> None:
  dest = destination.resolve()
  for member in package.getmembers():
    target = (dest / member.name).resolve()
    try:
      target.relative_to(dest)
    except ValueError as exc:
      raise RuntimeError(f"refusing unsafe archive member: {member.name}")
  try:
    package.extractall(dest, filter="data")
  except TypeError:
    package.extractall(dest)


def find_include_root(root: pathlib.Path) -> pathlib.Path | None:
  direct = root / "include" / "SCD41" / "SCD41.h"
  if direct.is_file():
    return root / "include"
  for header in root.rglob("include/SCD41/SCD41.h"):
    return header.parent.parent
  return None


def compiler_command() -> list[str] | None:
  env_cxx = os.environ.get("CXX")
  candidates = [env_cxx] if env_cxx else []
  candidates.extend(["c++", "g++", "clang++"])
  for candidate in candidates:
    if candidate and shutil.which(candidate):
      return [candidate]
  return None


def compile_consumer(include_root: pathlib.Path) -> int:
  compiler = compiler_command()
  if compiler is None:
    return fail("no C++ compiler found; set CXX or install c++/g++/clang++")

  with tempfile.TemporaryDirectory(prefix="scd41-consumer-") as tmp:
    tmp_path = pathlib.Path(tmp)
    source = tmp_path / "consumer.cpp"
    obj = tmp_path / "consumer.o"
    source.write_text(CONSUMER_SOURCE, encoding="utf-8", newline="\n")
    cmd = compiler + [
      "-std=c++17",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-I",
      str(include_root),
      "-c",
      str(source),
      "-o",
      str(obj),
    ]
    result = subprocess.run(cmd, text=True, capture_output=True)
    if result.returncode != 0:
      sys.stdout.write(result.stdout)
      sys.stderr.write(result.stderr)
      return fail(f"compiler exited with {result.returncode}")
  return 0


def check_path(path: pathlib.Path) -> int:
  if path.is_file() and path.name.endswith(".tar.gz"):
    with tempfile.TemporaryDirectory(prefix="scd41-package-") as tmp:
      tmp_path = pathlib.Path(tmp)
      with tarfile.open(path, "r:gz") as package:
        safe_extract(package, tmp_path)
      include_root = find_include_root(tmp_path)
      if include_root is None:
        return fail(f"package does not contain include/SCD41/SCD41.h: {path}")
      result = compile_consumer(include_root)
      if result == 0:
        print(f"Clean consumer compile check PASSED: {path.name}")
      return result

  root = path.resolve()
  include_root = find_include_root(root)
  if include_root is None:
    return fail(f"include/SCD41/SCD41.h not found under {root}")
  result = compile_consumer(include_root)
  if result == 0:
    print(f"Clean consumer compile check PASSED: {root}")
  return result


def main(args: list[str]) -> int:
  paths = expand_args(args) if args else [ROOT]
  for path in paths:
    result = check_path(path)
    if result != 0:
      return result
  return 0


if __name__ == "__main__":
  raise SystemExit(main(sys.argv[1:]))
