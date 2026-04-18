/**
 * @file HealthView.h
 * @brief Compact health snapshots and diffs for example CLIs.
 */

#pragma once

#include <Arduino.h>

#include "common/DriverCompat.h"
#include "common/Log.h"

namespace health_view {

inline const char* failureColor(uint32_t failures) {
  if (failures == 0U) {
    return LOG_COLOR_GREEN;
  }
  if (failures < 3U) {
    return LOG_COLOR_YELLOW;
  }
  return LOG_COLOR_RED;
}

inline const char* successColor(uint32_t successes) {
  return (successes > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_GRAY;
}

inline const char* successRateColor(float pct) {
  if (pct >= 99.9f) {
    return LOG_COLOR_GREEN;
  }
  if (pct >= 80.0f) {
    return LOG_COLOR_YELLOW;
  }
  return LOG_COLOR_RED;
}

inline const char* boolColor(bool value) {
  return value ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

template <typename DriverT>
struct Snapshot {
  app_driver::DriverState state = app_driver::DriverState::UNINIT;
  bool online = false;
  uint8_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t totalSuccess = 0;
  uint32_t lastOkMs = 0;
  uint32_t lastErrorMs = 0;
  app_driver::Status lastError = app_driver::Status::Ok();

  void capture(const DriverT& driver) {
    state = driver.state();
    online = driver.isOnline();
    consecutiveFailures = driver.consecutiveFailures();
    totalFailures = driver.totalFailures();
    totalSuccess = driver.totalSuccess();
    lastOkMs = driver.lastOkMs();
    lastErrorMs = driver.lastErrorMs();
    lastError = driver.lastError();
  }
};

template <typename DriverT>
inline void printHealthView(const DriverT& driver) {
  Snapshot<DriverT> snap;
  snap.capture(driver);
  const uint32_t total = snap.totalSuccess + snap.totalFailures;
  const float pct = (total > 0U)
                        ? (100.0f * static_cast<float>(snap.totalSuccess) /
                           static_cast<float>(total))
                        : 0.0f;

  Serial.printf(
      "Health: state=%s%s%s online=%s%s%s consec=%s%u%s ok=%s%lu%s fail=%s%lu%s rate=%s%.1f%%%s lastOk=%lu lastErr=%lu err=%s%s%s\n",
      failureColor(static_cast<uint32_t>(snap.consecutiveFailures)),
      app_driver::stateToString(snap.state),
      LOG_COLOR_RESET,
      boolColor(snap.online),
      snap.online ? "true" : "false",
      LOG_COLOR_RESET,
      failureColor(static_cast<uint32_t>(snap.consecutiveFailures)),
      static_cast<unsigned>(snap.consecutiveFailures),
      LOG_COLOR_RESET,
      successColor(snap.totalSuccess),
      static_cast<unsigned long>(snap.totalSuccess),
      LOG_COLOR_RESET,
      failureColor(snap.totalFailures),
      static_cast<unsigned long>(snap.totalFailures),
      LOG_COLOR_RESET,
      successRateColor(pct),
      pct,
      LOG_COLOR_RESET,
      static_cast<unsigned long>(snap.lastOkMs),
      static_cast<unsigned long>(snap.lastErrorMs),
      snap.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
      snap.lastError.ok() ? "OK" : app_driver::errToString(snap.lastError.code),
      LOG_COLOR_RESET);
}

template <typename DriverT>
inline void printHealthDiff(const Snapshot<DriverT>& before, const Snapshot<DriverT>& after) {
  bool changed = false;

  if (before.state != after.state) {
    Serial.printf("  State: %s%s%s -> %s%s%s\n",
                  failureColor(static_cast<uint32_t>(before.consecutiveFailures)),
                  app_driver::stateToString(before.state),
                  LOG_COLOR_RESET,
                  failureColor(static_cast<uint32_t>(after.consecutiveFailures)),
                  app_driver::stateToString(after.state),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.online != after.online) {
    Serial.printf("  Online: %s%s%s -> %s%s%s\n",
                  boolColor(before.online),
                  before.online ? "true" : "false",
                  LOG_COLOR_RESET,
                  boolColor(after.online),
                  after.online ? "true" : "false",
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.consecutiveFailures != after.consecutiveFailures) {
    Serial.printf("  ConsecFail: %s%u -> %u%s\n",
                  failureColor(static_cast<uint32_t>(after.consecutiveFailures)),
                  static_cast<unsigned>(before.consecutiveFailures),
                  static_cast<unsigned>(after.consecutiveFailures),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.totalSuccess != after.totalSuccess) {
    Serial.printf("  TotalOK: %lu -> %s%lu (+%lu)%s\n",
                  static_cast<unsigned long>(before.totalSuccess),
                  LOG_COLOR_GREEN,
                  static_cast<unsigned long>(after.totalSuccess),
                  static_cast<unsigned long>(after.totalSuccess - before.totalSuccess),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.totalFailures != after.totalFailures) {
    Serial.printf("  TotalFail: %lu -> %s%lu (+%lu)%s\n",
                  static_cast<unsigned long>(before.totalFailures),
                  LOG_COLOR_RED,
                  static_cast<unsigned long>(after.totalFailures),
                  static_cast<unsigned long>(after.totalFailures - before.totalFailures),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.lastError.code != after.lastError.code || before.lastError.detail != after.lastError.detail) {
    Serial.printf("  LastErr: %s%s%s -> %s%s%s\n",
                  before.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                  before.lastError.ok() ? "OK" : app_driver::errToString(before.lastError.code),
                  LOG_COLOR_RESET,
                  after.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                  after.lastError.ok() ? "OK" : app_driver::errToString(after.lastError.code),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.lastOkMs != after.lastOkMs) {
    Serial.printf("  LastOKms: %lu -> %lu\n",
                  static_cast<unsigned long>(before.lastOkMs),
                  static_cast<unsigned long>(after.lastOkMs));
    changed = true;
  }
  if (before.lastErrorMs != after.lastErrorMs) {
    Serial.printf("  LastErrMs: %lu -> %lu\n",
                  static_cast<unsigned long>(before.lastErrorMs),
                  static_cast<unsigned long>(after.lastErrorMs));
    changed = true;
  }
  if (!changed) {
    Serial.println("  (no health changes)");
  }
}

} // namespace health_view

template <typename DriverT>
using HealthSnapshot = health_view::Snapshot<DriverT>;

using health_view::printHealthDiff;
using health_view::printHealthView;
