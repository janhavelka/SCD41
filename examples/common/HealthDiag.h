/**
 * @file HealthDiag.h
 * @brief Driver-health helpers for example CLIs.
 */

#pragma once

#include <Arduino.h>

#include "common/DriverCompat.h"
#include "common/Log.h"

namespace diag {

inline const char* stateColor(app_driver::DriverState state) {
  switch (state) {
    case app_driver::DriverState::READY: return LOG_COLOR_GREEN;
    case app_driver::DriverState::DEGRADED: return LOG_COLOR_YELLOW;
    case app_driver::DriverState::OFFLINE: return LOG_COLOR_RED;
    case app_driver::DriverState::UNINIT: return LOG_COLOR_GRAY;
    default: return LOG_COLOR_RESET;
  }
}

inline const char* boolColor(bool value) {
  return value ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

inline const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

inline const char* totalFailureColor(uint32_t failures) {
  return (failures == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

inline void printHealthOneLine(app_driver::Device& driver) {
  LOGI("Health: state=%s%s%s online=%s%s%s ok=%lu fail=%lu consec=%u",
       stateColor(driver.state()),
       app_driver::stateToString(driver.state()),
       LOG_COLOR_RESET,
       boolColor(driver.isOnline()),
       driver.isOnline() ? "true" : "false",
       LOG_COLOR_RESET,
       static_cast<unsigned long>(driver.totalSuccess()),
       static_cast<unsigned long>(driver.totalFailures()),
       driver.consecutiveFailures());
}

inline void printHealthVerbose(app_driver::Device& driver) {
  const uint32_t totalSuccess = driver.totalSuccess();
  const uint32_t totalFailures = driver.totalFailures();
  const uint32_t total = totalSuccess + totalFailures;
  const float successRate =
      (total > 0U) ? (100.0f * static_cast<float>(totalSuccess) / static_cast<float>(total)) : 0.0f;

  LOG_SERIAL.println();
  LOGI("=== Core Driver Health ===");
  LOGI("  State: %s%s%s",
       stateColor(driver.state()),
       app_driver::stateToString(driver.state()),
       LOG_COLOR_RESET);
  LOGI("  Online: %s%s%s",
       boolColor(driver.isOnline()),
       driver.isOnline() ? "true" : "false",
       LOG_COLOR_RESET);
  LOGI("  Consecutive failures: %u", driver.consecutiveFailures());
  LOGI("  Total success: %lu", static_cast<unsigned long>(totalSuccess));
  LOGI("  Total failures: %s%lu%s",
       totalFailureColor(totalFailures),
       static_cast<unsigned long>(totalFailures),
       LOG_COLOR_RESET);
  LOGI("  Success rate: %s%.1f%%%s", successRateColor(successRate), successRate, LOG_COLOR_RESET);
  LOGI("  Last OK ms: %lu", static_cast<unsigned long>(driver.lastOkMs()));
  LOGI("  Last error ms: %lu", static_cast<unsigned long>(driver.lastErrorMs()));

  const app_driver::Status lastError = driver.lastError();
  if (lastError.ok()) {
    LOGI("  Last error: %snone%s", LOG_COLOR_GREEN, LOG_COLOR_RESET);
  } else {
    LOGI("  Last error: %s%s%s detail=%ld msg=%s",
         LOG_COLOR_RED,
         app_driver::errToString(lastError.code),
         LOG_COLOR_RESET,
         static_cast<long>(lastError.detail),
         lastError.msg ? lastError.msg : "");
  }

  LOG_SERIAL.println();
}

}  // namespace diag
