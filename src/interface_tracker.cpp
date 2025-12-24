// interface_tracker.cpp
#include "interface_tracker.hpp"

#include <algorithm>

namespace telemetry {

InterfaceTracker::InterfaceTracker(std::string iface, AgentConfig cfg)
  : iface_(std::move(iface)),
    cfg_(cfg),
    fsm_(cfg_.fsm, IfStatus::Degraded) {
  last_snapshot_.iface = iface_;
}

double InterfaceTracker::clamp01(double x) {
  if (x < 0.0) return 0.0;
  if (x > 1.0) return 1.0;
  return x;
}

double InterfaceTracker::norm_tp(double mbps) { return clamp01(mbps / 200.0); }
double InterfaceTracker::norm_rtt(double ms) { return clamp01(1.0 - (ms - 10.0) / 790.0); }
double InterfaceTracker::norm_loss(double pct) { return clamp01(1.0 - pct / 30.0); }
double InterfaceTracker::norm_jit(double ms) { return clamp01(1.0 - ms / 200.0); }

double InterfaceTracker::compute_avg_score_(const RollingWindow::Summary& s) const {
  const double n_tp = norm_tp(s.avg_throughput_mbps);
  const double n_rtt = norm_rtt(s.avg_rtt_ms);
  const double n_loss = norm_loss(s.avg_loss_pct);
  const double n_jit = norm_jit(s.avg_jitter_ms);

  const double score = cfg_.score.w_tp * n_tp +
                       cfg_.score.w_rtt * n_rtt +
                       cfg_.score.w_loss * n_loss +
                       cfg_.score.w_jit * n_jit;
  return clamp01(score);
}

double InterfaceTracker::update_ewma_(double prev, double current) const {
  const double a = cfg_.score.ewma_alpha;
  double ewma = a * current + (1.0 - a) * prev;
  if (cfg_.score.enable_downtrend_penalty && current < prev) {
    ewma -= cfg_.score.downtrend_penalty;
  }
  return clamp01(ewma);
}

void InterfaceTracker::recompute_(int64_t now_ts) {
  const auto s = window_.summary();

  score_avg_ = compute_avg_score_(s);

  if (!have_ewma_) {
    score_ewma_ = score_avg_;
    have_ewma_ = true;
  } else if (cfg_.score.useEwma) {
    score_ewma_ = update_ewma_(score_ewma_, score_avg_);
  } else {
    score_ewma_ = score_avg_;
  }

  double candidate = cfg_.score.useEwma ? score_ewma_ : score_avg_;

  const bool low_conf = (s.confidence < cfg_.score.min_confidence_for_promotion);
  if (cfg_.score.enable_confidence_cap && low_conf) {
    candidate = std::min(candidate, cfg_.score.score_cap_when_low_conf);
  }
  score_used_ = candidate;

  const IfStatus before = fsm_.status();
  const FsmUpdate upd = fsm_.update(now_ts, score_used_, s.confidence);
  const IfStatus after = upd.status;

  if (upd.transitioned) {
    pending_transition_ = TransitionEvent{iface_, now_ts, before, after, upd.reason};
  }

  last_snapshot_.iface = iface_;
  last_snapshot_.status = after;
  last_snapshot_.score_raw = score_avg_;
  last_snapshot_.score_smoothed = score_ewma_;
  last_snapshot_.score_used = score_used_;
  last_snapshot_.confidence = s.confidence;
  last_snapshot_.missing_rate = s.missing_rate;
  last_snapshot_.avg_tp_mbps = s.avg_throughput_mbps;
  last_snapshot_.avg_rtt_ms = s.avg_rtt_ms;
  last_snapshot_.avg_loss_pct = s.avg_loss_pct;
  last_snapshot_.avg_jitter_ms = s.avg_jitter_ms;
}

void InterfaceTracker::ingest(int64_t ts, const Metrics& m) {
  window_.ingest(ts, m);
  recompute_(window_.newest_ts());
}

void InterfaceTracker::note_time(int64_t ts_now) {
  window_.note_time(ts_now);
  recompute_(ts_now);
}

std::optional<TransitionEvent> InterfaceTracker::drain_transition() {
  auto out = pending_transition_;
  pending_transition_.reset();
  return out;
}

} // namespace telemetry
