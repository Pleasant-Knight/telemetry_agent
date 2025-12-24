// scenarios.cpp
#include "scenarios.hpp"

#include <algorithm>

namespace telemetry {

ScenarioGenerator::ScenarioGenerator(ScenarioId id, ImperfectDataConfig imp)
  : id_(id), imp_(imp) {}

const char* scenario_name(ScenarioId id) {
  switch (id) {
    case ScenarioId::A: return "A";
    case ScenarioId::B: return "B";
    case ScenarioId::C: return "C";
    case ScenarioId::D: return "D";
  }
  return "?";
}

double ScenarioGenerator::lerp(double a, double b, double u) {
  u = std::max(0.0, std::min(1.0, u));
  return a + (b - a) * u;
}

std::optional<ScenarioGenerator::Generated>
ScenarioGenerator::sample(const std::string& iface, int64_t t) const {
  ImperfectDataConfig imp = imp_;
  if (id_ == ScenarioId::D) {
    imp.enable_missing = true;
    imp.enable_late = true;
  }

  if (imp.enable_missing && imp.drop_every_n > 0) {
    const int salt = static_cast<int>(iface.size());
    if (((t + salt) % imp.drop_every_n) == 0) return std::nullopt;
  }

  int64_t out_ts = t;
  if (imp.enable_late && imp.late_every_n > 0) {
    const int salt = static_cast<int>(iface.empty() ? 0 : iface[0]);
    if (((t + salt) % imp.late_every_n) == 0) out_ts = t - imp.late_by_sec;
  }

  Metrics m{};
  if (iface == "eth0") m = eth0_(t);
  else if (iface == "wifi0") m = wifi0_(t);
  else if (iface == "lte0") m = lte0_(t);
  else if (iface == "sat0") m = sat0_(t);
  else return std::nullopt;

  return Generated{out_ts, m};
}

Metrics ScenarioGenerator::eth0_(int64_t) const {
  return Metrics{20, 180, 0.1, 3};
}
Metrics ScenarioGenerator::sat0_(int64_t) const {
  return Metrics{550, 60, 0.5, 25};
}
Metrics ScenarioGenerator::lte0_(int64_t t) const {
  double wig = (t % 10) * 0.3;
  Metrics m{90 + wig, 90, 1.0, 10 + 0.5*wig};
  if (id_ == ScenarioId::C) m = scenarioC_lte_(t);
  return m;
}

Metrics ScenarioGenerator::wifi0_(int64_t t) const {
  Metrics base{35, 110, 0.5, 6};
  if (id_ == ScenarioId::A) return scenarioA_wifi_(t);
  if (id_ == ScenarioId::B) return scenarioB_wifi_(t);
  if (id_ == ScenarioId::C) {
    base.throughput_mbps = 70;
    base.loss_pct = 0.3;
    base.jitter_ms = 5;
    return base;
  }
  return base;
}

Metrics ScenarioGenerator::scenarioA_wifi_(int64_t t) const {
  Metrics good{35, 110, 0.5, 6};
  Metrics bad{300, 30, 12.0, 80};

  if (t < 35) {
    double u = static_cast<double>(t) / 35.0;
    return Metrics{lerp(good.rtt_ms, bad.rtt_ms, u),
                   lerp(good.throughput_mbps, bad.throughput_mbps, u),
                   lerp(good.loss_pct, bad.loss_pct, u),
                   lerp(good.jitter_ms, bad.jitter_ms, u)};
  } else if (t < 55) {
    double u = static_cast<double>(t - 35) / 20.0;
    return Metrics{lerp(bad.rtt_ms, good.rtt_ms, u),
                   lerp(bad.throughput_mbps, good.throughput_mbps, u),
                   lerp(bad.loss_pct, good.loss_pct, u),
                   lerp(bad.jitter_ms, good.jitter_ms, u)};
  }
  return good;
}

Metrics ScenarioGenerator::scenarioB_wifi_(int64_t t) const {
  Metrics good{35, 110, 0.5, 6};
  Metrics spike{350, 90, 10.0, 70};
  int phase = static_cast<int>(t % 15);
  if (phase < 4) return spike;
  return good;
}

Metrics ScenarioGenerator::scenarioC_lte_(int64_t t) const {
  double loss = 8.0 + (t % 5) * 1.0;
  double jit = 60.0 + (t % 7) * 3.0;
  return Metrics{95, 160, loss, jit};
}

} // namespace telemetry
