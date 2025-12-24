// interface_tracker.cpp
#include "interface_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace telemetry {

InterfaceTracker::InterfaceTracker(std::string iface, AgentConfig cfg)
  : iface_(std::move(iface)),
    cfg_(cfg),
    fsm_(cfg_.fsm, IfStatus::Degraded) {
  // Initialize snapshot with iface name for better operator experience.
  last_snapshot_.iface = iface_;
}

double InterfaceTracker::clamp01_(double x) {
  return std::max(0.0, std::min(1.0, x));
}

std::optional<TransitionEvent> InterfaceTracker::drain_transition() {
  auto out = last_transition_;
  last_transition_.reset();   // IMPORTANT: clear so it can’t be emitted again
  return out;
}

void InterfaceTracker::ingest(int64_t ts, const Metrics& m) {
  const bool accepted = window_.ingest(ts, m);
  if (!accepted) {
    std::cerr << "Dropping too-old sample for " << iface_ << " at ts=" << ts << "\n";
    return;
  }
  // Recompute using the current notion of newest time inside window_.
  recompute_(window_.newest_ts());
}

void InterfaceTracker::note_time(int64_t ts_now) {
  window_.note_time(ts_now);
  recompute_(ts_now);
}

// Strategy 1: Convert rolling window means into a normalized weighted score.
// This score uses only the *current window averages* (no trend awareness).
double InterfaceTracker::compute_avg_score_(const RollingWindow::Summary& s) const {
  if (s.count <= 0) return 0.0;

  // Normalize each metric to [0..1] where 1 is best.
  // Throughput: higher better.
  const double T = clamp01_(s.avg_tp / cfg_.score.tp_max_mbps);

  // RTT: lower better; map rtt_min..rtt_max to 1..0.
  const double rtt_span = std::max(1e-9, (cfg_.score.rtt_max_ms - cfg_.score.rtt_min_ms));
  const double R = 1.0 - clamp01_((s.avg_rtt - cfg_.score.rtt_min_ms) / rtt_span);

  // Loss: lower better.
  const double L = 1.0 - clamp01_(s.avg_loss / cfg_.score.loss_max_pct);

  // Jitter: lower better.
  const double J = 1.0 - clamp01_(s.avg_jit / cfg_.score.jit_max_ms);

  const auto& w = cfg_.score;
  const double score = (w.w_tp * T) + (w.w_rtt * R) + (w.w_loss * L) + (w.w_jit * J);
  return clamp01_(score);
}

// Strategy 2: EWMA-smoothed score with an optional downtrend penalty.
// If the instantaneous average score falls below the previous EWMA, subtract
// a small fixed penalty to make downward trends "count" more than brief upward noise.
double InterfaceTracker::compute_ewma_score_(double prev_ewma, double current_avg) const {
  const double a = clamp01_(cfg_.score.ewma_alpha);
  double ewma = a * current_avg + (1.0 - a) * prev_ewma;

  if (cfg_.score.enable_downtrend_penalty && current_avg < prev_ewma) {
    ewma -= std::abs(cfg_.score.downtrend_penalty);
  }
  return clamp01_(ewma);
}

double InterfaceTracker::apply_confidence_cap_(double score, double confidence) const {
  if (!cfg_.score.enable_confidence_cap) {
    return score;
  }
  if (confidence >= cfg_.score.cap_confidence_threshold) return score;
  // Cap the score when we are missing too much data; avoids “false healthy”.
  return std::min(score, cfg_.score.cap_max_score_when_low_conf);
}

void InterfaceTracker::recompute_(int64_t ts_now) {
  auto sum = window_.summary();

  // Confidence: fraction of seconds that have samples in the window.
  const double confidence = (sum.expected > 0) ? (double(sum.count) / double(sum.expected)) : 0.0;

  // Strategy 1 score: instantaneous, window-average score.
  score_avg_ = compute_avg_score_(sum);
  score_avg_ = apply_confidence_cap_(score_avg_, confidence);

  // Strategy 2 score: EWMA score (trend-aware).
  if (!ewma_init_) {
    score_ewma_ = score_avg_;
    ewma_init_ = true;
  } else {
    score_ewma_ = compute_ewma_score_(score_ewma_, score_avg_);
  }

  // Selection: the ONLY difference between strategies is what score we feed into the FSM.
  // - useEwma=false  -> Strategy 1 (avg score)
  // - useEwma=true   -> Strategy 2 (EWMA score)
  score_used_ = cfg_.score.useEwma ? score_ewma_ : score_avg_;

  // Run anti-flap FSM using the selected score.
  const IfStatus prev_status = fsm_.status();
  auto upd = fsm_.update(ts_now, score_used_, confidence);

  // Update snapshot for operator visibility.
  last_snapshot_ = InterfaceSnapshot{
    .iface = iface_,
    .ts = ts_now,
    .score_raw = score_avg_,
    .score_smoothed = score_ewma_,
    .score_used = score_used_,
    .confidence = confidence,
    .missing_rate = sum.missing_rate,
    .avg_rtt_ms = sum.avg_rtt,
    .avg_tp_mbps = sum.avg_tp,
    .avg_loss_pct = sum.avg_loss,
    .avg_jitter_ms = sum.avg_jit,
    .status = upd.status
  };

  // Emit transition event if status changed.
  if (upd.transitioned && prev_status != upd.status) {
    last_transition_ = TransitionEvent{
      .iface = iface_,
      .ts = ts_now,
      .from = prev_status,
      .to = upd.status,
      .reason = upd.reason
    };
  } else {
    // Clear or keep last transition depending on your preference.
    // Keeping it is useful for "last transition" queries.
    // last_transition_.reset();
  }
}

} // namespace telemetry
