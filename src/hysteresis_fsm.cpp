// hysteresis_fsm.cpp
#include "hysteresis_fsm.hpp"

namespace telemetry {

HysteresisFsm::HysteresisFsm(FsmConfig cfg, IfStatus initial)
  : cfg_(cfg), status_(initial) {
  reset_counters_for_state_(status_);
}

FsmUpdate HysteresisFsm::transition_(int64_t ts_now, IfStatus next, std::string reason) {
  status_ = next;
  last_transition_ts_ = ts_now;
  reset_counters_for_state_(status_);
  return FsmUpdate{status_, true, std::move(reason)};
}

void HysteresisFsm::reset_counters_for_state_(IfStatus s) {
  cnt_below_healthy_exit_ = 0;
  cnt_above_healthy_enter_ = 0;
  cnt_below_down_enter_ = 0;
  cnt_above_down_exit_ = 0;

  (void)s;
}

bool HysteresisFsm::dwell_ok_(int64_t ts_now) const {
  if (cfg_.min_dwell_sec <= 0) return true;
  if (last_transition_ts_ == std::numeric_limits<int64_t>::min()) return true;
  return (ts_now - last_transition_ts_) >= cfg_.min_dwell_sec;
}

FsmUpdate HysteresisFsm::update(int64_t ts_now, double score, double confidence) {
  // Optional hard force-down when confidence is extremely low.
  if (cfg_.force_down_if_confidence_below >= 0.0 &&
      confidence < cfg_.force_down_if_confidence_below &&
      status_ != IfStatus::Down) {
    return transition_(ts_now, IfStatus::Down, "confidence below force-down threshold");
  }

  const bool allow_promotion = (confidence >= cfg_.min_confidence_for_promotion);

  if (status_ == IfStatus::Healthy) {
    if (score <= cfg_.healthy_exit) {
      ++cnt_below_healthy_exit_;
    } else {
      cnt_below_healthy_exit_ = 0;
    }

    if (cnt_below_healthy_exit_ >= cfg_.healthy_exit_N && dwell_ok_(ts_now)) {
      return transition_(ts_now, IfStatus::Degraded,
                         "score <= healthy_exit for N ticks");
    }
  } else if (status_ == IfStatus::Degraded) {
    if (score <= cfg_.down_enter) {
      ++cnt_below_down_enter_;
    } else {
      cnt_below_down_enter_ = 0;
    }

    if (allow_promotion && score >= cfg_.healthy_enter) {
      ++cnt_above_healthy_enter_;
    } else {
      cnt_above_healthy_enter_ = 0;
    }

    if (cnt_below_down_enter_ >= cfg_.down_enter_N) {
      // Allow fast drop to Down (safety) regardless of dwell time.
      return transition_(ts_now, IfStatus::Down,
                         "score <= down_enter for M ticks");
    }
    if (cnt_above_healthy_enter_ >= cfg_.healthy_enter_N && dwell_ok_(ts_now)) {
      return transition_(ts_now, IfStatus::Healthy,
                         "score >= healthy_enter for N ticks");
    }
  } else { // Down
    if (score >= cfg_.down_exit) {
      ++cnt_above_down_exit_;
    } else {
      cnt_above_down_exit_ = 0;
    }

    if (cnt_above_down_exit_ >= cfg_.down_exit_N && dwell_ok_(ts_now)) {
      return transition_(ts_now, IfStatus::Degraded,
                         "score >= down_exit for P ticks");
    }
  }

  return FsmUpdate{status_, false, ""};
}

} // namespace telemetry
