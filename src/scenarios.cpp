#include "scenarios.hpp"

#include <algorithm>

namespace telemetry {

ScenarioGenerator::ScenarioGenerator(ScenarioId id, ImperfectDataConfig imperfect)
  : id_(id), imp_(imperfect) {}

double ScenarioGenerator::lerp(double a, double b, double u) {
  u = std::max(0.0, std::min(1.0, u));
  return a + (b - a) * u;
}

std::optional<ScenarioGenerator::Generated>
ScenarioGenerator::sample(const std::string& iface, int64_t t) const {
  // Deterministic missing
  if (imp_.enable_missing && imp_.drop_every_n > 0) {
    // Drop pattern depends on iface to avoid synchronized drops
    const int salt = int(iface.size());
    if (((t + salt) % imp_.drop_every_n) == 0) return std::nullopt;
  }

  int64_t out_ts = t;

  // Deterministic late arrival (timestamp older than latest)
  if (imp_.enable_late && imp_.late_every_n > 0) {
    const int salt = int(iface[0]);
    if (((t + salt) % imp_.late_every_n) == 0) out_ts = t - imp_.late_by_sec;
  }

  Metrics m{};
  if (iface == "eth0") m = eth0_(t);
  else if (iface == "wifi0") m = wifi0_(t);
  else if (iface == "lte0") m = lte0_(t);
  else if (iface == "sat0") m = sat0_(t);
  else return std::nullopt;

  return Generated{out_ts, m};
}

// ---- Baselines (reasonable “router-like” metrics) ----

Metrics ScenarioGenerator::eth0_(int64_t) const {
  return Metrics{
    .rtt_ms = 20, .throughput_mbps = 180, .loss_pct = 0.1, .jitter_ms = 3
  };
}

Metrics ScenarioGenerator::sat0_(int64_t) const {
  return Metrics{
    .rtt_ms = 550, .throughput_mbps = 60, .loss_pct = 0.5, .jitter_ms = 25
  };
}

Metrics ScenarioGenerator::lte0_(int64_t t) const {
  // Default: moderate stable, mild noise (Scenario B asks mild noise). :contentReference[oaicite:5]{index=5}
  double wig = (t % 10) * 0.3; // deterministic small variation
  Metrics m{ .rtt_ms = 90 + wig, .throughput_mbps = 90, .loss_pct = 1.0, .jitter_ms = 10 + 0.5*wig };

  if (id_ == ScenarioId::C) {
    m = scenarioC_lte_(t);
  }
  return m;
}

Metrics ScenarioGenerator::wifi0_(int64_t t) const {
  // Default good wifi
  Metrics base{ .rtt_ms = 35, .throughput_mbps = 110, .loss_pct = 0.5, .jitter_ms = 6 };

  if (id_ == ScenarioId::A) return scenarioA_wifi_(t);
  if (id_ == ScenarioId::B) return scenarioB_wifi_(t);
  // Scenario C: wifi lower throughput but clean. :contentReference[oaicite:6]{index=6}
  if (id_ == ScenarioId::C) {
    base.throughput_mbps = 70;
    base.loss_pct = 0.3;
    base.jitter_ms = 5;
  }
  return base;
}

// ---- Scenario A: wifi0 gradually degrades ~40s then recovers ---- :contentReference[oaicite:7]{index=7}
Metrics ScenarioGenerator::scenarioA_wifi_(int64_t t) const {
  // 0..40 degrade, 40..70 recover, rest stable good
  Metrics good{ .rtt_ms = 35, .throughput_mbps = 110, .loss_pct = 0.5, .jitter_ms = 6 };
  Metrics bad { .rtt_ms = 300, .throughput_mbps = 30,  .loss_pct = 12.0,.jitter_ms = 80 };

  if (t < 40) {
    double u = double(t) / 40.0;
    return Metrics{
      .rtt_ms = lerp(good.rtt_ms, bad.rtt_ms, u),
      .throughput_mbps = lerp(good.throughput_mbps, bad.throughput_mbps, u),
      .loss_pct = lerp(good.loss_pct, bad.loss_pct, u),
      .jitter_ms = lerp(good.jitter_ms, bad.jitter_ms, u)
    };
  } else if (t < 70) {
    double u = double(t - 40) / 30.0;
    return Metrics{
      .rtt_ms = lerp(bad.rtt_ms, good.rtt_ms, u),
      .throughput_mbps = lerp(bad.throughput_mbps, good.throughput_mbps, u),
      .loss_pct = lerp(bad.loss_pct, good.loss_pct, u),
      .jitter_ms = lerp(bad.jitter_ms, good.jitter_ms, u)
    };
  }
  return good;
}

// ---- Scenario B: wifi0 short 3–5s spikes every ~15s (flap trap) ---- :contentReference[oaicite:8]{index=8}
Metrics ScenarioGenerator::scenarioB_wifi_(int64_t t) const {
  Metrics good{ .rtt_ms = 35, .throughput_mbps = 110, .loss_pct = 0.5, .jitter_ms = 6 };
  Metrics spike{ .rtt_ms = 350, .throughput_mbps = 90, .loss_pct = 10.0, .jitter_ms = 70 };

  // Every 15 seconds, create a deterministic 4-second spike window.
  int phase = int(t % 15);
  if (phase >= 0 && phase < 4) return spike; // 4 sec spike
  return good;
}

// ---- Scenario C: misleading throughput on lte0: high throughput but 8–12% loss + high jitter ---- :contentReference[oaicite:9]{index=9}
Metrics ScenarioGenerator::scenarioC_lte_(int64_t t) const {
  // Keep high throughput, sustained “bad quality”
  double loss = 8.0 + (t % 5) * 1.0;     // 8..12%
  double jit  = 60.0 + (t % 7) * 3.0;    // high jitter
  return Metrics{
    .rtt_ms = 95,
    .throughput_mbps = 160,
    .loss_pct = loss,
    .jitter_ms = jit
  };
}

} // namespace telemetry
