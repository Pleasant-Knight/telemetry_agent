#include <cassert>
#include <iostream>

#include "interface_tracker.hpp"

using namespace telemetry;

static AgentConfig base_cfg() {
  AgentConfig cfg;
  cfg.score.ewma_alpha = 0.5;
  cfg.score.enable_downtrend_penalty = false;
  cfg.score.enable_confidence_cap = false;
  cfg.fsm.healthy_enter = 0.72;
  cfg.fsm.healthy_exit  = 0.66;
  cfg.fsm.down_enter    = 0.35;
  cfg.fsm.down_exit     = 0.45;
  cfg.fsm.healthy_enter_N = 1;
  cfg.fsm.healthy_exit_N  = 1;
  cfg.fsm.down_enter_N    = 1;
  cfg.fsm.down_exit_N     = 1;
  cfg.fsm.min_dwell_sec   = 0;
  return cfg;
}

int main() {
  // Feed a stable good signal and verify score_used follows toggle
  Metrics good{20, 180, 0.1, 3};

  { // raw mode
    auto cfg = base_cfg();
    cfg.score.useEwma = false;
    InterfaceTracker tr("eth0", cfg);
    for (int t=0;t<10;++t) { tr.note_time(t); tr.ingest(t, good); }
    auto s = tr.snapshot();
    assert(std::abs(s.score_used - s.score_raw) < 1e-9);
  }

  { // ewma mode
    auto cfg = base_cfg();
    cfg.score.useEwma = true;
    InterfaceTracker tr("eth0", cfg);
    for (int t=0;t<10;++t) { tr.note_time(t); tr.ingest(t, good); }
    auto s = tr.snapshot();
    assert(std::abs(s.score_used - s.score_smoothed) < 1e-9);
  }

  std::cout << "test_interface_tracker OK\n";
  return 0;
}
