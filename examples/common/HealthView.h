/**
 * @file HealthView.h
 * @brief Minimal single-line health printer for examples.
 */

#pragma once

#include <Arduino.h>

#include "common/DriverCompat.h"

template <typename DriverT>
inline void printHealthView(const DriverT& driver) {
  Serial.printf("state=%s online=%s failures=%u totalFail=%lu totalOk=%lu\n",
                app_driver::stateToString(driver.state()),
                driver.isOnline() ? "true" : "false",
                static_cast<unsigned>(driver.consecutiveFailures()),
                static_cast<unsigned long>(driver.totalFailures()),
                static_cast<unsigned long>(driver.totalSuccess()));
}
