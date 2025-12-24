// telemetry_agent_cli.cpp
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

static ScenarioId parse_scenario(const std::string& s) {
  if (s == "A" || s == "a") return ScenarioId::A;
  if (s == "B" || s == "b") return ScenarioId::B;
  if (s == "C" || s == "c") return ScenarioId::C;
  if (s == "D" || s == "d") return ScenarioId::D;
  std::cerr << "Unknown scenario: " << s << " (use A|B|C|D|all)\n";
  std::exit(2);
}

static AgentConfig default_config() {
  AgentConfig cfg;
  cfg.score.ewma_alpha = 0.25;
  cfg.score.useEwma = true;
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

static void print_table(int64_t t, const std::vector<InterfaceSnapshot>& snaps, bool useEwma) {
  std::cout << "\n[t=" << t << "s] (useEwma=" << (useEwma? "true":"false") << ")\n";
  std::cout << std::left
            << std::setw(6)  << "iface"
            << std::setw(9)  << "status"
            << std::setw(8)  << "used"
            << std::setw(8)  << "raw"
            << std::setw(8)  << "ewma"
            << std::setw(7)  << "conf"
            << std::setw(10) << "tp"
            << std::setw(10) << "rtt"
            << std::setw(10) << "loss"
            << std::setw(10) << "jit"
            << "\n";

  auto ordered = snaps;
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b){ return a.iface < b.iface; });

  for (const auto& s : ordered) {
    std::cout << std::left
              << std::setw(6)  << s.iface
              << std::setw(9)  << to_string(s.status)
              << std::setw(8)  << std::fixed << std::setprecision(3) << s.score_used
              << std::setw(8)  << std::fixed << std::setprecision(3) << s.score_raw
              << std::setw(8)  << std::fixed << std::setprecision(3) << s.score_smoothed
              << std::setw(7)  << std::fixed << std::setprecision(2) << s.confidence
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_tp_mbps
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_rtt_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.avg_loss_pct
              << std::setw(10) << std::fixed << std::setprecision(1) << s.avg_jitter_ms
              << "\n";
  }
}

static void run_once(ScenarioId sid, bool useEwma, int seconds) {
  AgentConfig cfg = default_config();
  cfg.score.useEwma = useEwma;

  TelemetryAgent agent(cfg);
  const std::vector<std::string> ifaces = {"eth0","wifi0","lte0","sat0"};
  for (auto& i : ifaces) agent.ensure_interface(i);

  ScenarioGenerator gen(sid);

  for (int64_t t = 0; t < seconds; ++t) {
    agent.note_time(t);
    for (const auto& iface : ifaces) {
      auto g = gen.sample(iface, t);
      if (!g) continue;
      agent.ingest(iface, g->ts, g->m);
    }

    print_table(t, agent.snapshots(), useEwma);

    for (const auto& ev : agent.drain_transitions()) {
      std::cout << "  TRANSITION [" << ev.ts << "s] " << ev.iface
                << " " << to_string(ev.from) << "->" << to_string(ev.to)
                << " | " << ev.reason << "\n";
    }

    agent.record_tick();
  }

  std::cout << "\n=== Ranking by avg score_used ===\n";
  for (const auto& it : agent.summary_ranked()) {
    std::cout << "  " << it.iface << " avg=" << std::fixed << std::setprecision(3) << it.avg_score
              << " last=" << to_string(it.last_status) << "\n";
  }
}

int main(int argc, char** argv) {
  std::string scenario_arg = "A";
  int seconds = 90;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--scenario" && i + 1 < argc) scenario_arg = argv[++i];
    else if (a == "--seconds" && i + 1 < argc) seconds = std::stoi(argv[++i]);
  }

  if (scenario_arg == "all" || scenario_arg == "ALL") {
    for (ScenarioId sid : {ScenarioId::A, ScenarioId::B, ScenarioId::C, ScenarioId::D}) {
      for (bool useEwma : {false, true}) {
        std::cout << "\n\n=== Scenario " << scenario_name(sid) << " useEwma=" << (useEwma? "true":"false") << " ===\n";
        run_once(sid, useEwma, seconds);
      }
    }
    return 0;
  }

  const ScenarioId sid = parse_scenario(scenario_arg);
  for (bool useEwma : {false, true}) {
    std::cout << "\n\n=== Scenario " << scenario_name(sid) << " useEwma=" << (useEwma? "true":"false") << " ===\n";
    run_once(sid, useEwma, seconds);
  }
  return 0;
}
