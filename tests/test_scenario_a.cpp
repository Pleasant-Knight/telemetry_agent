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

  int transitions_raw = 0;
  int transitions_ewma = 0;

  for (bool useEwma : {false, true}) {
    TelemetryAgent agent(cfg_for(useEwma));
    for (auto& i : ifaces) agent.ensure_interface(i);
    ScenarioGenerator gen(ScenarioId::A);

    bool saw_degrade = false;
    bool saw_healthy_again = false;

    for (int64_t t=0; t<100; ++t) {
      agent.note_time(t);
      for (auto& iface : ifaces) {
        auto g = gen.sample(iface, t);
        if (!g) continue;
        agent.ingest(iface, g->ts, g->m);
      }
      auto snaps = agent.snapshots();
      for (auto& s : snaps) {
        if (s.iface != "wifi0") continue;
        if (s.status != IfStatus::Healthy && t > 5) saw_degrade = true;
        if (saw_degrade && s.status == IfStatus::Healthy) saw_healthy_again = true;
      }

      auto evs = agent.drain_transitions();
      int n = count_iface(evs, "wifi0");
      if (useEwma) transitions_ewma += n; else transitions_raw += n;

      agent.record_tick();
    }

    assert(saw_degrade);
    if (useEwma) {
      assert(saw_healthy_again);
    }
  }

  assert(transitions_ewma <= 6);
  assert(transitions_raw <= 8);

  std::printf("test_scenario_a OK (raw_trans=%d, ewma_trans=%d)\n",
              transitions_raw, transitions_ewma);
  return 0;
}
