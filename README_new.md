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
```bash
# Clone and navigate to project
cd telemetry_agent

# Create build directory
mkdir build && cd build

# Configure with Ninja (recommended)
cmake -GNinja ..

# Or use default make
cmake ..

# Build
ninja    # or make

# Run tests
ninja test    # or make test
./test_rolling_window
```

### Build Options
- **Release build** (default): Optimized performance
- **Debug build**: `cmake -DCMAKE_BUILD_TYPE=Debug ..`
- **Compiler warnings**: Enabled by default

## Running Tests

The project includes comprehensive unit tests covering:
- Basic ingest and summary operations
- Partial window filling and averaging
- Ring buffer collision handling
- Out-of-order sample acceptance
- Too-old sample rejection
- Time advancement without samples

```bash
# Run all tests
./test_rolling_window

# Expected output:
Starting RollingWindow tests...
Test 1: Basic ingest and summary count... PASSED
Test 2: Fill partial window and check means... PASSED
Test 3: Overwrite via ring index collision... PASSED
Test 4: Correction for same timestamp replaces value... PASSED
Test 5: Out-of-order within window is accepted... PASSED
Test 6: Too-old sample rejected... PASSED
All RollingWindow tests passed.
```

## Implementation Details

### Circular Buffer Design
- **45 slots** indexed by `timestamp % 45`
- Each slot stores: `{ts, valid, metrics}`
- **Collision handling**: Same index can store different timestamps
- **Eviction**: Automatic when new samples overwrite old slots

### Time Handling
- **Unix timestamps** (seconds since epoch)
- **Late samples**: Accepted if within 45-second window
- **Future samples**: Handled gracefully
- **Time jumps**: Summary scans full window for correctness

### Performance Characteristics
- **Space**: O(1) - fixed 45-slot buffer
- **Time**: O(1) for ingest, O(1) for summary
- **Memory efficient**: No dynamic allocation during operation