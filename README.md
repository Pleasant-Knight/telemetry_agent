# Telemetry Agent - Rolling Window Implementation

A C++ implementation of a rolling window data structure for network telemetry metrics, following deep module design principles.

## Overview

This project implements a `RollingWindow` class that maintains a 45-second sliding window of network metrics using a time-indexed circular buffer. The design encapsulates complexity within the module while providing a clean, powerful API.

## Architecture

### RollingWindow Class

**Public API:**
- `bool ingest(int64_t ts, const Metrics& m)` - Add a sample at timestamp `ts`
- `void note_time(int64_t ts_now)` - Advance time without adding samples
- `Summary summary() const` - Get current window statistics
- Debug helpers: `bool has_sample(int64_t ts)`, `std::optional<Metrics> get(int64_t ts)`

**Data Structures:**
- `Metrics` - Network sample data: `{rtt_ms, throughput_mbps, loss_pct, jitter_ms}`
- `Summary` - Window statistics: `{newest_ts, oldest_ts, count, confidence, missing_rate, avg_rtt, avg_tp, avg_loss, avg_jit}`

### Key Features

- **45-second rolling window** with time-indexed circular buffer
- **Out-of-order sample handling** within the window
- **Automatic eviction** of old samples
- **Confidence metric** (`count/45`) indicating data completeness
- **O(1) ingest** and **O(1) summary** operations
- **Thread-safe design** (single-threaded usage assumed)

## Building the Project

### Prerequisites
- CMake 3.16+
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Ninja (recommended for faster builds)

### Build Steps
- counts of valid samples

### Ingest logic (O(1)):

```bash
idx = ts % 45

If slots[idx].valid and slots[idx].ts != ts, that means we’re overwriting an old second:

# subtract old metrics from aggregates to create eviction

If inserting a late sample into an existing slot with same ts, you’re replacing:

subtract old, add new

Write new slot + add to aggregates
```

### Compute window stats (O(1))
```bash
avg = sum / count
missing_rate = 1 - count/45

# This avoids any per-tick scan and naturally bounds memory.

# Late sample handling:
If ts < latest_ts - 44: discard + log (“outside window”)
Else insert into its slot; aggregates remain correct.

# Missing sample handling:
# A missing second just means no valid slot for that second. 
# Our summary uses count and can optionally penalize missingness.
