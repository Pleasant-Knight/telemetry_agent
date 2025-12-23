#include "rolling_window.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using telemetry::Metrics;
using telemetry::RollingWindow;

static bool eq(double a, double b, double eps = 1e-9) {
  return std::fabs(a - b) <= eps;
}

int main() {
  std::printf("Starting RollingWindow tests...\n");

  {
    std::printf("Test 1: Basic ingest and summary count... ");
    // 1) Basic ingest and summary count
    RollingWindow w;
    Metrics m{100.0, 50.0, 1.0, 10.0};
    assert(w.ingest(1000, m));

    auto s = w.summary();
    assert(s.newest_ts == 1000);
    assert(s.oldest_ts == 1000 - 44);
    assert(s.count == 1);
    assert(eq(s.avg_rtt, 100));
    assert(eq(s.avg_tp, 50));
    assert(eq(s.avg_loss, 1));
    assert(eq(s.avg_jit, 10));
    std::printf("PASSED\n");
  }

  {
    std::printf("Test 2: Fill partial window and check means... ");
    // 2) Fill partial window and check means
    RollingWindow w;
    for (int i = 0; i < 10; i++) {
      Metrics m{100.0 + i, 10.0, 0.0, 0.0};
      assert(w.ingest(2000 + i, m));
    }
    auto s = w.summary();
    assert(s.count == 10);
    // avg_rtt = (100..109)/10 = 104.5
    assert(eq(s.avg_rtt, 104.5));
    std::printf("PASSED\n");
  }

  {
    std::printf("Test 3: Overwrite via ring index collision... ");
    // 3) Overwrite via ring index collision and ensure old sample disappears from window
    RollingWindow w;
    // Choose two timestamps with same idx: ts and ts+45
    Metrics a{10.0, 0.0, 0.0, 0.0};
    Metrics b{110.0, 0.0, 0.0, 0.0};
    assert(w.ingest(3000, a));
    assert(w.ingest(3045, b)); // overwrites same slot index

    // newest_ts=3045 window is [3001..3045], so ts=3000 is out anyway
    auto s = w.summary();
    assert(s.newest_ts == 3045);
    assert(!w.has_sample(3000));
    assert(w.has_sample(3045));
    assert(s.count == 1);
    assert(eq(s.avg_rtt, 110));
    std::printf("PASSED\n");
  }

  {
    std::printf("Test 4: Correction for same timestamp replaces value... ");
    // 4) Correction for same timestamp replaces value
    RollingWindow w;
    Metrics a{50.0, 0.0, 0.0, 0.0};
    Metrics b{70.0, 0.0, 0.0, 0.0};
    assert(w.ingest(4000, a));
    assert(w.ingest(4000, b)); // correction
    auto s = w.summary();
    assert(s.count == 1);
    assert(eq(s.avg_rtt, 70));
    auto got = w.get(4000);
    assert(got.has_value());
    assert(eq(got->rtt_ms, 70));
    std::printf("PASSED\n");
  }

  {
    std::printf("Test 5: Out-of-order within window is accepted... ");
    // 5) Out-of-order within window is accepted and counted
    RollingWindow w;
    assert(w.ingest(5000, Metrics{10.0, 0.0, 0.0, 0.0}));
    assert(w.ingest(5002, Metrics{30.0, 0.0, 0.0, 0.0}));
    assert(w.ingest(5001, Metrics{20.0, 0.0, 0.0, 0.0})); // late sample

    auto s = w.summary();
    assert(s.count == 3);
    assert(eq(s.avg_rtt, 20.0)); // (10+20+30)/3
    std::printf("PASSED\n");
  }

  {
    std::printf("Test 6: Too-old sample rejected... ");
    // 6) Too-old sample rejected
    RollingWindow w;
    assert(w.ingest(6000, Metrics{1.0, 0.0, 0.0, 0.0}));
    // Advance time far forward
    w.note_time(6100);
    // Window oldest is 6100-44=6056, so 6000 is too old
    assert(!w.ingest(6000, Metrics{999.0, 0.0, 0.0, 0.0}));
    std::printf("PASSED\n");
  }

  std::printf("All RollingWindow tests passed.\n");
  return 0;
}
