# Telemetry Agent - Rolling Window Implementation

A C++ implementation of a rolling window data structure for network telemetry metrics, following deep module design principles.

## Overview

This project implements a `RollingWindow` class that maintains a 45-second sliding window of network metrics using a time-indexed circular buffer. The design encapsulates complexity within the module while providing a clean, powerful API. 

**Expanded scope:** the rolling window is the foundation for a multi-interface telemetry agent that:

* ingests per-interface measurements continuously.
* computes a normalized health score.
* assigns stable statuses (healthy / degraded / down) without flapping.
* exposes latest per-interface state via CLI (and can be extended to IPC/HTTP).

---

## Architecture

### Deep Modules (Ousterhout-style)

John Ousterhout’s “deep module” idea: *hide complexity behind a small, powerful interface*. 
In this repo, the complexity is pushed down into a few modules that each do one hard thing well.

### Requirements → Modules Mapping

| Requirement                                          | Module(s)                                                     | Notes                                                                                                                      |
| ---------------------------------------------------- | ------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| 1) Ingest per-interface measurements continuously    | `TelemetryAgent`, `InterfaceTracker`, `RollingWindow45s`      | `TelemetryAgent` routes samples to the correct tracker; `RollingWindow45s` accepts out-of-order samples within the window. |
| 2) Maintain a rolling 45-second window per interface | `RollingWindow45s`                                            | Fixed 45-slot time-indexed ring buffer with bounded memory.                                                                |
| 3) Compute a normalized health score per interface   | `ScoreModel` (in `InterfaceTracker`)                          | Normalizes throughput/RTT/loss/jitter to [0..1], combines with weights, clamps, optional confidence cap.                   |
| 4) Assign stable interface statuses without flapping | `HysteresisFsm` (+ EWMA in `InterfaceTracker`)                | EWMA reduces noise; hysteresis + consecutive evidence prevents chatter; optional dwell time limits rapid toggles.          |
| 5) Expose latest state to services/operators         | CLI runner (`telemetry_agent`), `TelemetryAgent::snapshots()` | CLI prints table per tick + transitions + summary ranking; can be extended to JSON/HTTP later.                             |
| Bonus) Missing or out-of-order samples               | `RollingWindow45s`, optional scenario “imperfect data”        | Window accepts late samples inside 45s; confidence (`count/45`) informs gating.                                            |

---

## Module Breakdown

### RollingWindow45s Class (per-interface rolling window)

**Public API:**

* `bool ingest(int64_t ts, const Metrics& m)` - Add a sample at timestamp `ts`.
* `void note_time(int64_t ts_now)` - Advance time without adding samples.
* `Summary summary() const` - Get current window statistics.
* Debug helpers: `bool has_sample(int64_t ts)`, `std::optional<Metrics> get(int64_t ts)`.

**Data Structures:**

* `Metrics` - Network sample data: `{rtt_ms, throughput_mbps, loss_pct, jitter_ms}`.
* `Summary` - Window statistics: `{newest_ts, oldest_ts, count, confidence, missing_rate, avg_rtt, avg_tp, avg_loss, avg_jit}`.

**Key Features**

* **45-second rolling window** with time-indexed circular buffer.
* **Out-of-order sample handling** within the window.
* **Automatic eviction** of old samples.
* **Confidence metric** (`count/45`) indicating data completeness.
* **O(1) ingest** and **O(1) summary** operations (fixed 45-slot scan).
* **Thread-safe design** (single-threaded usage assumed) .

> Note on “avoid rescanning”: summary scans a fixed 45-slot buffer (constant time, tiny constant). This keeps correctness simple under missing/out-of-order/time jumps. If we later increase sampling rate/window size, this can be upgraded to running aggregates with explicit eviction.

---

### InterfaceTracker (deep module per interface)

**Responsibility:** turn samples into a stable per-interface state.
Pipeline:

1. Ingest sample into `RollingWindow45s`
2. Compute summary statistics over the window
3. Normalize + combine metrics into `score_raw ∈ [0..1]`
4. Smooth with EWMA → `score_smoothed`
5. Convert score into stable status using `HysteresisFsm`
6. Expose `InterfaceSnapshot` to callers

Outputs:

* `InterfaceSnapshot` {score, status, confidence, means}
* `TransitionEvent` (edge-triggered) with a reason string

---

### HysteresisFsm (anti-flapping finite state machine)

Converts a noisy score stream into stable states using:

* **Hysteresis thresholds** (enter threshold differs from exit threshold)
* **Consecutive evidence** (must be above/below threshold for N ticks)
* Optional **minimum dwell** time between transitions

Tiny ASCII FSM diagram:

```
          score > healthy_enter for N ticks
   +--------------------------------------------+
   |                                            |
   v                                            |
[DEGRADED] ---- score < down_enter for M ticks -> [DOWN]
   ^   |                                            |
   |   | score < healthy_exit for K ticks           |
   |   +---------------------------+                |
   |                               |                |
   +--- score > down_exit for P ticks --------------+
                 (recovery)
                 DOWN -> DEGRADED
```

Typical intent:

* **Fast drop** when truly bad (safety)
* **Slow promotion** to Healthy (stability)

---

### TelemetryAgent (multi-interface manager)

Owns `std::unordered_map<std::string, InterfaceTracker>` and provides:

* `ingest(iface, ts, metrics)`
* `note_time(ts_now)` (expire time even if samples missing)
* `snapshots()` (for CLI / service integration)
* transition drain stream (for logging/operator visibility)
* end-of-run ranking by average score (Scenario C evaluation)

---

## Default Scoring & FSM Parameters (Recommended)

These are *recommended starting defaults* that align with scenario intent and avoid common pitfalls (Scenario B flapping; Scenario A recovery).

### Score normalization (ranges)

* throughput: 0..200 Mbps (higher is better)
* RTT: 10..800 ms (lower is better)
* loss: 0..30% (lower is better)
* jitter: 0..200 ms (lower is better)

### Score weights (sum to 1.0)

Recommended:

* `w_loss = 0.30`
* `w_rtt  = 0.25`
* `w_tp   = 0.25`
* `w_jit  = 0.20`

Rationale:

* Throughput alone can mislead (Scenario C), so quality (loss/jitter/RTT) should dominate.

### EWMA

* `ewma_alpha = 0.20–0.25` (1 Hz sampling)

Lower alpha = smoother (less reactive to spikes), higher alpha = more responsive.

### Hysteresis thresholds (tuned to this score scale)

Based on observed “good wifi” scores in scenarios, a practical default is:

* `healthy_enter = 0.72`
* `healthy_exit  = 0.66`
* `down_enter    = 0.35`
* `down_exit     = 0.45`

### Evidence + dwell

* `healthy_enter_N = 6–8` (conservative promotion)
* `healthy_exit_N  = 5–6` (avoid reacting to 3–5s spikes)
* `down_enter_N    = 3` (drop quickly if truly bad)
* `down_exit_N     = 5` (don’t pop up/down too fast)
* `min_dwell_sec   = 5–10`

### Confidence gating

* `min_confidence_for_promotion = 0.60`
* Optional confidence cap: if confidence < 0.60, cap score at ~0.70 to avoid declaring Healthy on sparse data.

---

## Scenarios (A/B/C/D) and Expected Behavior

The CLI runner supports deterministic scenarios (90 seconds, 1 Hz, 4 interfaces: `eth0`, `wifi0`, `lte0`, `sat0`):

* **Scenario A:** `wifi0` gradually degrades then recovers; status should change with hysteresis and not instantly.
* **Scenario B:** `wifi0` experiences 3–5s spikes every ~15s; status should *not* flap repeatedly.
* **Scenario C:** `lte0` has high throughput but sustained loss/jitter; scoring should rank a cleaner `wifi0` above it.
* **Scenario D:** `wifi0` drops ~5% of samples and `sat0` receives ~2% late samples; late samples inside the 45-second window are accepted, too-old samples are discarded with a log.

At end of run, the CLI prints a ranking by average score.

---

## CLI Output (Operator View)

The CLI prints a table per tick with per-interface:

* status
* smoothed score
* confidence
* window means (tp/rtt/loss/jitter)
  and prints transition events (with reasons) when they occur.

---

## Building the Project

### Prerequisites

* CMake 3.16+
* C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
* Ninja (recommended for faster builds)

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

### Run the scenario CLI

```bash
# From build/
./telemetry_agent_cli --scenario A
./telemetry_agent_cli --scenario B
./telemetry_agent_cli --scenario C
./telemetry_agent_cli --scenario D
```

### Benchmarks

```bash
# From build/
cmake --build . --target benchmark_scenarios
./benchmark_scenarios --scenario A
./benchmark_scenarios --scenario D --seconds 300 --runs 3
./benchmark_scenarios --scenario B --missing --late
```

### Build Options

* **Release build** (default): Optimized performance
* **Debug build**: `cmake -DCMAKE_BUILD_TYPE=Debug ..`
* **Compiler warnings**: Enabled by default 

---

## Running Tests

### RollingWindow tests

The project includes unit tests covering:

* Basic ingest and summary operations
* Partial window filling and averaging
* Ring buffer collision handling
* Out-of-order sample acceptance
* Too-old sample rejection
* Time advancement without samples 

```bash
./test_rolling_window
```

### Scenario tests (agent-level)

Additional tests validate scenario expectations:

* **Scenario B no-flap:** ensure `wifi0` doesn’t toggle repeatedly under periodic spikes.
* **Scenario A degrade/recover:** ensure `wifi0` eventually recovers (with thresholds aligned to score scale).

> Testing note: transitions must be edge-triggered (use drain semantics) so tests count true transitions, not repeated “last transition” reporting.

---

## Implementation Details

### Circular Buffer Design

* **45 slots** indexed by `timestamp % 45`
* Each slot stores: `{ts, valid, metrics}`
* **Collision handling**: Same index can store different timestamps
* **Eviction**: Automatic when new samples overwrite old slots 

### Time Handling

* **Unix timestamps** (seconds since epoch)
* **Late samples**: Accepted if within 45-second window
* **Future samples**: Handled gracefully
* **Time jumps**: Summary scans full window for correctness 

### Performance Characteristics

* **Space**: O(1) - fixed 45-slot buffer
* **Time**: O(1) ingest; O(1) summary (fixed 45-slot scan)
* **Memory efficient**: No dynamic allocation during operation 

---

## Failure Modes & Agent Behavior

The telemetry agent was designed to behave predictably under imperfect or adversarial conditions commonly found in embedded networking environments.

### Missing samples
- **Cause:** temporary sensor failure, interface sleep, CPU pressure
- **Behavior:** missing samples simply reduce `confidence = count/45`
- **Effect:** promotion to `Healthy` is gated by confidence; the agent avoids declaring healthy on sparse data

### Late or out-of-order samples
- **Cause:** scheduling jitter, batching, delayed drivers
- **Behavior:** samples are accepted if their timestamp falls within the 45-second window
- **Effect:** aggregates remain correct; samples outside the window are safely ignored

### Noisy or bursty measurements
- **Cause:** Wi-Fi interference, transient congestion
- **Behavior:** EWMA smoothing + consecutive evidence requirements
- **Effect:** short spikes (3–5s) do not cause repeated status toggles (Scenario B)

### Sustained degradation
- **Cause:** link quality collapse, congestion, RF loss
- **Behavior:** FSM transitions after sufficient evidence
- **Effect:** status changes are delayed but deterministic and explainable

### Time jumps or clock irregularities
- **Cause:** NTP correction, suspend/resume
- **Behavior:** window summary filters samples by timestamp range
- **Effect:** stale samples are excluded automatically

### Complete data loss
- **Cause:** interface unplugged or driver crash
- **Behavior:** confidence drops toward zero; FSM will converge to `Degraded` or `Down`
- **Effect:** avoids false healthy state during blind operation

---

## Production Considerations (Hoplynk Device)

This implementation focuses on correctness, determinism, and clarity. For a production Hoplynk router, the following enhancements would be added:

### Watchdogs & liveness
- Monitor agent heartbeat.
- Restart on deadlock or prolonged lack of updates.
- Optional hardware watchdog integration.

### Persistence
- Persist last known interface state across restarts.
- Store recent score/history to avoid cold-start misclassification.

### Backpressure & rate limiting
- Bound ingestion rate per interface.
- Drop or coalesce samples under load.
- Protect against misbehaving drivers.

### Concurrency model
- Separate threads for:
  - metric collection.
  - scoring/state updates.
  - publishing/export.
- Use lock-free queues or single-writer model per interface.

### IPC / export
- Replace CLI-only output with:
  - Unix domain socket or gRPC.
  - structured JSON/Protobuf.
- Allow external agents to subscribe to state changes.

### Configuration & tuning
- Load thresholds/weights from config file.
- Support per-interface tuning (e.g., satellite vs Wi-Fi).

### Security & isolation
- Run with least privileges.
- Validate input ranges defensively.
- Harden against malformed samples.

### Observability
- Export internal metrics (FSM counters, confidence).
- Log transition reasons for post-mortem analysis.

---

## About

A network router telemetry agent for multiple interfaces. 

---

[1]: https://github.com/Pleasant-Knight/telemetry_agent "GitHub - Pleasant-Knight/telemetry_agent: A network router telemetry agent for multiple interfaces"
