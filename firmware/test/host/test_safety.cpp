#include "GardenSafety.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace garden;

namespace {
unsigned checks = 0;
#define CHECK(expression)                                                       \
  do {                                                                        \
    ++checks;                                                                 \
    if (!(expression)) {                                                      \
      std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "             \
                << #expression << '\n';                                       \
      std::exit(1);                                                           \
    }                                                                         \
  } while (false)

SensorSample sample(uint32_t atMs, float moisture = 20.0f) {
  SensorSample value;
  value.valid = true;
  value.calibrated = true;
  value.moisture = moisture;
  value.sampledAtMs = atMs;
  return value;
}

Request request(uint32_t sequence = 1, uint32_t durationMs = 2000,
                Mode mode = Mode::Manual) {
  Request value;
  value.sequence = sequence;
  value.durationMs = durationMs;
  value.mode = mode;
  value.minimum = 35;
  value.target = 65;
  return value;
}

void ready(SafetyController& controller, uint32_t nowMs = 0) {
  controller.setSample(sample(nowMs));
  CHECK(controller.arm(nowMs, true) == Result::Accepted);
  CHECK(controller.armed());
}

void completedPulse(SafetyController& controller, uint32_t seq, uint32_t nowMs,
                    uint32_t durationMs) {
  ready(controller, nowMs);
  CHECK(controller.start(request(seq, durationMs), nowMs, true) ==
        Result::Accepted);
  CHECK(controller.running());
  controller.setSample(sample(nowMs + durationMs));
  controller.tick(nowMs + durationMs, true);
  CHECK(!controller.running());
  CHECK(controller.currentEvent()->reason == StopReason::DurationElapsed);
  CHECK(controller.currentEvent()->actualDurationMs == durationMs);
}

void calibrationTests() {
  Calibration calibration;
  float moisture = -999;
  CHECK(!calibrationValid(calibration));
  CHECK(!calibratedMoisture(calibration, 1000, moisture));
  CHECK(moisture == -999);  // Missing calibration never manufactures a %.
  calibration.confirmed = true;
  calibration.dryRaw = 3000;
  calibration.wetRaw = 1000;
  CHECK(calibrationValid(calibration));
  CHECK(calibratedMoisture(calibration, 3000, moisture) && moisture == 0);
  CHECK(calibratedMoisture(calibration, 1000, moisture) && moisture == 100);
  CHECK(calibratedMoisture(calibration, 2000, moisture) && moisture == 50);
  CHECK(calibratedMoisture(calibration, 4095, moisture) && moisture == 0);
  CHECK(calibratedMoisture(calibration, 0, moisture) && moisture == 100);
  CHECK(!calibratedMoisture(calibration, -1, moisture));
  CHECK(!calibratedMoisture(calibration, 4096, moisture));
  calibration.dryRaw = 1000;
  calibration.wetRaw = 3000;
  CHECK(calibrationValid(calibration));
  CHECK(calibratedMoisture(calibration, 1000, moisture) && moisture == 0);
  CHECK(calibratedMoisture(calibration, 3000, moisture) && moisture == 100);
  CHECK(calibratedMoisture(calibration, 2000, moisture) && moisture == 50);
  calibration.wetRaw = 1199;
  CHECK(!calibrationValid(calibration));
  calibration.wetRaw = 1200;
  CHECK(calibrationValid(calibration));
  calibration.wetRaw = 4096;
  CHECK(!calibrationValid(calibration));
  calibration.wetRaw = 3000;
  calibration.confirmed = false;
  CHECK(!calibrationValid(calibration));
}

void initialAndValidationTests() {
  SafetyController disabled;
  disabled.setSample(sample(0));
  CHECK(!disabled.running());
  CHECK(!disabled.armed());
  CHECK(disabled.currentEvent() == nullptr);
  CHECK(disabled.arm(0, true) == Result::HardwareDisabled);
  CHECK(disabled.start(request(), 0, true) == Result::HardwareDisabled);

  SafetyController controller(true);
  CHECK(controller.arm(0, true) == Result::NotCalibrated);
  controller.setSample(sample(0));
  CHECK(controller.start(request(), 0, true) == Result::NotArmed);
  CHECK(controller.arm(0, false) == Result::NetworkLost);
  ready(controller);
  Request invalid = request(0);
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  invalid = request(1, 99);
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  invalid = request(1, 5001);
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  invalid = request();
  invalid.minimum = invalid.target;
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  invalid = request();
  invalid.minimum = -1;
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  invalid = request();
  invalid.target = 101;
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  invalid = request();
  invalid.target = std::numeric_limits<float>::quiet_NaN();
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  invalid = request();
  invalid.mode = static_cast<Mode>(99);
  CHECK(controller.start(invalid, 0, true) == Result::InvalidRequest);
  CHECK(controller.lastAcceptedSequence() == 0);
  CHECK(!controller.running());
}

void deadlineTargetAndStopTests() {
  SafetyController deadline(true);
  ready(deadline, 100);
  CHECK(deadline.start(request(), 100, true) == Result::Accepted);
  CHECK(deadline.currentEvent()->actualDurationMs == 0);
  deadline.tick(2099, true);
  CHECK(deadline.running());
  deadline.tick(2100, true);
  CHECK(!deadline.running());
  CHECK(deadline.armed());
  CHECK(deadline.currentEvent()->reason == StopReason::DurationElapsed);
  CHECK(deadline.currentEvent()->actualDurationMs == 2000);
  CHECK(deadline.budgetUsedMs(2100) == 2000);

  SafetyController target(true);
  ready(target);
  CHECK(target.start(request(), 0, true) == Result::Accepted);
  target.setSample(sample(450, 65));
  target.tick(450, true);
  CHECK(!target.running());
  CHECK(target.currentEvent()->reason == StopReason::TargetReached);
  CHECK(target.currentEvent()->actualDurationMs == 450);

  SafetyController stopped(true);
  ready(stopped);
  CHECK(stopped.start(request(), 0, true) == Result::Accepted);
  CHECK(stopped.arm(100, true) == Result::Busy);
  CHECK(stopped.start(request(2), 100, true) == Result::Busy);
  stopped.stop(250);
  CHECK(!stopped.running() && !stopped.armed());
  CHECK(stopped.currentEvent()->reason == StopReason::ManualStop);
  CHECK(stopped.currentEvent()->actualDurationMs == 250);
  stopped.stop(300);  // Idempotent stop preserves actual end time.
  CHECK(stopped.currentEvent()->actualDurationMs == 250);
  CHECK(stopped.budgetUsedMs(300) == 250);
  ready(stopped, 60250);
  CHECK(stopped.start(request(2), 60250, true) == Result::Accepted);
  stopped.disarm(60300);
  CHECK(!stopped.running() && !stopped.armed());
  CHECK(stopped.currentEvent()->reason == StopReason::Disarmed);
  CHECK(stopped.currentEvent()->actualDurationMs == 50);
}

void faultTests() {
  {
    SafetyController controller(true);
    ready(controller);
    CHECK(controller.start(request(), 0, true) == Result::Accepted);
    controller.tick(120, false);
    CHECK(!controller.running() && !controller.armed());
    CHECK(controller.currentEvent()->reason == StopReason::NetworkLost);
    controller.tick(121, true);
    CHECK(!controller.running() && !controller.armed());
  }
  {
    SafetyController controller(true);
    ready(controller);
    CHECK(controller.start(request(1, 5000), 0, true) == Result::Accepted);
    controller.tick(3000, true);
    CHECK(controller.running());
    controller.tick(3001, true);
    CHECK(!controller.running() && !controller.armed());
    CHECK(controller.currentEvent()->reason == StopReason::SensorStale);
    CHECK(controller.currentEvent()->actualDurationMs == 3001);
  }
  {
    SafetyController controller(true);
    ready(controller);
    CHECK(controller.start(request(), 0, true) == Result::Accepted);
    SensorSample invalid = sample(150);
    invalid.valid = false;
    controller.setSample(invalid);
    controller.tick(150, true);
    CHECK(!controller.running() && !controller.armed());
    CHECK(controller.currentEvent()->reason == StopReason::SensorInvalid);
  }
  {
    SafetyController controller(true);
    ready(controller);
    CHECK(controller.start(request(), 0, true) == Result::Accepted);
    SensorSample invalid = sample(150);
    invalid.calibrated = false;
    controller.setSample(invalid);
    controller.tick(150, true);
    CHECK(!controller.running() && !controller.armed());
    CHECK(controller.currentEvent()->reason == StopReason::NotCalibrated);
  }
  {
    SafetyController controller(true);
    ready(controller);
    controller.setSample(sample(299900));
    CHECK(controller.start(request(), 299900, true) == Result::Accepted);
    CHECK(controller.armRemainingMs(299900) == 100);
    controller.tick(300000, true);
    CHECK(!controller.running() && !controller.armed());
    CHECK(controller.currentEvent()->reason == StopReason::ArmExpired);
    CHECK(controller.currentEvent()->actualDurationMs == 100);
  }
  {
    SafetyController controller(true);
    controller.setSample(sample(0, std::numeric_limits<float>::quiet_NaN()));
    CHECK(controller.arm(0, true) == Result::SensorInvalid);
    controller.setSample(sample(0, 101));
    CHECK(controller.arm(0, true) == Result::SensorInvalid);
    controller.setSample(sample(0, -1));
    CHECK(controller.arm(0, true) == Result::SensorInvalid);
    ready(controller);
    controller.tick(10, false);  // An idle armed device also loses its lease.
    CHECK(!controller.armed());
  }
}

void automaticAndCooldownTests() {
  SafetyController disabled(true);
  ready(disabled);
  CHECK(disabled.start(request(1, 2000, Mode::Automatic), 0, true) ==
        Result::AutomaticDisabled);
  CHECK(!disabled.running());

  SafetyController automatic(true, true);
  ready(automatic);
  automatic.setSample(sample(0, 40));
  CHECK(automatic.start(request(1, 2000, Mode::Automatic), 0, true) ==
        Result::NotDry);
  automatic.setSample(sample(0, 65));
  CHECK(automatic.start(request(), 0, true) == Result::TargetReached);
  automatic.setSample(sample(0, 20));
  CHECK(automatic.start(request(1, 2000, Mode::Automatic), 0, true) ==
        Result::Accepted);
  automatic.tick(2000, true);
  automatic.setSample(sample(61999));
  CHECK(automatic.cooldownRemainingMs(61999) == 1);
  CHECK(automatic.start(request(2), 61999, true) == Result::Cooldown);
  automatic.setSample(sample(62000));
  CHECK(automatic.start(request(2), 62000, true) == Result::Accepted);
}

void replayAndHistoryTests() {
  SafetyController controller(true);
  ready(controller);
  const Request first = request();
  CHECK(controller.start(first, 0, true) == Result::Accepted);
  CHECK(controller.start(first, 500, true) == Result::Duplicate);
  CHECK(controller.currentEvent()->startedAtMs == 0);
  Request conflict = first;
  conflict.durationMs = 3000;
  CHECK(controller.start(conflict, 600, true) == Result::Conflict);
  controller.tick(2000, true);
  CHECK(!controller.running());
  controller.disarm(2100);
  CHECK(controller.start(first, 2200, false) == Result::Duplicate);
  CHECK(!controller.running());
  CHECK(controller.currentEvent()->actualDurationMs == 2000);

  SafetyController history(true);
  uint32_t now = 0;
  for (uint32_t sequence = 1; sequence <= 9; ++sequence) {
    completedPulse(history, sequence, now, 100);
    now += 60100;
  }
  CHECK(history.findEvent(1) == nullptr);
  CHECK(history.findEvent(2) != nullptr);
  CHECK(history.findEvent(9) != nullptr);
  CHECK(history.start(request(1, 100), now, true) == Result::StaleSequence);
  CHECK(history.start(request(9, 100), now, true) == Result::Duplicate);
  CHECK(!history.running());
  CHECK(history.lastAcceptedSequence() == 9);

  SafetyController exhausted(true);
  ready(exhausted);
  CHECK(exhausted.start(request(UINT32_MAX, 100), 0, true) == Result::Accepted);
  exhausted.tick(100, true);
  CHECK(exhausted.start(request(1), 200, true) == Result::StaleSequence);
}

void budgetTests() {
  SafetyController controller(true);
  uint32_t now = 0;
  for (uint32_t sequence = 1; sequence <= 6; ++sequence) {
    completedPulse(controller, sequence, now, 5000);
    now += 65000;
  }
  ready(controller, now);
  CHECK(controller.budgetUsedMs(now) == 30000);
  CHECK(controller.start(request(7, 100), now, true) == Result::BudgetExceeded);
  CHECK(!controller.running());
  // Only the oldest pulse expires at this boundary. Conservative charging of
  // entire pulses still protects the next reservation.
  ready(controller, 3605000);
  CHECK(controller.budgetUsedMs(3605000) == 25000);
  CHECK(controller.start(request(7, 5000), 3605000, true) == Result::Accepted);
  controller.setSample(sample(3610000));
  controller.tick(3610000, true);
  CHECK(controller.budgetUsedMs(3610000) == 30000);
}

void wraparoundTests() {
  SafetyController controller(true);
  const uint32_t start = UINT32_MAX - 1000;
  ready(controller, start);
  CHECK(controller.start(request(), start, true) == Result::Accepted);
  const uint32_t end = start + 2000;
  controller.tick(end - 1, true);
  CHECK(controller.running());
  controller.tick(end, true);
  CHECK(!controller.running());
  CHECK(controller.currentEvent()->actualDurationMs == 2000);
  CHECK(controller.cooldownRemainingMs(end) == 60000);
  CHECK(controller.budgetUsedMs(end) == 2000);
  controller.setSample(sample(end + 60000));
  CHECK(controller.start(request(2), end + 59999, true) == Result::SensorStale);
  // A sample from the future is intentionally invalid via unsigned age.
  ready(controller, end + 60000);
  CHECK(controller.start(request(2), end + 60000, true) == Result::Accepted);

  SafetyController lease(true);
  const uint32_t armAt = UINT32_MAX - 100000;
  ready(lease, armAt);
  lease.setSample(sample(armAt + kArmLeaseMs));
  lease.tick(armAt + kArmLeaseMs, true);
  CHECK(!lease.armed());
  CHECK(lease.armRemainingMs(armAt + kArmLeaseMs) == 0);
}
}  // namespace

int main() {
  calibrationTests();
  initialAndValidationTests();
  deadlineTargetAndStopTests();
  faultTests();
  automaticAndCooldownTests();
  replayAndHistoryTests();
  budgetTests();
  wraparoundTests();
  std::cout << "GardenSafety: " << checks << " checks passed.\n";
  return 0;
}
