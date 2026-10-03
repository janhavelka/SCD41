#!/usr/bin/env python3
"""Regressions for package boundaries and native-IDF source checks."""
from __future__ import annotations

import contextlib
import io
import pathlib
import os
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

import check_cli_contract as arduino
import check_idf_example_contract as idf
import check_package_contents as package
import check_clean_consumer_compile as consumer


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
    def run_transport_fixture(self, harness: str, headers: dict[str, str],
                              sources: tuple[str, ...] = ()) -> None:
        compiler = consumer.compiler_command()
        self.assertIsNotNone(compiler, "native C++ compiler required for adapter regression")
        with tempfile.TemporaryDirectory(prefix="scd41-adapter-regression-") as directory:
            root = pathlib.Path(directory)
            for name, content in headers.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            cpp = root / "adapter.cpp"
            exe = root / ("adapter.exe" if os.name == "nt" else "adapter")
            cpp.write_text(harness, encoding="utf-8")
            command = compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                                  "-I" + str(root), "-I" + str(arduino.ROOT),
                                  "-I" + str(arduino.ROOT / "include"), str(cpp)]
            command += [str(arduino.ROOT / source) for source in sources]
            built = subprocess.run(command + ["-o", str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_wire_adapter_preserves_failures_and_timeout_bounds(self) -> None:
        # Run the shipped adapter against controlled Wire outcomes, including
        # generic faults in expected-NACK phases. This is not hardware evidence.
        headers = {
            "Arduino.h": "#pragma once\n#include <cstdint>\ninline uint32_t millis() { return 42U; }\n",
            "Wire.h": r'''
#pragma once
#include <cstddef>
#include <cstdint>
struct TwoWire {
  bool beginOk = true, clockOk = true;
  uint16_t timeout = 0;
  uint8_t status = 0;
  size_t writeCapacity = 128, received = 3;
  int attempts = 0, reads = 0;
  bool begin(int, int) { return beginOk; }
  bool setClock(uint32_t) { return clockOk; }
  void setTimeOut(uint16_t value) { timeout = value; }
  void beginTransmission(uint8_t) {}
  size_t write(const uint8_t*, size_t size) { return size < writeCapacity ? size : writeCapacity; }
  uint8_t endTransmission(bool) { ++attempts; return status; }
  size_t requestFrom(uint8_t, size_t, bool) { ++attempts; reads = 0; return received; }
  int available() { return reads < static_cast<int>(received); }
  int read() { ++reads; return 0x42; }
};
inline TwoWire Wire;
''',
        }
        self.run_transport_fixture(r'''
#include <cassert>
#include <initializer_list>
#include "examples/common/I2cTransport.h"
int main() {
  using namespace SCD41;
  Wire.clockOk = false;
  assert(!transport::initWire(8, 9, 400000, 50));
  Wire.clockOk = true;
  Wire.beginOk = false;
  assert(!transport::initWire(8, 9, 400000, 50));
  Wire.beginOk = true;
  assert(!transport::initWire(8, 9, 400000, 0));
  assert(!transport::initWire(8, 9, 400000, 65536));
  assert(transport::initWire(8, 9, 400000, 50) && Wire.timeout == 50);
  uint8_t bytes[3] = {};
  TransferRequest request;
  request.writeData = bytes;
  request.writeLength = 2;
  request.timeoutMs = 50;
  for (auto intent : {TransferIntent::NORMAL, TransferIntent::EXPECTED_WRITE_NACK}) {
    request.intent = intent;
    for (uint8_t status = 0; status <= 6; ++status) {
      Wire.status = status;
      Wire.attempts = 0;
      auto result = transport::wireTransfer(request, &Wire);
      assert(Wire.attempts == 1 && result.completedMs == 42U);
      const TransferCode codes[] = {TransferCode::OK, TransferCode::SHORT_TRANSFER,
          TransferCode::NACK, TransferCode::NACK, TransferCode::BUS_ERROR,
          TransferCode::TIMEOUT, TransferCode::FAILED};
      assert(result.code == codes[status]);
      if (status == 0) {
        assert(result.disposition == TransferDisposition::COMPLETE && result.bytesTransferred == 2);
      } else {
        assert(result.disposition == TransferDisposition::INDETERMINATE);
      }
    }
  }
  Wire.attempts = 0;
  request.timeoutMs = 65536;
  auto result = transport::wireTransfer(request, &Wire);
  assert(result.disposition == TransferDisposition::NOT_STARTED && Wire.attempts == 0);
  request.timeoutMs = 50;
  result = transport::wireTransfer(request, nullptr);
  assert(result.disposition == TransferDisposition::NOT_STARTED && Wire.attempts == 0);
  Wire.writeCapacity = 1;
  result = transport::wireTransfer(request, &Wire);
  assert(result.code == TransferCode::SHORT_TRANSFER && result.bytesTransferred == 0);
  request.writeData = nullptr;
  request.writeLength = 0;
  request.readData = bytes;
  request.readLength = sizeof(bytes);
  for (size_t received = 0; received <= sizeof(bytes); ++received) {
    Wire.received = received;
    Wire.attempts = 0;
    result = transport::wireTransfer(request, &Wire);
    assert(Wire.attempts == 1 && result.completedMs == 42U);
    if (received == 0) assert(result.code == TransferCode::FAILED);
    else if (received < sizeof(bytes)) assert(result.code == TransferCode::SHORT_TRANSFER);
    else assert(result.code == TransferCode::OK && bytes[2] == 0x42);
  }
}
''', headers)

    def test_idf_adapter_preserves_nack_and_generic_fault_distinction(self) -> None:
        headers = {
            "esp_err.h": r'''
#pragma once
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 0x102,
    ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_TIMEOUT = 0x107, ESP_ERR_INVALID_RESPONSE = 0x108;
''',
            "esp_timer.h": "#pragma once\n#include <cstdint>\ninline int64_t esp_timer_get_time() { return 42000; }\n",
            "esp_idf_version.h": "#pragma once\n#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))\n#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6,0,1)\n",
            "driver/i2c_master.h": r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <esp_err.h>
using i2c_master_dev_handle_t = void*;
inline esp_err_t returnedError = ESP_OK;
inline int attempts = 0, lastTimeout = 0;
inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t*, size_t, int timeout) {
  ++attempts; lastTimeout = timeout; return returnedError;
}
inline esp_err_t i2c_master_receive(i2c_master_dev_handle_t, uint8_t*, size_t, int timeout) {
  ++attempts; lastTimeout = timeout; return returnedError;
}
inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t*, size_t,
                                           uint8_t*, size_t, int timeout) {
  ++attempts; lastTimeout = timeout; return returnedError;
}
''',
        }
        self.run_transport_fixture(r'''
#include <cassert>
#include <initializer_list>
#include "examples/idf/basic/main/IdfI2cTransport.h"
int main() {
  using namespace SCD41;
  uint8_t bytes[3] = {};
  IdfI2cContext context;
  context.device = &context;
  TransferRequest request;
  request.timeoutMs = 50;
  request.writeData = bytes;
  request.writeLength = 2;
  for (auto intent : {TransferIntent::NORMAL, TransferIntent::EXPECTED_WRITE_NACK}) {
    request.intent = intent;
    for (esp_err_t error : {ESP_OK, ESP_ERR_INVALID_RESPONSE, ESP_ERR_TIMEOUT,
                           ESP_ERR_INVALID_STATE, ESP_FAIL}) {
      returnedError = error;
      attempts = 0;
      auto result = idfI2cTransfer(request, &context);
      assert(attempts == 1 && lastTimeout == 50 && result.completedMs == 42U);
      assert(result.detail == error);
      if (error == ESP_OK) {
        assert(result.code == TransferCode::OK && result.bytesTransferred == 2);
        assert(result.disposition == TransferDisposition::COMPLETE);
      } else {
        const auto expected = error == ESP_ERR_INVALID_RESPONSE ? TransferCode::NACK :
            error == ESP_ERR_TIMEOUT ? TransferCode::TIMEOUT : TransferCode::BUS_ERROR;
        assert(result.code == expected && result.bytesTransferred == 0);
        assert(result.disposition == TransferDisposition::INDETERMINATE);
      }
    }
  }
  attempts = 0;
  request.address = 0x63;
  auto result = idfI2cTransfer(request, &context);
  assert(result.disposition == TransferDisposition::NOT_STARTED && attempts == 0);
  request.address = 0x62;
  request.timeoutMs = 0;
  result = idfI2cTransfer(request, &context);
  assert(result.disposition == TransferDisposition::NOT_STARTED && attempts == 0);
  request.timeoutMs = 50;
  request.readData = bytes;
  request.readLength = sizeof(bytes);
  returnedError = ESP_OK;
  result = idfI2cTransfer(request, &context);
  assert(attempts == 1 && result.code == TransferCode::OK && result.bytesTransferred == 5);
  request.writeData = nullptr;
  request.writeLength = 0;
  returnedError = ESP_ERR_INVALID_RESPONSE;
  result = idfI2cTransfer(request, &context);
  assert(attempts == 2 && result.code == TransferCode::NACK);
  assert(result.disposition == TransferDisposition::NO_EFFECT);
}
''', headers, ("examples/idf/basic/main/IdfI2cTransport.cpp",))

    def test_idf_console_yields_on_partial_and_continuous_input(self) -> None:
        # Execute the real parser body with a finite simulated UART stream.
        # This catches starvation that static API/command-parity checks cannot.
        source = idf.IDF_MAIN.read_text(encoding="utf-8")
        declarations = source[source.index("struct Line {"):source.index("bool splitHeadTail(")]
        body = idf.function_body(source, "readLine")
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
#include <unistd.h>
constexpr size_t CLI_LINE_CAPACITY = 128U;
std::string incoming;
size_t position = 0;
size_t reads = 0;
int read(int, char* value, size_t) {
  ++reads;
  if (position == incoming.size()) return -1;
  *value = incoming[position++];
  return 1;
}
#define LOGW(...) ((void)0)
'''
        harness += declarations + "\nbool readLine(Line& output) {\n" + body + "\n}\n"
        harness += r'''
int main() {
  Line output;
  incoming = "he";
  assert(!readLine(output));
  incoming += "lp\n";
  assert(readLine(output) && output == "help");
  incoming = std::string(1024, 'x'); position = 0; reads = 0;
  assert(!readLine(output));
  assert(reads <= CLI_LINE_CAPACITY && position < incoming.size());
  incoming += "\nprobe\n";
  bool found = false;
  for (size_t pass = 0; pass < 16 && !found; ++pass) {
    reads = 0;
    found = readLine(output);
    assert(reads <= CLI_LINE_CAPACITY);
  }
  assert(found && output == "probe");
  incoming = "offs\bet\n"; position = 0;
  assert(readLine(output) && output == "offet");
}
'''
        compiler = consumer.compiler_command()
        self.assertIsNotNone(compiler, "native C++ compiler required for CLI regression")
        with tempfile.TemporaryDirectory(prefix="scd41-cli-regression-") as directory:
            root = pathlib.Path(directory)
            cpp = root / "console.cpp"
            exe = root / ("console.exe" if os.name == "nt" else "console")
            cpp.write_text(harness, encoding="utf-8")
            built = subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                                              str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

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
