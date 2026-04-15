/**
 * @file CommandHandler.h
 * @brief Parsing helpers for interactive examples.
 */

#pragma once

#include <Arduino.h>
#include <cstdlib>

namespace cmd {

inline bool splitHeadTail(const String& input, String& head, String& tail) {
  const int split = input.indexOf(' ');
  if (split < 0) {
    head = input;
    head.trim();
    tail = "";
    return head.length() > 0U;
  }

  head = input.substring(0, split);
  tail = input.substring(split + 1);
  head.trim();
  tail.trim();
  return head.length() > 0U;
}

inline bool parseInt32(const String& token, int32_t& outValue) {
  if (token.length() == 0U) {
    return false;
  }

  char* end = nullptr;
  const long value = std::strtol(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0') {
    return false;
  }

  outValue = static_cast<int32_t>(value);
  return true;
}

inline bool parseU16(const String& token, uint16_t& outValue) {
  if (token.length() == 0U) {
    return false;
  }

  char* end = nullptr;
  const unsigned long value = std::strtoul(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0' || value > 0xFFFFUL) {
    return false;
  }

  outValue = static_cast<uint16_t>(value);
  return true;
}

inline bool parseU32(const String& token, uint32_t& outValue) {
  if (token.length() == 0U) {
    return false;
  }

  char* end = nullptr;
  const unsigned long value = std::strtoul(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0') {
    return false;
  }

  outValue = static_cast<uint32_t>(value);
  return true;
}

inline bool parseFloat(const String& token, float& outValue) {
  if (token.length() == 0U) {
    return false;
  }

  char* end = nullptr;
  outValue = static_cast<float>(std::strtod(token.c_str(), &end));
  return !(end == token.c_str() || *end != '\0');
}

inline bool parseBool01(const String& token, bool& outValue) {
  if (token == "1" || token == "on" || token == "true" || token == "enable") {
    outValue = true;
    return true;
  }
  if (token == "0" || token == "off" || token == "false" || token == "disable") {
    outValue = false;
    return true;
  }
  return false;
}

}  // namespace cmd
