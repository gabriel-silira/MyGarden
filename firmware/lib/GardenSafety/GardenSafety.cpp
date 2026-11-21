#include "GardenSafety.h"

#include <cmath>

namespace garden {
namespace {
uint32_t elapsed(uint32_t nowMs, uint32_t sinceMs) {
  // Unsigned subtraction is intentional: millis() wraps after ~49.7 days.
  return nowMs - sinceMs;
}

bool sameRequest(const Request& a, const Request& b) {
  return a.sequence == b.sequence && a.durationMs == b.durationMs &&
         a.mode == b.mode && a.minimum == b.minimum && a.target == b.target;
}

bool validRequest(const Request& request) {
  return request.sequence > 0 && request.durationMs >= kMinimumPulseMs &&
         request.durationMs <= kMaximumPulseMs &&
         (request.mode == Mode::Manual || request.mode == Mode::Automatic) &&
         std::isfinite(request.minimum) && std::isfinite(request.target) &&
         request.minimum >= 0 && request.minimum < request.target &&
         request.target <= 100;
}
}  // namespace

bool calibrationValid(const Calibration& calibration) {
  if (!calibration.confirmed || calibration.dryRaw < 0 ||
      calibration.dryRaw > 4095 || calibration.wetRaw < 0 ||
      calibration.wetRaw > 4095) {
    return false;
  }
  const int span = calibration.dryRaw - calibration.wetRaw;
  return span >= 200 || span <= -200;
}

bool calibratedMoisture(const Calibration& calibration, int raw,
                        float& moisture) {
  if (!calibrationValid(calibration) || raw < 0 || raw > 4095) return false;
  float result = 100.0f * (raw - calibration.dryRaw) /
                 (calibration.wetRaw - calibration.dryRaw);
  if (result < 0) result = 0;
  if (result > 100) result = 100;
  moisture = result;
  return true;
}

SafetyController::SafetyController(bool hardwareEnabled, bool automaticAllowed)
    : hardwareEnabled_(hardwareEnabled), automaticAllowed_(automaticAllowed) {}

void SafetyController::setSample(const SensorSample& sample) { sample_ = sample; }

Result SafetyController::readiness(uint32_t nowMs, bool networkConnected) const {
  if (!hardwareEnabled_) return Result::HardwareDisabled;
  if (!networkConnected) return Result::NetworkLost;
  if (!sample_.calibrated) return Result::NotCalibrated;
  if (!sample_.valid || !std::isfinite(sample_.moisture) ||
      sample_.moisture < 0 || sample_.moisture > 100) {
    return Result::SensorInvalid;
  }
  if (elapsed(nowMs, sample_.sampledAtMs) > kMaximumSampleAgeMs) {
    return Result::SensorStale;
  }
  return Result::Accepted;
}

Result SafetyController::arm(uint32_t nowMs, bool networkConnected) {
  tick(nowMs, networkConnected);
  if (running()) return Result::Busy;
  const Result result = readiness(nowMs, networkConnected);
  if (result != Result::Accepted) return result;
  armed_ = true;
  armedAtMs_ = nowMs;
  return Result::Accepted;
}

Result SafetyController::start(const Request& request, uint32_t nowMs,
                               bool networkConnected) {
  tick(nowMs, networkConnected);
  // Reconciliation precedes readiness checks. A retry cannot turn a completed
  // command into a new activation, even after a disconnect or expired lease.
  if (request.sequence != 0) {
    const Event* prior = findEvent(request.sequence);
    if (prior) return sameRequest(prior->request, request)
                          ? Result::Duplicate : Result::Conflict;
    if (request.sequence <= lastAcceptedSequence_) return Result::StaleSequence;
  }
  if (!validRequest(request)) return Result::InvalidRequest;
  if (running()) return Result::Busy;
  const Result ready = readiness(nowMs, networkConnected);
  if (ready != Result::Accepted) return ready;
  if (!armed_) return Result::NotArmed;
  if (request.mode == Mode::Automatic && !automaticAllowed_) {
    return Result::AutomaticDisabled;
  }
  if (sample_.moisture >= request.target) return Result::TargetReached;
  if (request.mode == Mode::Automatic && sample_.moisture >= request.minimum) {
    return Result::NotDry;
  }
  if (cooldownRemainingMs(nowMs) != 0) return Result::Cooldown;
  // Reserve the full requested pulse before energizing. Only one run exists.
  const uint32_t used = budgetUsedMs(nowMs);
  if (used > kBudgetMaximumMs || request.durationMs > kBudgetMaximumMs - used) {
    return Result::BudgetExceeded;
  }

  currentEvent_ = nextEvent_;
  nextEvent_ = (nextEvent_ + 1) % kEventCapacity;
  Event& event = events_[currentEvent_];
  event = Event();
  event.request = request;
  event.startedAtMs = nowMs;
  event.running = true;
  hasCurrent_ = true;
  lastAcceptedSequence_ = request.sequence;
  return Result::Accepted;
}

void SafetyController::finish(uint32_t nowMs, StopReason reason) {
  if (!running()) return;
  Event& event = events_[currentEvent_];
  event.actualDurationMs = elapsed(nowMs, event.startedAtMs);
  event.reason = reason;
  event.running = false;
  lastFinishedAtMs_ = nowMs;
  hasFinished_ = true;
  BudgetEntry& entry = budget_[nextBudget_];
  nextBudget_ = (nextBudget_ + 1) % kBudgetCapacity;
  entry.finishedAtMs = nowMs;
  entry.durationMs = event.actualDurationMs;
  entry.used = true;
}

void SafetyController::revoke(uint32_t nowMs, StopReason reason) {
  finish(nowMs, reason);
  armed_ = false;
}

void SafetyController::pruneBudget(uint32_t nowMs) {
  if (hasFinished_ && elapsed(nowMs, lastFinishedAtMs_) >= kCooldownMs) {
    hasFinished_ = false;
  }
  for (size_t i = 0; i < kBudgetCapacity; ++i) {
    if (budget_[i].used &&
        elapsed(nowMs, budget_[i].finishedAtMs) >= kBudgetWindowMs) {
      budget_[i].used = false;
    }
  }
}

void SafetyController::tick(uint32_t nowMs, bool networkConnected) {
  pruneBudget(nowMs);
  if (!armed_ && !running()) return;
  const Result ready = readiness(nowMs, networkConnected);
  if (ready != Result::Accepted) {
    StopReason reason = StopReason::SensorInvalid;
    switch (ready) {
      case Result::NetworkLost: reason = StopReason::NetworkLost; break;
      case Result::NotCalibrated: reason = StopReason::NotCalibrated; break;
      case Result::SensorStale: reason = StopReason::SensorStale; break;
      default: break;
    }
    revoke(nowMs, reason);
    return;
  }
  if (elapsed(nowMs, armedAtMs_) >= kArmLeaseMs) {
    revoke(nowMs, StopReason::ArmExpired);
    return;
  }
  if (!running()) return;
  Event& event = events_[currentEvent_];
  event.actualDurationMs = elapsed(nowMs, event.startedAtMs);
  if (event.actualDurationMs >= event.request.durationMs) {
    finish(nowMs, StopReason::DurationElapsed);
  } else if (sample_.moisture >= event.request.target) {
    finish(nowMs, StopReason::TargetReached);
  }
}

void SafetyController::stop(uint32_t nowMs) {
  revoke(nowMs, StopReason::ManualStop);
}

void SafetyController::disarm(uint32_t nowMs) {
  revoke(nowMs, StopReason::Disarmed);
}

bool SafetyController::running() const {
  return hasCurrent_ && events_[currentEvent_].running;
}
bool SafetyController::armed() const { return armed_; }
uint32_t SafetyController::lastAcceptedSequence() const {
  return lastAcceptedSequence_;
}
const Event* SafetyController::findEvent(uint32_t sequence) const {
  if (sequence == 0) return nullptr;
  for (size_t i = 0; i < kEventCapacity; ++i) {
    if (events_[i].request.sequence == sequence) return &events_[i];
  }
  return nullptr;
}
const Event* SafetyController::currentEvent() const {
  return hasCurrent_ ? &events_[currentEvent_] : nullptr;
}
uint32_t SafetyController::budgetUsedMs(uint32_t nowMs) const {
  uint64_t used = 0;
  for (size_t i = 0; i < kBudgetCapacity; ++i) {
    if (budget_[i].used &&
        elapsed(nowMs, budget_[i].finishedAtMs) < kBudgetWindowMs) {
      used += budget_[i].durationMs;
    }
  }
  if (running()) used += elapsed(nowMs, events_[currentEvent_].startedAtMs);
  // Saturation avoids numeric wrap if a stalled caller reports a huge run.
  return used > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(used);
}
uint32_t SafetyController::cooldownRemainingMs(uint32_t nowMs) const {
  if (!hasFinished_) return 0;
  const uint32_t age = elapsed(nowMs, lastFinishedAtMs_);
  return age >= kCooldownMs ? 0 : kCooldownMs - age;
}
uint32_t SafetyController::armRemainingMs(uint32_t nowMs) const {
  if (!armed_) return 0;
  const uint32_t age = elapsed(nowMs, armedAtMs_);
  return age >= kArmLeaseMs ? 0 : kArmLeaseMs - age;
}

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Manual: return "manual";
    case Mode::Automatic: return "automatic";
  }
  return "unknown";
}
const char* resultName(Result result) {
  switch (result) {
    case Result::Accepted: return "accepted";
    case Result::Duplicate: return "duplicate";
    case Result::Conflict: return "conflict";
    case Result::StaleSequence: return "stale_sequence";
    case Result::InvalidRequest: return "invalid_request";
    case Result::HardwareDisabled: return "hardware_disabled";
    case Result::AutomaticDisabled: return "automatic_disabled";
    case Result::NotArmed: return "not_armed";
    case Result::NetworkLost: return "network_lost";
    case Result::SensorInvalid: return "sensor_invalid";
    case Result::SensorStale: return "sensor_stale";
    case Result::NotCalibrated: return "not_calibrated";
    case Result::Busy: return "busy";
    case Result::Cooldown: return "cooldown";
    case Result::BudgetExceeded: return "budget_exceeded";
    case Result::TargetReached: return "target_reached";
    case Result::NotDry: return "not_dry";
  }
  return "unknown";
}
const char* stopReasonName(StopReason reason) {
  switch (reason) {
    case StopReason::None: return "none";
    case StopReason::DurationElapsed: return "duration_elapsed";
    case StopReason::TargetReached: return "target_reached";
    case StopReason::ManualStop: return "manual_stop";
    case StopReason::Disarmed: return "disarmed";
    case StopReason::NetworkLost: return "network_lost";
    case StopReason::SensorInvalid: return "sensor_invalid";
    case StopReason::SensorStale: return "sensor_stale";
    case StopReason::NotCalibrated: return "not_calibrated";
    case StopReason::ArmExpired: return "arm_expired";
  }
  return "unknown";
}

}  // namespace garden
