/**
 * @file CliShell.h
 * @brief Small serial line reader for example CLIs.
 */

#pragma once

#include <Arduino.h>

#include "common/Log.h"

namespace cli_shell {

inline bool readLine(String& outLine) {
  static String buffer;

  while (LOG_SERIAL.available() > 0) {
    const char c = static_cast<char>(LOG_SERIAL.read());
    if (c == '\r' || c == '\n') {
      if (buffer.length() == 0U) {
        continue;
      }
      outLine = buffer;
      buffer = "";
      outLine.trim();
      return true;
    }
    buffer += c;
  }

  return false;
}

}  // namespace cli_shell
