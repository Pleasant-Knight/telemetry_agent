// Simple benchmark for TelemetryAgent + ScenarioGenerator.
//
// Default behavior:
//   - run scenarios A, B, C, D
//   - for each scenario, run twice: useEwma=false then useEwma=true
//   - print a compact comparison table
//
// You can still benchmark a single scenario via: --scenario A|B|C|D
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "telemetry_agent.hpp"
#include "scenarios.hpp"

using namespace telemetry;

struct Options {
  bool run_all = true;
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
  std::cerr << "Unknown scenario: " << s << "\n";
  std::exit(2);
}

static int parse_int(const char* s, const char* flag) {
  try {
    return std::stoi(s);
  } catch (...) {
    std::cerr << "Invalid int for " << flag << ": " << s << "\n";
    std::exit(2);
  }
}

static Options parse_args(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--scenario" && i + 1 < argc) {
      opt.scenario = parse_scenario(argv[++i]);
      opt.run_all = false; // explicit scenario overrides default all
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
      std::printf(
        "Usage: benchmark_scenarios [--scenario A|B|C|D] [--seconds N] [--runs N]\n"
        "                           [--missing] [--late]\n"
        "                           [--drop-every N] [--late-every N] [--late-by N]\n\n"
        "Default: runs scenarios A,B,C,D and prints a comparison table for useEwma=false/true.\n"
      );
      std::exit(0);
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      std::exit(2);
    }
  }
  return opt;
}

struct BenchResult {
  ScenarioId scenario{};
  bool useEwma = true;
  int runs = 0;
  int seconds = 0;
  bool missing = false;
  bool late = false;

  int64_t total_ingests = 0;
  std::chrono::duration<double> total_time{0};

  double avg_time_s() const {
    return total_time.count() / std::max(1, runs);
  }
  double ingests_per_s() const {
    return (total_time.count() > 0.0) ? (double)total_ingests / total_time.count() : 0.0;
  }
  double avg_time_ms() const {
    return avg_time_s() * 1000.0;
  }
};

static BenchResult bench_one_scenario(const Options& opt, ScenarioId sid, bool useEwma, const AgentConfig& base_cfg) {
  const std::vector<std::string> ifaces = {"eth0", "wifi0", "lte0", "sat0"};

  AgentConfig cfg = base_cfg;
  cfg.score.useEwma = useEwma;

  BenchResult out;
  out.scenario = sid;
  out.useEwma = useEwma;
  out.runs = opt.runs;
  out.seconds = opt.seconds;
  out.missing = opt.missing;
  out.late = opt.late;

  ImperfectDataConfig imp{};
  imp.enable_missing = opt.missing;
  imp.enable_late = opt.late;
  imp.drop_every_n = opt.drop_every_n;
  imp.late_every_n = opt.late_every_n;
  imp.late_by_sec = opt.late_by_sec;

  for (int run = 0; run < opt.runs; ++run) {
    TelemetryAgent agent(cfg);
    for (const auto& iface : ifaces) {
      agent.ensure_interface(iface);
    }

    ScenarioGenerator gen(sid, imp);

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
    out.total_time += (end - start);
    out.total_ingests += ingests;
  }

  return out;
}

static void print_table_header(const Options& opt) {
  std::printf("benchmark_scenarios\n");
  std::printf("  runs=%d seconds=%d missing=%s late=%s",
              opt.runs,
              opt.seconds,
              opt.missing ? "true" : "false",
              opt.late ? "true" : "false");
  if (opt.missing) std::printf(" drop_every=%d", opt.drop_every_n);
  if (opt.late) std::printf(" late_every=%d late_by=%d", opt.late_every_n, opt.late_by_sec);
  std::printf("\n\n");

  std::printf("%-9s%-10s%-14s%-16s%-14s\n",
              "scenario", "useEwma", "avg_ms/run", "total_ingests", "ingests/s");
  std::printf("%s\n", std::string(63, '-').c_str());
}

static void print_row(const BenchResult& r) {
  std::printf("%-9s%-10s%-14.3f%-16lld%-14.0f\n",
              scenario_name(r.scenario),
              r.useEwma ? "true" : "false",
              r.avg_time_ms(),
              static_cast<long long>(r.total_ingests),
              r.ingests_per_s());
}

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);

  AgentConfig base_cfg;
  base_cfg.score.ewma_alpha = 0.25;
  base_cfg.score.useEwma = true; // default strategy if run standalone
  base_cfg.fsm.healthy_enter = 0.78;
  base_cfg.fsm.healthy_exit  = 0.70;
  base_cfg.fsm.down_enter    = 0.35;
  base_cfg.fsm.down_exit     = 0.45;
  base_cfg.fsm.healthy_enter_N = 8;
  base_cfg.fsm.healthy_exit_N  = 5;
  base_cfg.fsm.down_enter_N    = 3;
  base_cfg.fsm.down_exit_N     = 5;
  base_cfg.fsm.min_dwell_sec   = 5;

  std::vector<ScenarioId> scenarios;
  if (opt.run_all) {
    scenarios = {ScenarioId::A, ScenarioId::B, ScenarioId::C, ScenarioId::D};
  } else {
    scenarios = {opt.scenario};
  }

  print_table_header(opt);

  // For each scenario, run both strategies back-to-back for apples-to-apples comparison.
  for (auto sid : scenarios) {
    BenchResult r_raw  = bench_one_scenario(opt, sid, false, base_cfg);
    BenchResult r_ewma = bench_one_scenario(opt, sid, true,  base_cfg);

    print_row(r_raw);
    print_row(r_ewma);
  }

  std::printf(
    "\nLegend:\n"
    "  avg_ms/run = average wall time per run (lower is faster)\n"
    "  total_ingests = total number of agent.ingest() calls across all runs\n"
    "  ingests/s = total_ingests / total_wall_time\n"
  );
  return 0;
}
