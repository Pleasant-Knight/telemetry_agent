// telemetry_agent_cli.cpp
#include <algorithm>
#include <cstdint>
#include <cstdio>
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
  std::printf("\n[t=%llds] (useEwma=%s)\n",
              static_cast<long long>(t),
              useEwma ? "true" : "false");
  std::printf("%-6s%-9s%-8s%-8s%-8s%-7s%-10s%-10s%-10s%-10s\n",
              "iface", "status", "used", "raw", "ewma", "conf",
              "tp", "rtt", "loss", "jit");

  auto ordered = snaps;
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b){ return a.iface < b.iface; });

  for (const auto& s : ordered) {
    std::printf("%-6s%-9s%-8.3f%-8.3f%-8.3f%-7.2f%-10.1f%-10.1f%-10.2f%-10.1f\n",
                s.iface.c_str(),
                to_string(s.status),
                s.score_used,
                s.score_raw,
                s.score_smoothed,
                s.confidence,
                s.avg_tp_mbps,
                s.avg_rtt_ms,
                s.avg_loss_pct,
                s.avg_jitter_ms);
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
      std::printf("  TRANSITION [%llds] %s %s->%s | %s\n",
                  static_cast<long long>(ev.ts),
                  ev.iface.c_str(),
                  to_string(ev.from),
                  to_string(ev.to),
                  ev.reason.c_str());
    }

    agent.record_tick();
  }

  std::printf("\n=== Ranking by avg score_used ===\n");
  for (const auto& it : agent.summary_ranked()) {
    std::printf("  %s avg=%.3f last=%s\n",
                it.iface.c_str(),
                it.avg_score,
                to_string(it.last_status));
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
        std::printf("\n\n=== Scenario %s useEwma=%s ===\n",
                    scenario_name(sid),
                    useEwma ? "true" : "false");
        run_once(sid, useEwma, seconds);
      }
    }
    return 0;
  }

  const ScenarioId sid = parse_scenario(scenario_arg);
  for (bool useEwma : {false, true}) {
    std::printf("\n\n=== Scenario %s useEwma=%s ===\n",
                scenario_name(sid),
                useEwma ? "true" : "false");
    run_once(sid, useEwma, seconds);
  }
  return 0;
}
