/**
 * @file Log.h
 * @brief Simple serial logging macros for examples.
 */

#pragma once

#include <Arduino.h>

#include "common/BuildConfig.h"

#if LOG_LEVEL < 0 || LOG_LEVEL > 2
#error "LOG_LEVEL must be 0-2 (0=off, 1=error, 2=info)"
#endif

#ifndef LOG_SERIAL
#define LOG_SERIAL Serial
#endif

#define LOG_COLOR_RESET  "\033[0m"
#define LOG_COLOR_RED    "\033[31m"
#define LOG_COLOR_GREEN  "\033[32m"
#define LOG_COLOR_YELLOW "\033[33m"
#define LOG_COLOR_CYAN   "\033[36m"

inline void log_begin(unsigned long baud = 115200) {
  LOG_SERIAL.begin(baud);
}

#define LOG_PRINT_WITH_TAG(tagColor, tag, fmt, ...) \
  LOG_SERIAL.printf(tagColor "[" tag "]" LOG_COLOR_RESET " " fmt "\n", ##__VA_ARGS__)

#define LOGE(fmt, ...) \
  do { \
    if (LOG_LEVEL >= 1) LOG_PRINT_WITH_TAG(LOG_COLOR_RED, "E", fmt, ##__VA_ARGS__); \
  } while (0)

#define LOGW(fmt, ...) \
  do { \
    if (LOG_LEVEL >= 2) LOG_PRINT_WITH_TAG(LOG_COLOR_YELLOW, "W", fmt, ##__VA_ARGS__); \
  } while (0)

#define LOGI(fmt, ...) \
  do { \
    if (LOG_LEVEL >= 2) LOG_PRINT_WITH_TAG(LOG_COLOR_CYAN, "I", fmt, ##__VA_ARGS__); \
  } while (0)
