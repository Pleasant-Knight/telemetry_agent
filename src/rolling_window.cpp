#include "rolling_window.hpp"

namespace telemetry {

// Safe modulo for negative timestamps (just in case).
int RollingWindow::idx(int64_t ts) {
  int64_t m = ts % kWindow;
  if (m < 0) {
    m += kWindow;
  }
  return static_cast<int>(m);
}

bool RollingWindow::ingest(int64_t ts, const Metrics& m) {
  if (newest_ts_ == std::numeric_limits<int64_t>::min() || (ts > newest_ts_)) {
    newest_ts_ = ts;
  }

  const int64_t oldest_allowed = newest_ts_ - (kWindow - 1);
  if (ts < oldest_allowed) { 
    return false;  // too old, reject!
  }

  const int i = idx(ts);
  Slot& s = slots_[i];

  // Empty slot -> fill
  if (!s.valid) {
    s.valid = true;
    s.ts = ts;
    s.m = m;
    return true;
  }
  // Occupied slot with same ts --> replace/update.
  if (s.ts == ts) {
    s.m = m;
    return true;
  }

  // Different timestamp -> overwrite (evict whatever was there)
  // We don't maintain global aggregates in strategy #1, so overwrite is enough.
  s.ts = ts;
  s.m = m;
  s.valid = true;
  return true;
}

void RollingWindow::note_time(int64_t ts_now) {
  if (newest_ts_ == std::numeric_limits<int64_t>::min()) {
    newest_ts_ = ts_now;
  } else if (ts_now > newest_ts_) {
    newest_ts_ = ts_now;
  }
}

RollingWindow::Summary RollingWindow::summary() const {
  Summary out{};
  out.expected = kWindow;

  if (newest_ts_ == std::numeric_limits<int64_t>::min()) {
    // No samples yet: keep defaults.
    out.missing_rate = 1.0;
    out.confidence = 0.0;
    return out;
  }

  out.newest_ts = newest_ts_;
  out.oldest_ts = newest_ts_ - (kWindow - 1);

  double sr = 0.0, st = 0.0, sl = 0.0, sj = 0.0;
  int c = 0;

  for (const auto& s : slots_) {
    if (!s.valid) {
        continue;
    }
    if (!in_range(s.ts, out.oldest_ts, out.newest_ts)) {
        continue;
    }
    // Clean sample within range.
    sr += s.m.rtt_ms;
    st += s.m.throughput_mbps;
    sl += s.m.loss_pct;
    sj += s.m.jitter_ms;
    c++;
  }

  out.count = c;
  out.missing_rate = 1.0 - (static_cast<double>(c) / static_cast<double>(kWindow));
  out.confidence = static_cast<double>(c) / static_cast<double>(kWindow);

  if (c > 0) {
    out.avg_rtt  = sr / c;
    out.avg_tp   = st / c;
    out.avg_loss = sl / c;
    out.avg_jit  = sj / c;
  }
  return out;
}

bool RollingWindow::has_sample(int64_t ts) const {
  if (newest_ts_ == std::numeric_limits<int64_t>::min()) {
    return false;
  }

  const int64_t oldest = newest_ts_ - (kWindow - 1);
  if (!in_range(ts, oldest, newest_ts_)) {
    return false;
  }

  const int i = idx(ts);
  const Slot& s = slots_[i];

  return s.valid && s.ts == ts;
}

std::optional<Metrics> RollingWindow::get(int64_t ts) const {
  if (!has_sample(ts)) {
    return std::nullopt;
  }
  const Slot& s = slots_[idx(ts)];
  return s.m;
}

} // namespace telemetry
