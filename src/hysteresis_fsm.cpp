// hysteresis_fsm.cpp
#include "hysteresis_fsm.hpp"

#include <algorithm> // std::max
#include <sstream>

namespace telemetry {

static const char* to_string(IfStatus s) {
  switch (s) {
    case IfStatus::Healthy:  return "healthy";
    case IfStatus::Degraded: return "degraded";
    case IfStatus::Down:     return "down";
  }
  return "unknown status";
}

HysteresisFsm::HysteresisFsm(FsmConfig cfg, IfStatus initial)
  : cfg_(cfg), status_(initial) {}

// Dwell time check: if no transition has happened yet, allow.
bool HysteresisFsm::dwell_ok_(int64_t ts_now) const {
  if (last_transition_ts_ == std::numeric_limits<int64_t>::min()) return true;
  return (ts_now - last_transition_ts_) >= cfg_.min_dwell_sec;
}

FsmUpdate HysteresisFsm::transition_(int64_t ts_now, IfStatus next, std::string reason) {
  IfStatus prev = status_;
  status_ = next;
  last_transition_ts_ = ts_now;
  reset_counters_for_state_(next);

  // Provide a human-readable reason with previous/next states.
  std::ostringstream oss;
  oss << to_string(prev) << " -> " << to_string(next) << ": " << reason;

  return FsmUpdate{status_, true, oss.str()};
}

void HysteresisFsm::reset_counters_for_state_(IfStatus s) {
  // Reset everything by default. This avoids counter “carry-over” creating surprises.
  cnt_below_healthy_exit_ = 0;
  cnt_above_healthy_enter_ = 0;
  cnt_below_down_enter_ = 0;
  cnt_above_down_exit_ = 0;

  // If you want to preserve some counters across transitions, do it intentionally.
  (void)s;
}

FsmUpdate HysteresisFsm::update(int64_t ts_now, double score, double confidence) {
  // Clamp score & confidence to reasonable range to avoid undefined behavior.
  score = std::max(0.0, std::min(1.0, score));
  confidence = std::max(0.0, std::min(1.0, confidence));

  // Optional “force down” behavior when telemetry quality is extremely low.
  // This is disabled by default to avoid surprising behavior.
  if (cfg_.force_down_if_confidence_below >= 0.0 &&
      confidence < cfg_.force_down_if_confidence_below) {
    if (status_ != IfStatus::Down) {
      // Typically allow emergency transition even if dwell time not met.
      return transition_(ts_now, IfStatus::Down,
                         "confidence " + std::to_string(confidence) +
                         " < force_down_if_confidence_below " +
                         std::to_string(cfg_.force_down_if_confidence_below));
    }
    return FsmUpdate{status_, false, ""};
  }

  // Main FSM logic.
  // Philosophy: only count evidence for transitions that are possible from the current state.
  switch (status_) {
    case IfStatus::Healthy: {
      // Evidence for Healthy -> Degraded
      if (score < cfg_.healthy_exit) {
        cnt_below_healthy_exit_++;
      } else {
        cnt_below_healthy_exit_ = 0;
      }

      // We generally *allow* leaving Healthy even if confidence is mediocre,
      // because we prefer safety (bad link should be noticed).
      if (cnt_below_healthy_exit_ >= cfg_.healthy_exit_N) {
        // Dwell time prevents rapid oscillations. For Healthy->Degraded, enforce dwell.
        if (dwell_ok_(ts_now)) {
          return transition_(ts_now, IfStatus::Degraded,
                             "score " + std::to_string(score) +
                             " < healthy_exit " + std::to_string(cfg_.healthy_exit) +
                             " for " + std::to_string(cnt_below_healthy_exit_) + " consecutive ticks");
        }
      }
      return FsmUpdate{status_, false, ""};
    }

    case IfStatus::Degraded: {
      // Evidence for Degraded -> Healthy (promotion)
      // Gate promotion on confidence to avoid “false healthy” when data is missing.
      if (confidence >= cfg_.min_confidence_for_promotion && score > cfg_.healthy_enter) {
        cnt_above_healthy_enter_++;
      } else {
        cnt_above_healthy_enter_ = 0;
      }

      // Evidence for Degraded -> Down
      if (score < cfg_.down_enter) {
        cnt_below_down_enter_++;
      } else {
        cnt_below_down_enter_ = 0;
      }

      // Priority: drop to Down if sustained bad.
      if (cnt_below_down_enter_ >= cfg_.down_enter_N) {
        // Often we allow fast drop to Down even if dwell time not met (safety).
        return transition_(ts_now, IfStatus::Down,
                           "score " + std::to_string(score) +
                           " < down_enter " + std::to_string(cfg_.down_enter) +
                           " for " + std::to_string(cnt_below_down_enter_) + " consecutive ticks");
      }

      // Otherwise consider promotion to Healthy (more conservative).
      if (cnt_above_healthy_enter_ >= cfg_.healthy_enter_N) {
        if (dwell_ok_(ts_now)) {
          return transition_(ts_now, IfStatus::Healthy,
                             "score " + std::to_string(score) +
                             " > healthy_enter " + std::to_string(cfg_.healthy_enter) +
                             " with confidence " + std::to_string(confidence) +
                             " for " + std::to_string(cnt_above_healthy_enter_) + " consecutive ticks");
        }
      }

      return FsmUpdate{status_, false, ""};
    }

    case IfStatus::Down: {
      // Evidence for Down -> Degraded (recovery)
      if (score > cfg_.down_exit) {
        cnt_above_down_exit_++;
      } else {
        cnt_above_down_exit_ = 0;
      }

      if (cnt_above_down_exit_ >= cfg_.down_exit_N) {
        if (dwell_ok_(ts_now)) {
          return transition_(ts_now, IfStatus::Degraded,
                             "score " + std::to_string(score) +
                             " > down_exit " + std::to_string(cfg_.down_exit) +
                             " for " + std::to_string(cnt_above_down_exit_) + " consecutive ticks");
        }
      }
      return FsmUpdate{status_, false, ""};
    }
  }

  return FsmUpdate{status_, false, ""};
}

} // namespace telemetry
