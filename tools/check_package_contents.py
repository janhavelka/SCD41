#!/usr/bin/env python3
from __future__ import annotations

import glob
import pathlib
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_PACKAGE_PATHS = (
    "library.json",
    "README.md",
    "LICENSE",
    "include/SCD41/SCD41.h",
    "include/SCD41/Version.h",
    "include/SCD41/Config.h",
    "include/SCD41/Status.h",
    "include/SCD41/CommandTable.h",
    "src/SCD41.cpp",
    "src/PlatformTime.h",
)

FORBIDDEN_PACKAGE_PATHS = (
    ".git/",
    ".github/",
    ".pio/",
    "dist/",
    "__pycache__/",
)

FORBIDDEN_PACKAGE_SUFFIXES = (
    ".elf",
    ".bin",
    ".map",
    ".o",
    ".obj",
    ".a",
    ".log",
)

FORBIDDEN_PACKAGE_NAME_SUFFIXES = (
    ".tar.gz",
    ".zip",
)


def fail(message: str) -> int:
    print(f"Package content check FAILED: {message}")
    return 1


def normalize_member(name: str) -> str:
    return name.replace("\\", "/").lstrip("./")


def member_contains(members: set[str], required: str) -> bool:
    return any(member == required or member.endswith(f"/{required}") for member in members)


def member_has_forbidden_path(members: set[str], forbidden: str) -> bool:
    return any(
        member == forbidden.rstrip("/") or f"/{forbidden}" in member
        for member in members
    )


def find_package(args: list[str]) -> pathlib.Path | None:
    if args:
        matches: list[pathlib.Path] = []
        for arg in args:
            path_arg = pathlib.Path(arg)
            if any(char in arg for char in "*?[]"):
                pattern = arg if path_arg.is_absolute() else str(ROOT / arg)
                matches.extend(pathlib.Path(path) for path in glob.glob(pattern))
            else:
                matches.append(path_arg)
        packages = sorted(
            (path.resolve() for path in matches if path.is_file()),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
        return packages[0] if packages else pathlib.Path(args[0]).resolve()
    packages = sorted(
        list(ROOT.glob("*.tar.gz")) + list((ROOT / "dist").glob("*.tar.gz")),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    return packages[0] if packages else None


def main(args: list[str]) -> int:
    package_path = find_package(args)
    if package_path is None:
        return fail("no package tarball found; run `python -m platformio pkg pack` first")
    if not package_path.is_file():
        return fail(f"package tarball not found: {package_path}")

    with tarfile.open(package_path, "r:gz") as package:
        members = {normalize_member(member.name) for member in package.getmembers()}

    missing = [path for path in REQUIRED_PACKAGE_PATHS if not member_contains(members, path)]
    if missing:
        return fail(f"missing required package paths: {missing}")

    forbidden = [
        path for path in FORBIDDEN_PACKAGE_PATHS if member_has_forbidden_path(members, path)
    ]
    if forbidden:
        return fail(f"forbidden build/repo paths are packaged: {forbidden}")

    forbidden_suffixes = sorted(
        member
        for member in members
        if pathlib.PurePosixPath(member).suffix in FORBIDDEN_PACKAGE_SUFFIXES
    )
    if forbidden_suffixes:
        return fail(f"forbidden build artifact files are packaged: {forbidden_suffixes}")

    forbidden_archives = sorted(
        member for member in members if member.endswith(FORBIDDEN_PACKAGE_NAME_SUFFIXES)
    )
    if forbidden_archives:
        return fail(f"forbidden archive files are packaged: {forbidden_archives}")

    print(f"Package content check PASSED: {package_path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
