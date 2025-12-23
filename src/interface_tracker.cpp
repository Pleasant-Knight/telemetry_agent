// interface_tracker.cpp
#include "interface_tracker.hpp"

#include <algorithm>
#include <cmath>

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

void InterfaceTracker::ingest(int64_t ts, const Metrics& m) {
  const bool accepted = window_.ingest(ts, m);
  if (!accepted) {
    // Too old: ignore silently, or log in your integration layer.
    return;
  }
  // Recompute using the current notion of newest time inside window_.
  recompute_(window_.newest_ts());
}

void InterfaceTracker::note_time(int64_t ts_now) {
  window_.note_time(ts_now);
  recompute_(ts_now);
}

// Convert rolling window stats into a normalized raw score.
// This is intentionally “pure-ish” to be easy to test.
double InterfaceTracker::compute_raw_score_(const RollingWindow::Summary& s) const {
  // If there are no samples, raw score should be pessimistic (0).
  if (s.count <= 0) return 0.0;

  // Normalize each metric to [0..1] where 1 is best.
  // Throughput: higher better
  double T = clamp01_(s.avg_tp / cfg_.score.tp_max_mbps);

  // RTT: lower better; map rtt_min..rtt_max to 1..0.
  double rtt_span = std::max(1e-9, (cfg_.score.rtt_max_ms - cfg_.score.rtt_min_ms));
  double R = 1.0 - clamp01_((s.avg_rtt - cfg_.score.rtt_min_ms) / rtt_span);

  // Loss: lower better
  double L = 1.0 - clamp01_(s.avg_loss / cfg_.score.loss_max_pct);

  // Jitter: lower better
  double J = 1.0 - clamp01_(s.avg_jit / cfg_.score.jit_max_ms);

  // Weighted combine
  const auto& w = cfg_.score;
  double score = (w.w_tp * T) + (w.w_rtt * R) + (w.w_loss * L) + (w.w_jit * J);

  return clamp01_(score);
}

double InterfaceTracker::apply_confidence_cap_(double score, double confidence) const {
  if (!cfg_.score.enable_confidence_cap) return score;
  if (confidence >= cfg_.score.cap_confidence_threshold) return score;
  // Cap the score when we are missing too much data; avoids “false healthy”.
  return std::min(score, cfg_.score.cap_max_score_when_low_conf);
}

void InterfaceTracker::recompute_(int64_t ts_now) {
  auto sum = window_.summary();

  // Confidence: fraction of seconds that have samples in the window.
  const double confidence = (sum.expected > 0) ? (double(sum.count) / double(sum.expected)) : 0.0;

  double raw = compute_raw_score_(sum);
  raw = apply_confidence_cap_(raw, confidence);

  // EWMA smoothing on score to reduce jitter.
  double smoothed = raw;
  if (!ewma_init_) {
    score_ewma_ = raw;
    ewma_init_ = true;
    smoothed = raw;
  } else {
    const double a = clamp01_(cfg_.score.ewma_alpha);
    score_ewma_ = a * raw + (1.0 - a) * score_ewma_;
    smoothed = score_ewma_;
  }

  // Run anti-flap FSM using smoothed score.
  const IfStatus prev_status = fsm_.status();
  auto upd = fsm_.update(ts_now, smoothed, confidence);

  // Update snapshot for operator visibility.
  last_snapshot_ = InterfaceSnapshot{
    .iface = iface_,
    .ts = ts_now,
    .score_raw = raw,
    .score_smoothed = smoothed,
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
