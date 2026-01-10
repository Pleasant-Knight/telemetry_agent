#include <cassert>
#include <cmath>
#include <cstdio>

#include "rolling_window.hpp"

using namespace telemetry;

int main() {
  RollingWindow w;
  // Establish time at 0 without samples
  w.note_time(0);
  auto s0 = w.summary();
  assert(s0.newest_ts == 0);
  assert(s0.count == 0);
  assert(std::abs(s0.confidence - 0.0) < 1e-9);

  // Ingest 45 sequential samples with constant metrics
  Metrics m{100, 50, 2.0, 10};
  for (int t = 0; t < 45; ++t) {
    bool ok = w.ingest(t, m);
    assert(ok);
  }
  auto s1 = w.summary();
  assert(s1.count == 45);
  assert(s1.oldest_ts == 0);
  assert(s1.newest_ts == 44);
  assert(std::abs(s1.avg_rtt_ms - 100.0) < 1e-9);

  // Out-of-order sample inside window should be accepted
  Metrics m2{200, 0, 0, 0};
  bool ok2 = w.ingest(10, m2);
  assert(ok2);
  auto s2 = w.summary();
  // average changes slightly
  assert(s2.count == 45);
  assert(s2.avg_rtt_ms > 100.0);

  // Advance time: window slides, old samples become too old to accept
  w.note_time(60);
  auto s3 = w.summary();
  assert(s3.newest_ts == 60);
  assert(s3.oldest_ts == 16);

  bool reject = w.ingest(10, m);
  assert(!reject);

  std::printf("test_rolling_window OK\n");
  return 0;
}
