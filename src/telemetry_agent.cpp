// telemetry_agent.cpp
#include "telemetry_agent.hpp"

#include <algorithm>

namespace telemetry {

TelemetryAgent::TelemetryAgent(AgentConfig cfg) : cfg_(cfg) {}

void TelemetryAgent::ensure_interface(const std::string& iface) {
  if (trackers_.find(iface) != trackers_.end()) return;
  trackers_.emplace(iface, InterfaceTracker(iface, cfg_));
  score_sum_[iface] = 0.0;
  score_count_[iface] = 0;
}

void TelemetryAgent::ingest(const std::string& iface, int64_t ts, const Metrics& m) {
  ensure_interface(iface);
  auto& tr = trackers_.at(iface);
  tr.ingest(ts, m);
  if (auto ev = tr.drain_transition()) pending_transitions_.push_back(*ev);
}

void TelemetryAgent::note_time(int64_t ts_now) {
  for (auto& [iface, tr] : trackers_) {
    tr.note_time(ts_now);
    if (auto ev = tr.drain_transition()) pending_transitions_.push_back(*ev);
  }
}

std::vector<InterfaceSnapshot> TelemetryAgent::snapshots() const {
  std::vector<InterfaceSnapshot> out;
  out.reserve(trackers_.size());
  for (const auto& [iface, tr] : trackers_) out.push_back(tr.snapshot());
  return out;
}

std::vector<TransitionEvent> TelemetryAgent::drain_transitions() {
  auto out = pending_transitions_;
  pending_transitions_.clear();
  return out;
}

void TelemetryAgent::record_tick() {
  for (const auto& [iface, tr] : trackers_) {
    const auto s = tr.snapshot();
    score_sum_[iface] += s.score_used;
    score_count_[iface] += 1;
  }
}

std::vector<TelemetryAgent::RunSummaryItem> TelemetryAgent::summary_ranked() const {
  std::vector<RunSummaryItem> out;
  out.reserve(trackers_.size());
  for (const auto& [iface, tr] : trackers_) {
    const int n = score_count_.at(iface);
    const double avg = (n > 0) ? (score_sum_.at(iface) / n) : 0.0;
    out.push_back(RunSummaryItem{iface, avg, tr.snapshot().status});
  }
  std::sort(out.begin(), out.end(),
            [](const auto& a, const auto& b){ return a.avg_score > b.avg_score; });
  return out;
}

} // namespace telemetry
