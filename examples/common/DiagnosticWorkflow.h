/**
 * @file DiagnosticWorkflow.h
 * @brief Framework-neutral, fixed-memory CLI diagnostic sequencing.
 */

#pragma once

#include <cstdint>

#include "SCD41/SCD41.h"

namespace scd41_cli {

static constexpr uint32_t DEFAULT_STRESS_CYCLES = 50U;
static constexpr uint32_t MAX_STRESS_CYCLES = 1000U;

enum class WorkflowKind : uint8_t {
  NONE = 0,
  STRESS,
  STRESS_MIX,
  SELFCHECK,
};

inline const char* workflowName(WorkflowKind kind) {
  switch (kind) {
    case WorkflowKind::NONE: return "none";
    case WorkflowKind::STRESS: return "stress";
    case WorkflowKind::STRESS_MIX: return "stress_mix";
    case WorkflowKind::SELFCHECK: return "selfcheck";
  }
  return "unknown";
}

class DiagnosticWorkflow {
 public:
  bool begin(WorkflowKind kind, uint32_t cycles,
             SCD41::OperatingMode operatingMode) {
    if (active() || kind == WorkflowKind::NONE || cycles == 0U ||
        cycles > MAX_STRESS_CYCLES) {
      return false;
    }
    if (operatingMode != SCD41::OperatingMode::IDLE &&
        operatingMode != SCD41::OperatingMode::PERIODIC &&
        operatingMode != SCD41::OperatingMode::LOW_POWER_PERIODIC) {
      return false;
    }
    if (kind == WorkflowKind::SELFCHECK &&
        operatingMode != SCD41::OperatingMode::IDLE) {
      return false;
    }

    clear();
    _kind = kind;
    _operatingMode = operatingMode;
    _cyclesTotal = kind == WorkflowKind::SELFCHECK ? 1U : cycles;
    _totalSteps = _cyclesTotal * stepsPerCycle();
    return true;
  }

  bool nextRequest(SCD41::OperationRequest& request) const {
    if (!active() || _waiting || _stepsCompleted >= _totalSteps) {
      return false;
    }
    request = SCD41::OperationRequest::make(operationForStep(_stepsCompleted));
    return request.kind != SCD41::OperationKind::NONE;
  }

  bool markStarted(const SCD41::OperationId& id,
                   SCD41::OperationKind operation) {
    if (!active() || _waiting || operation != operationForStep(_stepsCompleted)) {
      return false;
    }
    _operationId = id;
    _operation = operation;
    _waiting = true;
    return true;
  }

  bool acceptResult(const SCD41::OperationResult& result) {
    if (!_waiting || result.id != _operationId || result.kind != _operation) {
      return false;
    }
    _waiting = false;
    if (result.outcome == SCD41::OperationOutcome::SUCCEEDED) {
      ++_passed;
    } else if (result.outcome == SCD41::OperationOutcome::NO_DATA &&
               result.kind == SCD41::OperationKind::FETCH_SAMPLE) {
      ++_warnings;
    } else {
      ++_failed;
    }
    ++_stepsCompleted;
    _cyclesCompleted = _stepsCompleted / stepsPerCycle();
    if (_stopRequested || _stepsCompleted >= _totalSteps) {
      _finished = true;
    }
    return true;
  }

  void markStartFailure() {
    if (!active() || _waiting) {
      return;
    }
    ++_failed;
    _finished = true;
  }

  void requestStop() {
    if (!active()) {
      return;
    }
    _stopRequested = true;
    if (!_waiting) {
      _finished = true;
    }
  }

  void clear() { *this = DiagnosticWorkflow{}; }

  bool active() const {
    return _kind != WorkflowKind::NONE && !_finished;
  }
  bool finished() const { return _finished; }
  bool waiting() const { return _waiting; }
  WorkflowKind kind() const { return _kind; }
  uint32_t totalSteps() const { return _totalSteps; }
  uint32_t completedSteps() const { return _stepsCompleted; }
  uint32_t totalCycles() const { return _cyclesTotal; }
  uint32_t completedCycles() const { return _cyclesCompleted; }
  uint32_t passed() const { return _passed; }
  uint32_t warnings() const { return _warnings; }
  uint32_t failed() const { return _failed; }

 private:
  uint32_t stepsPerCycle() const {
    if (_kind == WorkflowKind::SELFCHECK) {
      return 4U;
    }
    if (_kind == WorkflowKind::STRESS_MIX) {
      return _operatingMode == SCD41::OperatingMode::IDLE ? 4U : 3U;
    }
    return 1U;
  }

  SCD41::OperationKind operationForStep(uint32_t absoluteStep) const {
    if (_kind == WorkflowKind::STRESS) {
      return SCD41::OperationKind::READ_DATA_READY;
    }
    if (_kind == WorkflowKind::SELFCHECK) {
      static constexpr SCD41::OperationKind SEQUENCE[] = {
          SCD41::OperationKind::READ_IDENTITY,
          SCD41::OperationKind::READ_SENSOR_VARIANT,
          SCD41::OperationKind::READ_CONFIGURATION,
          SCD41::OperationKind::SELF_TEST,
      };
      return SEQUENCE[absoluteStep % 4U];
    }
    if (_kind == WorkflowKind::STRESS_MIX) {
      if (_operatingMode == SCD41::OperatingMode::IDLE) {
        static constexpr SCD41::OperationKind IDLE_SEQUENCE[] = {
            SCD41::OperationKind::READ_IDENTITY,
            SCD41::OperationKind::READ_CONFIGURATION,
            SCD41::OperationKind::READ_SENSOR_VARIANT,
            SCD41::OperationKind::READ_DATA_READY,
        };
        return IDLE_SEQUENCE[absoluteStep % 4U];
      }
      static constexpr SCD41::OperationKind PERIODIC_SEQUENCE[] = {
          SCD41::OperationKind::READ_DATA_READY,
          SCD41::OperationKind::FETCH_SAMPLE,
          SCD41::OperationKind::READ_AMBIENT_PRESSURE,
      };
      return PERIODIC_SEQUENCE[absoluteStep % 3U];
    }
    return SCD41::OperationKind::NONE;
  }

  WorkflowKind _kind = WorkflowKind::NONE;
  SCD41::OperatingMode _operatingMode = SCD41::OperatingMode::UNKNOWN;
  SCD41::OperationId _operationId = {};
  SCD41::OperationKind _operation = SCD41::OperationKind::NONE;
  uint32_t _cyclesTotal = 0U;
  uint32_t _cyclesCompleted = 0U;
  uint32_t _totalSteps = 0U;
  uint32_t _stepsCompleted = 0U;
  uint32_t _passed = 0U;
  uint32_t _warnings = 0U;
  uint32_t _failed = 0U;
  bool _waiting = false;
  bool _finished = false;
  bool _stopRequested = false;
};

}  // namespace scd41_cli
