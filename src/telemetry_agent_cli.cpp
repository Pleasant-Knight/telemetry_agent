#include <cstdlib>
#include <cstdio>
#include <algorithm>
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
  if (s == "D" || s == "d") return ScenarioId::D;
  std::cerr << "Unknown scenario: " << s << " (use A|B|C|D)\n";
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
              << std::setw(8)  << std::fixed << std::setprecision(3) << s.score_used
              << std::setw(8)  << std::fixed << std::setprecision(2) << s.confidence
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_tp_mbps
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_rtt_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.avg_loss_pct
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_jitter_ms
              << "\n";
  }
}

static const char* yn(bool b) { return b ? "true" : "false"; }

static void run_one(ScenarioId sid, AgentConfig cfg) {
  std::cout << "\n=== Scenario "
            << (sid == ScenarioId::A ? "A" : sid == ScenarioId::B ? "B" : sid == ScenarioId::C ? "C" : "D")
            << " (useEwma=" << yn(cfg.score.useEwma) << ") ===\n";

  TelemetryAgent agent(cfg);
  const std::vector<std::string> ifaces = {"eth0", "wifi0", "lte0", "sat0"};
  for (auto& i : ifaces) agent.ensure_interface(i);

  // Deterministic scenarios, 90 seconds
  ScenarioGenerator gen(sid, ImperfectDataConfig{.enable_missing = false, .enable_late = false});

  for (int64_t t = 0; t < 90; ++t) {
    agent.note_time(t);
    for (const auto& iface : ifaces) {
      auto g = gen.sample(iface, t);
      if (!g) continue;
      agent.ingest(iface, g->ts, g->m);
    }

    auto snaps = agent.snapshots();
    print_table(t, snaps);

    auto evs = agent.drain_transitions();
    for (const auto& ev : evs) {
      std::cout << "  TRANSITION [" << ev.ts << "s] " << ev.iface
                << " " << status_str(ev.from) << " -> " << status_str(ev.to)
                << " | " << ev.reason << "\n";
    }

    agent.record_tick();
  }

  auto rs = agent.summary();
  std::cout << "\n=== End-of-run summary (rank by avg score_used) ===\n";
  for (const auto& s : rs.ranked) {
    std::cout << "  " << s.iface << " avg_score=" << std::fixed << std::setprecision(3)
              << s.avg_score << " last_status=" << status_str(s.last_status) << "\n";
  }
}

int main(int argc, char** argv) {
  std::printf("Minimal CLI: telemetry_agent_cli run --scenario A|B|C|D\n");
  std::string scenario_arg;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--scenario" && i + 1 < argc) {
        scenario_arg = argv[++i];
    }
  }
  if (scenario_arg.empty()) {
    std::cerr << "Usage: telemetry_agent_cli run --scenario A|B|C|D\n";
    return 2;
  }

  const bool run_all = (scenario_arg == "all" || scenario_arg == "ALL");
  ScenarioId sid = run_all ? ScenarioId::A : parse_scenario(scenario_arg);

  AgentConfig cfg;
  // Sensible defaults (tune later)
  cfg.score.ewma_alpha = 0.25;
  cfg.score.useEwma = true; // default (Strategy 2)
  cfg.fsm.healthy_enter = 0.78;
  cfg.fsm.healthy_exit  = 0.70;
  cfg.fsm.down_enter    = 0.35;
  cfg.fsm.down_exit     = 0.45;
  cfg.fsm.healthy_enter_N = 8;
  cfg.fsm.healthy_exit_N  = 5;
  cfg.fsm.down_enter_N    = 3;
  cfg.fsm.down_exit_N     = 5;
  cfg.fsm.min_dwell_sec   = 5;

  auto run_sid = [&](ScenarioId x) {
    // Run twice: Strategy 1 then Strategy 2.
    cfg.score.useEwma = false;
    run_one(x, cfg);
    cfg.score.useEwma = true;
    run_one(x, cfg);
  };

  if (run_all) {
    run_sid(ScenarioId::A);
    run_sid(ScenarioId::B);
    run_sid(ScenarioId::C);
    run_sid(ScenarioId::D);
  } else {
    run_sid(sid);
  }

  return 0;
}
