#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

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

static int count_iface(const std::vector<TransitionEvent>& evs, const std::string& iface) {
  int c=0; for (auto& e: evs) if (e.iface==iface) c++; return c;
}

int main() {
  const std::vector<std::string> ifaces = {"eth0","wifi0","lte0","sat0"};

  int raw_trans = 0;
  int ewma_trans = 0;

  for (bool useEwma : {false, true}) {
    TelemetryAgent agent(cfg_for(useEwma));
    for (auto& i : ifaces) agent.ensure_interface(i);
    ScenarioGenerator gen(ScenarioId::B);

    int transitions = 0;

    for (int64_t t=0; t<180; ++t) {
      agent.note_time(t);
      for (auto& iface : ifaces) {
        auto g = gen.sample(iface, t);
        if (!g) continue;
        agent.ingest(iface, g->ts, g->m);
      }
      transitions += count_iface(agent.drain_transitions(), "wifi0");
      agent.record_tick();
    }

    if (useEwma) ewma_trans = transitions;
    else raw_trans = transitions;
  }

  // EWMA should reduce flapping relative to raw.
  assert(ewma_trans <= raw_trans);
  // In EWMA mode, we expect only a few transitions at most.
  assert(ewma_trans <= 6);

  std::printf("test_scenario_b OK (raw=%d, ewma=%d)\n", raw_trans, ewma_trans);
  return 0;
}
