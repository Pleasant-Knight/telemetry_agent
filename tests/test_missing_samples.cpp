#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

static bool is_finite_double(double x) { return std::isfinite(x); }

static AgentConfig cfg_for(bool useEwma) {
  AgentConfig cfg;
  cfg.score.useEwma = useEwma;
  cfg.score.ewma_alpha = 0.25;
  cfg.score.enable_downtrend_penalty = false;

  cfg.fsm.healthy_enter = 0.72;
  cfg.fsm.healthy_exit  = 0.66;
  cfg.fsm.down_enter    = 0.35;
  cfg.fsm.down_exit     = 0.45;
  cfg.fsm.healthy_enter_N = 6;
  cfg.fsm.healthy_exit_N  = 6;
  cfg.fsm.down_enter_N    = 3;
  cfg.fsm.down_exit_N     = 5;
  cfg.fsm.min_dwell_sec   = 5;
  return cfg;
}

int main() {
  const std::vector<std::string> ifaces = {"eth0","wifi0","lte0","sat0"};

  for (bool useEwma : {false, true}) {
    TelemetryAgent agent(cfg_for(useEwma));
    for (auto& i : ifaces) agent.ensure_interface(i);

    // Scenario D: missing + late by default.
    ScenarioGenerator gen(ScenarioId::D);

    for (int64_t t = 0; t < 120; ++t) {
      agent.note_time(t);
      for (const auto& iface : ifaces) {
        auto g = gen.sample(iface, t);
        if (!g) continue;
        agent.ingest(iface, g->ts, g->m);
      }

      agent.drain_transitions();
      agent.record_tick();

      for (const auto& s : agent.snapshots()) {
        assert(s.confidence >= 0.0 && s.confidence <= 1.0);

        assert(is_finite_double(s.score_used));
        assert(is_finite_double(s.score_raw));
        assert(is_finite_double(s.score_smoothed));

        assert(is_finite_double(s.avg_rtt_ms));
        assert(is_finite_double(s.avg_tp_mbps));
        assert(is_finite_double(s.avg_loss_pct));
        assert(is_finite_double(s.avg_jitter_ms));
      }
    }
  }

  std::cout << "test_missing_samples OK\n";
  return 0;
}
