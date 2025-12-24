// rolling_window.cpp
#include "rolling_window.hpp"

namespace telemetry {

bool RollingWindow::ingest(int64_t ts, const Metrics& m) {
  if (newest_ts_ == std::numeric_limits<int64_t>::min()) {
    newest_ts_ = ts;
  } else if (ts > newest_ts_) {
    newest_ts_ = ts;
  }

  const int64_t oldest = newest_ts_ - (kWindow - 1);
  if (ts < oldest) return false;

  const int i = idx(ts);
  slots_[i].ts = ts;
  slots_[i].valid = true;
  slots_[i].m = m;
  return true;
}

void RollingWindow::note_time(int64_t ts_now) {
  if (newest_ts_ == std::numeric_limits<int64_t>::min()) {
    newest_ts_ = ts_now;
    return;
  }
  if (ts_now > newest_ts_) newest_ts_ = ts_now;
}

RollingWindow::Summary RollingWindow::summary() const {
  Summary s;
  if (newest_ts_ == std::numeric_limits<int64_t>::min()) return s;

  s.newest_ts = newest_ts_;
  s.oldest_ts = newest_ts_ - (kWindow - 1);

  double sum_rtt = 0.0, sum_tp = 0.0, sum_loss = 0.0, sum_jit = 0.0;
  int count = 0;

  for (const auto& slot : slots_) {
    if (!slot.valid) continue;
    if (!in_range(slot.ts, s.oldest_ts, s.newest_ts)) continue;
    sum_rtt += slot.m.rtt_ms;
    sum_tp += slot.m.throughput_mbps;
    sum_loss += slot.m.loss_pct;
    sum_jit += slot.m.jitter_ms;
    count++;
  }

  s.count = count;
  s.confidence = static_cast<double>(count) / static_cast<double>(kWindow);
  s.missing_rate = 1.0 - s.confidence;

  if (count > 0) {
    s.avg_rtt_ms = sum_rtt / count;
    s.avg_throughput_mbps = sum_tp / count;
    s.avg_loss_pct = sum_loss / count;
    s.avg_jitter_ms = sum_jit / count;
  }
  return s;
}

bool RollingWindow::has_sample(int64_t ts) const {
  const int i = idx(ts);
  return slots_[i].valid && slots_[i].ts == ts;
}

std::optional<Metrics> RollingWindow::get(int64_t ts) const {
  const int i = idx(ts);
  if (slots_[i].valid && slots_[i].ts == ts) return slots_[i].m;
  return std::nullopt;
}

} // namespace telemetry
