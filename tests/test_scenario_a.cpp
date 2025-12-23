#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>
#include <string>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

int main() {
  AgentConfig cfg;
  cfg.score.ewma_alpha = 0.25;
  cfg.fsm.healthy_enter = 0.72;
  cfg.fsm.healthy_exit  = 0.66;
  cfg.fsm.healthy_enter_N = 6;
  cfg.fsm.healthy_exit_N  = 5;

  TelemetryAgent agent(cfg);
  const std::vector<std::string> ifaces = {"eth0", "wifi0", "lte0", "sat0"};
  for (auto& i : ifaces) agent.ensure_interface(i);

  ScenarioGenerator gen(ScenarioId::A);

  bool saw_wifi_degrade = false;
  bool saw_wifi_healthy_again = false;

  for (int64_t t = 0; t < 90; ++t) {
    agent.note_time(t);
    for (const auto& iface : ifaces) {
      auto g = gen.sample(iface, t);
      if (!g) continue;
      agent.ingest(iface, g->ts, g->m);
    }

    auto snap = agent.snapshots();
    for (const auto& s : snap) {
        /*if (s.iface == "wifi0" && (t % 5 == 0)) {
            std::cerr << "t=" << t
              << " status=" << int(s.status)
              << " score=" << s.score_smoothed
              << " conf=" << s.confidence
              << " avg_loss=" << s.avg_loss_pct
              << " avg_rtt=" << s.avg_rtt_ms
              << "\n";
        }*/
      if (s.iface == "wifi0") {
        if (s.status == IfStatus::Degraded) {
            saw_wifi_degrade = true;
        }
        if (saw_wifi_degrade && s.status == IfStatus::Healthy) {
            saw_wifi_healthy_again = true;
        }
      }
    }
  }

  // Scenario A expects wifi0 healthy -> degraded (not instantly) then back to healthy with hysteresis. :contentReference[oaicite:19]{index=19}
  std::printf("saw_wifi_degrade: %d, saw_wifi_healthy_again: %d\n",
              saw_wifi_degrade, saw_wifi_healthy_again);
  assert(saw_wifi_degrade);
  assert(saw_wifi_healthy_again);
  return 0;
}
