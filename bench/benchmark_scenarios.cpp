// Simple benchmark for TelemetryAgent + ScenarioGenerator.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

struct Options {
  ScenarioId scenario = ScenarioId::A;
  int seconds = 90;
  int runs = 5;
  bool missing = false;
  bool late = false;
  int drop_every_n = 10;
  int late_every_n = 12;
  int late_by_sec = 2;
};

static ScenarioId parse_scenario(const std::string& s) {
  if (s == "A" || s == "a") return ScenarioId::A;
  if (s == "B" || s == "b") return ScenarioId::B;
  if (s == "C" || s == "c") return ScenarioId::C;
  if (s == "D" || s == "d") return ScenarioId::D;
  std::cerr << "Unknown scenario: " << s << " (use A|B|C|D)\n";
  std::exit(2);
}

static int parse_int(const std::string& s, const char* name) {
  try {
    return std::stoi(s);
  } catch (...) {
    std::cerr << "Invalid integer for " << name << ": " << s << "\n";
    std::exit(2);
  }
}

static Options parse_args(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--scenario" && i + 1 < argc) {
      opt.scenario = parse_scenario(argv[++i]);
    } else if (a == "--seconds" && i + 1 < argc) {
      opt.seconds = parse_int(argv[++i], "--seconds");
    } else if (a == "--runs" && i + 1 < argc) {
      opt.runs = parse_int(argv[++i], "--runs");
    } else if (a == "--missing") {
      opt.missing = true;
    } else if (a == "--late") {
      opt.late = true;
    } else if (a == "--drop-every" && i + 1 < argc) {
      opt.drop_every_n = parse_int(argv[++i], "--drop-every");
    } else if (a == "--late-every" && i + 1 < argc) {
      opt.late_every_n = parse_int(argv[++i], "--late-every");
    } else if (a == "--late-by" && i + 1 < argc) {
      opt.late_by_sec = parse_int(argv[++i], "--late-by");
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage: benchmark_scenarios [--scenario A|B|C|D] [--seconds N] [--runs N]\n"
                << "                           [--missing] [--late]\n"
                << "                           [--drop-every N] [--late-every N] [--late-by N]\n";
      std::exit(0);
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      std::exit(2);
    }
  }
  return opt;
}

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);

  AgentConfig cfg;
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

  const std::vector<std::string> ifaces = {"eth0", "wifi0", "lte0", "sat0"};
  int64_t total_ingests = 0;
  std::chrono::duration<double> total_time{0};

  for (int run = 0; run < opt.runs; ++run) {
    TelemetryAgent agent(cfg);
    for (const auto& iface : ifaces) {
      agent.ensure_interface(iface);
    }

    ImperfectDataConfig imp{};
    imp.enable_missing = opt.missing;
    imp.enable_late = opt.late;
    imp.drop_every_n = opt.drop_every_n;
    imp.late_every_n = opt.late_every_n;
    imp.late_by_sec = opt.late_by_sec;

    ScenarioGenerator gen(opt.scenario, imp);

    const auto start = std::chrono::steady_clock::now();
    int64_t ingests = 0;

    for (int64_t t = 0; t < opt.seconds; ++t) {
      agent.note_time(t);
      for (const auto& iface : ifaces) {
        auto g = gen.sample(iface, t);
        if (!g) continue;
        agent.ingest(iface, g->ts, g->m);
        ++ingests;
      }
      agent.record_tick();
    }

    const auto end = std::chrono::steady_clock::now();
    total_time += (end - start);
    total_ingests += ingests;
  }

  const double avg_sec = total_time.count() / std::max(1, opt.runs);
  const double ingests_per_sec = (total_time.count() > 0.0)
    ? (double)total_ingests / total_time.count()
    : 0.0;

  std::cout << "benchmark_scenarios\n";
  std::cout << "  runs=" << opt.runs
            << " seconds=" << opt.seconds
            << " missing=" << (opt.missing ? "true" : "false")
            << " late=" << (opt.late ? "true" : "false")
            << "\n";
  std::cout << "  total_time_s=" << total_time.count()
            << " avg_time_s=" << avg_sec
            << " total_ingests=" << total_ingests
            << " ingests_per_s=" << ingests_per_sec
            << "\n";

  return 0;
}
