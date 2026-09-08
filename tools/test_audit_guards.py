#!/usr/bin/env python3
"""Regressions for package boundaries and native-IDF source checks."""
from __future__ import annotations

import contextlib
import io
import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

import check_cli_contract as arduino
import check_idf_example_contract as idf
import check_package_contents as package


class PackageGuardTests(unittest.TestCase):
    def check_archive(self, names: list[str]) -> tuple[int, str]:
        with tempfile.TemporaryDirectory(prefix="scd41-package-guard-") as temporary:
            archive = pathlib.Path(temporary) / "package.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                for name in names:
                    output.addfile(tarfile.TarInfo(name))
            messages = io.StringIO()
            with contextlib.redirect_stdout(messages):
                result = package.main([str(archive)])
            return result, messages.getvalue()

    def test_normalization_preserves_dotted_paths(self) -> None:
        for name in (".github/workflows/ci.yml", "./.github/workflows/ci.yml",
                     "././.github/workflows/ci.yml", r".\.github\workflows\ci.yml"):
            with self.subTest(name=name):
                self.assertEqual(package.normalize_member(name), ".github/workflows/ci.yml")

    def test_forbidden_directories_at_every_package_depth(self) -> None:
        # Include empty directory entries: archives need not contain children
        # or trailing separators for a forbidden directory to be present.
        for forbidden in package.FORBIDDEN_PACKAGE_PATHS:
            for prefix in ("", "./", "package/", "./package/nested/"):
                for suffix in ("", "/", "/child.txt"):
                    name = prefix + forbidden.rstrip("/") + suffix
                    with self.subTest(name=name):
                        result, message = self.check_archive(
                            list(package.REQUIRED_PACKAGE_PATHS) + [name]
                        )
                        self.assertEqual(result, 1)
                        self.assertIn("forbidden build/repo paths", message)

    def test_allowed_lookalikes_and_package_prefixes(self) -> None:
        lookalikes = [".github-notes/readme.txt", "github/workflows/ci.yml",
                     "docs/reports-old/readme.txt", "docs/myreports/readme.txt",
                     "distribution/readme.txt", "__pycache__-notes/readme.txt"]
        for prefix in ("", "package/"):
            with self.subTest(prefix=prefix):
                names = [prefix + name for name in package.REQUIRED_PACKAGE_PATHS]
                names.extend(prefix + name for name in lookalikes)
                result, message = self.check_archive(names)
                self.assertEqual(result, 0, message)

    def test_fixed_name_idf_component_is_required(self) -> None:
        wrapper = "examples/idf/basic/components/SCD41/CMakeLists.txt"
        result, message = self.check_archive(
            [name for name in package.REQUIRED_PACKAGE_PATHS if name != wrapper]
        )
        self.assertEqual(result, 1)
        self.assertIn(wrapper, message)


class CliGuardTests(unittest.TestCase):
    def test_duplicated_contract_tables_remain_aligned(self) -> None:
        self.assertEqual(arduino.MANDATORY_COMMANDS, idf.MANDATORY_COMMANDS)
        self.assertEqual(arduino.FORBIDDEN_DRIVER_CALLS, idf.FORBIDDEN_DRIVER_CALLS)
        source = arduino.MAIN.read_text(encoding="utf-8")
        self.assertEqual(arduino.command_names(source), idf.command_names(source))
        self.assertEqual(arduino.help_command_names(source), idf.help_command_names(source))

    def test_current_idf_example_passes(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(idf.main(), 0)

    def test_idf_guard_rejects_forbidden_include_literals(self) -> None:
        original_read = pathlib.Path.read_text
        for injection, expected_error in (
            ('#include "../../../../examples/01_basic_bringup_cli/main.cpp"\n',
             "Arduino source reuse"),
            ('#include <Arduino.h>\n', "Arduino header"),
            ('#include "driver/i2c.h"\n', "legacy I2C header"),
        ):
            with self.subTest(injection=injection):
                def read_mutation(path, *args, **kwargs):
                    source = original_read(path, *args, **kwargs)
                    return injection + source if path == idf.IDF_MAIN else source

                messages = io.StringIO()
                with mock.patch.object(pathlib.Path, "read_text", new=read_mutation), \
                        contextlib.redirect_stdout(messages), \
                        self.assertRaises(SystemExit) as failure:
                    idf.main()
                self.assertEqual(failure.exception.code, 1)
                self.assertIn(expected_error, messages.getvalue())


if __name__ == "__main__":
    unittest.main()
