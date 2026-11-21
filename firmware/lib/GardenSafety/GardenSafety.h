#ifndef MYGARDEN_GARDEN_SAFETY_H
#define MYGARDEN_GARDEN_SAFETY_H

#include <stddef.h>
#include <stdint.h>

// Portable policy only: no GPIO, networking, wall clock or dynamic allocation.
// The caller must serialize access and apply running() to the physical output
// after every operation. tick() must run independently of HTTP processing.
namespace garden {

constexpr uint32_t kMinimumPulseMs = 100;
constexpr uint32_t kMaximumPulseMs = 5000;
constexpr uint32_t kCooldownMs = 60000;
constexpr uint32_t kMaximumSampleAgeMs = 3000;
constexpr uint32_t kArmLeaseMs = 300000;
constexpr uint32_t kBudgetWindowMs = 3600000;
constexpr uint32_t kBudgetMaximumMs = 30000;
constexpr size_t kEventCapacity = 8;
constexpr size_t kBudgetCapacity = 64;

enum class Mode { Manual, Automatic };
enum class Result {
  Accepted,
  Duplicate,
  Conflict,
  StaleSequence,
  InvalidRequest,
  HardwareDisabled,
  AutomaticDisabled,
  NotArmed,
  NetworkLost,
  SensorInvalid,
  SensorStale,
  NotCalibrated,
  Busy,
  Cooldown,
  BudgetExceeded,
  TargetReached,
  NotDry
};
enum class StopReason {
  None,
  DurationElapsed,
  TargetReached,
  ManualStop,
  Disarmed,
  NetworkLost,
  SensorInvalid,
  SensorStale,
  NotCalibrated,
  ArmExpired
};

struct SensorSample {
  bool valid = false;
  bool calibrated = false;
  float moisture = 0;
  uint32_t sampledAtMs = 0;
};

struct Request {
  uint32_t sequence = 0;
  uint32_t durationMs = 2000;
  Mode mode = Mode::Manual;
  float minimum = 35;
  float target = 65;
};

struct Event {
  Request request;
  uint32_t startedAtMs = 0;
  // Actual elapsed run time, not the requested duration. Updated by tick().
  uint32_t actualDurationMs = 0;
  bool running = false;
  StopReason reason = StopReason::None;
};

struct Calibration {
  bool confirmed = false;
  int dryRaw = 0;
  int wetRaw = 0;
};

bool calibrationValid(const Calibration& calibration);
bool calibratedMoisture(const Calibration& calibration, int raw,
                        float& moisture);
const char* resultName(Result result);
const char* stopReasonName(StopReason reason);
const char* modeName(Mode mode);

class SafetyController {
 public:
  explicit SafetyController(bool hardwareEnabled = false,
                            bool automaticAllowed = false);
  void setSample(const SensorSample& sample);
  Result arm(uint32_t nowMs, bool networkConnected);
  Result start(const Request& request, uint32_t nowMs, bool networkConnected);
  void tick(uint32_t nowMs, bool networkConnected);
  // Both commands end the run (if any) and revoke the arm lease.
  void stop(uint32_t nowMs);
  void disarm(uint32_t nowMs);

  bool running() const;
  bool armed() const;
  uint32_t lastAcceptedSequence() const;
  const Event* findEvent(uint32_t sequence) const;
  // Most recently accepted event, including its terminal state; null at boot.
  const Event* currentEvent() const;
  // Completed pulse durations whose END is in the last hour, plus the active
  // run's elapsed time. This conservatively charges entire boundary pulses.
  uint32_t budgetUsedMs(uint32_t nowMs) const;
  uint32_t cooldownRemainingMs(uint32_t nowMs) const;
  uint32_t armRemainingMs(uint32_t nowMs) const;

 private:
  struct BudgetEntry {
    uint32_t finishedAtMs = 0;
    uint32_t durationMs = 0;
    bool used = false;
  };
  Result readiness(uint32_t nowMs, bool networkConnected) const;
  void finish(uint32_t nowMs, StopReason reason);
  void revoke(uint32_t nowMs, StopReason reason);
  void pruneBudget(uint32_t nowMs);

  const bool hardwareEnabled_;
  const bool automaticAllowed_;
  SensorSample sample_;
  bool armed_ = false;
  uint32_t armedAtMs_ = 0;
  uint32_t lastAcceptedSequence_ = 0;
  Event events_[kEventCapacity];
  size_t nextEvent_ = 0;
  size_t currentEvent_ = 0;
  bool hasCurrent_ = false;
  bool hasFinished_ = false;
  uint32_t lastFinishedAtMs_ = 0;
  BudgetEntry budget_[kBudgetCapacity];
  size_t nextBudget_ = 0;
};

}  // namespace garden
#endif
