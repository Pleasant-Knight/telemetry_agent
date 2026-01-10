// Benchmark wrapper for the standalone full_agent executable.
// Runs scenarios A/B/C and measures wall time for each run.
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

struct Options {
  bool run_all = true;
  char scenario = 'A';
  int runs = 3;
};

static char parse_scenario(const std::string& s) {
  if (s == "A" || s == "a") return 'A';
  if (s == "B" || s == "b") return 'B';
  if (s == "C" || s == "c") return 'C';
  std::cerr << "Unknown scenario: " << s << " (use A|B|C)\n";
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
      opt.run_all = false;
    } else if (a == "--runs" && i + 1 < argc) {
      opt.runs = parse_int(argv[++i], "--runs");
    } else if (a == "--help" || a == "-h") {
      std::printf("Usage: benchmark_full_agent [--scenario A|B|C] [--runs N]\n");
      std::exit(0);
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      std::exit(2);
    }
  }
  return opt;
}

static std::string full_agent_exe() {
#ifdef _WIN32
  return "full_agent.exe";
#else
  return "./full_agent";
#endif
}

static double run_once(char scenario) {
  const std::string cmd = full_agent_exe() + " run --scenario " + std::string(1, scenario);
  const auto start = std::chrono::steady_clock::now();
  const int rc = std::system(cmd.c_str());
  const auto end = std::chrono::steady_clock::now();
  if (rc != 0) {
    std::cerr << "full_agent failed for scenario " << scenario << ", rc=" << rc << "\n";
    std::exit(1);
  }
  return std::chrono::duration<double>(end - start).count();
}

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);
  std::vector<char> scenarios;
  if (opt.run_all) {
    scenarios = {'A', 'B', 'C'};
  } else {
    scenarios = {opt.scenario};
  }

  std::printf("benchmark_full_agent\n");
  std::printf("  runs=%d\n\n", opt.runs);

  for (char s : scenarios) {
    double total = 0.0;
    for (int i = 0; i < opt.runs; ++i) {
      total += run_once(s);
    }
    const double avg = total / std::max(1, opt.runs);
    std::printf("Scenario %c avg_time_s=%.6f total_time_s=%.6f\n",
                s, avg, total);
  }

  return 0;
}
