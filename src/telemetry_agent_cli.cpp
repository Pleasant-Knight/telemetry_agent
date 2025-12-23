#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

static const char* status_str(IfStatus s) {
  switch (s) {
    case IfStatus::Healthy:  return "healthy";
    case IfStatus::Degraded: return "degraded";
    case IfStatus::Down:     return "down";
  }
  return "unknown";
}

static ScenarioId parse_scenario(const std::string& s) {
  if (s == "A" || s == "a") return ScenarioId::A;
  if (s == "B" || s == "b") return ScenarioId::B;
  if (s == "C" || s == "c") return ScenarioId::C;
  std::cerr << "Unknown scenario: " << s << " (use A|B|C)\n";
  std::exit(2);
}

static void print_table(int64_t t, const std::vector<InterfaceSnapshot>& snaps) {
  std::cout << "\n[t=" << t << "s] Interface states\n";
  std::cout << std::left
            << std::setw(6)  << "iface"
            << std::setw(8)  << "status"
            << std::setw(8)  << "score"
            << std::setw(8)  << "conf"
            << std::setw(10) << "tp(Mb)"
            << std::setw(10) << "rtt(ms)"
            << std::setw(10) << "loss(%)"
            << std::setw(10) << "jit(ms)"
            << "\n";

  std::cout << std::string(70, '-') << "\n";

  // Print in stable order
  auto ordered = snaps;
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b){ return a.iface < b.iface; });

  for (const auto& s : ordered) {
    std::cout << std::left
              << std::setw(6)  << s.iface
              << std::setw(8)  << status_str(s.status)
              << std::setw(8)  << std::fixed << std::setprecision(3) << s.score_smoothed
              << std::setw(8)  << std::fixed << std::setprecision(2) << s.confidence
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_tp_mbps
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_rtt_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.avg_loss_pct
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_jitter_ms
              << "\n";
  }
}

int main(int argc, char** argv) {
  std::printf("Minimal CLI: telemetry_agent_cli run --scenario A|B|C\n");
  std::string scenario_arg;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--scenario" && i + 1 < argc) {
        scenario_arg = argv[++i];
    }
  }
  if (scenario_arg.empty()) {
    std::cerr << "Usage: telemetry_agent_cli run --scenario A|B|C\n";
    return 2;
  }

  ScenarioId sid = parse_scenario(scenario_arg);

  AgentConfig cfg;
  // Sensible defaults (tune later)
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

  TelemetryAgent agent(cfg);

  // Required interfaces :contentReference[oaicite:11]{index=11}
  const std::vector<std::string> ifaces = {"eth0", "wifi0", "lte0", "sat0"};
  for (auto& i : ifaces)  {
    agent.ensure_interface(i);
  }

  // Deterministic scenarios, 90 seconds :contentReference[oaicite:12]{index=12}
  ScenarioGenerator gen(sid, ImperfectDataConfig{
    .enable_missing = false,
    .enable_late = false
  });

  for (int64_t t = 0; t < 90; ++t) {
    std::printf("Advance time for expiration even if missing samples.\n");
    agent.note_time(t);

    for (const auto& iface : ifaces) {
      auto g = gen.sample(iface, t);
      if (!g) continue; // missing (if enabled)
      agent.ingest(iface, g->ts, g->m);
    }

    // Print per-tick states (scores+status over time requested) :contentReference[oaicite:13]{index=13}
    auto snaps = agent.snapshots();
    print_table(t, snaps);

    // Print transitions with reasons :contentReference[oaicite:14]{index=14}
    auto evs = agent.drain_transitions();
    for (const auto& ev : evs) {
      std::cout << "  TRANSITION [" << ev.ts << "s] " << ev.iface
                << " " << status_str(ev.from) << " -> " << status_str(ev.to)
                << " | " << ev.reason << "\n";
    }

    agent.record_tick();
  }

  // End-of-run summary ranking :contentReference[oaicite:15]{index=15}
  auto rs = agent.summary();
  std::cout << "\n=== End-of-run summary (rank by avg score) ===\n";
  for (const auto& s : rs.ranked) {
    std::cout << "  " << s.iface << " avg_score=" << std::fixed << std::setprecision(3)
              << s.avg_score << " last_status=" << status_str(s.last_status) << "\n";
  }

  return 0;
}
