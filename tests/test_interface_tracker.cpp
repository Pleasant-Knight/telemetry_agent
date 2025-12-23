// test_interface_tracket.cpp
// Tiny driver to show how InterfaceTracker + HysteresisFsm behave.
// You can replace the sample generator with real collection code.
#include "interface_tracker.hpp"
#include <iostream>

using namespace telemetry;

int main() {
  AgentConfig cfg;

  // Example tuning (you will likely tune per product realities).
  cfg.score.ewma_alpha = 0.25;
  cfg.fsm.healthy_enter = 0.78;
  cfg.fsm.healthy_exit  = 0.70;
  cfg.fsm.down_enter    = 0.35;
  cfg.fsm.down_exit     = 0.45;

  cfg.fsm.healthy_enter_N = 8;
  cfg.fsm.healthy_exit_N  = 5;
  cfg.fsm.down_enter_N    = 3;
  cfg.fsm.down_exit_N     = 5;
  cfg.fsm.min_dwell_sec   = 5;

  InterfaceTracker eth0("eth0", cfg);

  // Simulate 1Hz samples for 80 seconds.
  for (int64_t t = 1000; t < 1080; ++t) {
    Metrics m;
    if (t < 1030) {
      // Good link
      m.throughput_mbps = 150;
      m.rtt_ms = 30;
      m.loss_pct = 0.2;
      m.jitter_ms = 5;
    } else if (t < 1055) {
      // Degraded: loss + jitter increasing, throughput still high
      m.throughput_mbps = 120;
      m.rtt_ms = 120;
      m.loss_pct = 5.0;
      m.jitter_ms = 40;
    } else {
      // Bad: near-down
      m.throughput_mbps = 10;
      m.rtt_ms = 600;
      m.loss_pct = 25.0;
      m.jitter_ms = 150;
    }

    eth0.ingest(t, m);

    if (auto ev = eth0.last_transition()) {
      std::cout << "[" << ev->ts << "] " << ev->iface << " transition: "
                << to_string(ev->from) << " -> " << to_string(ev->to)
                << " | " << ev->reason << "\n";
    }

    auto snap = eth0.snapshot();
    std::cout << "[" << t << "] score=" << snap.score_smoothed
              << " conf=" << snap.confidence
              << " status=" << to_string(snap.status) << "\n";
  }

  return 0;
}
