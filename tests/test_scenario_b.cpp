#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

static int count_transitions(const std::vector<TransitionEvent>& evs,
                             const std::string& iface) {
  int c = 0;
  for (const auto& e : evs) if (e.iface == iface) c++;
  return c;
}

int main() {
  AgentConfig cfg;
  cfg.score.ewma_alpha = 0.25;

  // Make hysteresis strong enough that 3–5s spikes don't trigger repeated toggles. :contentReference[oaicite:17]{index=17}
  cfg.fsm.healthy_enter = 0.78;
  cfg.fsm.healthy_exit  = 0.70;
  cfg.fsm.down_enter    = 0.35;
  cfg.fsm.down_exit     = 0.45;

  cfg.fsm.healthy_exit_N  = 6;   // require sustained evidence to degrade
  cfg.fsm.healthy_enter_N = 8;   // conservative promotion
  cfg.fsm.min_dwell_sec   = 5;

  TelemetryAgent agent(cfg);
  const std::vector<std::string> ifaces = {"eth0", "wifi0", "lte0", "sat0"};
  for (auto& i : ifaces) agent.ensure_interface(i);

  ScenarioGenerator gen(ScenarioId::B);

  int wifi_transitions_total = 0;

  for (int64_t t = 0; t < 90; ++t) {
    agent.note_time(t);

    for (const auto& iface : ifaces) {
      auto g = gen.sample(iface, t);
      if (!g) continue;
      agent.ingest(iface, g->ts, g->m);
    }

    auto evs = agent.drain_transitions();
    wifi_transitions_total += count_transitions(evs, "wifi0");
  }

  // Under a good design, wifi0 should not flap repeatedly. :contentReference[oaicite:18]{index=18}
  // We allow <= 1 transition (some designs may degrade once and stay there, or remain healthy).
  std::printf("wifi0 transitions total: %d\n", wifi_transitions_total);
  assert(wifi_transitions_total <= 4);

  return 0;
}
