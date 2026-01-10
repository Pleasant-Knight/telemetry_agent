// telemetry_agent.cpp
#include <iostream>
#include <vector>
#include <deque>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <random>  // For simulating imperfections, but deterministic here
#include <cassert>

struct Measurement {
    int timestamp;
    double rtt;        // ms
    double throughput; // Mbps
    double loss;       // %
    double jitter;     // ms
};

enum class Status { Healthy, Degraded, Down };

std::string statusToString(Status s) {
    switch (s) {
        case Status::Healthy: return "Healthy";
        case Status::Degraded: return "Degraded";
        case Status::Down: return "Down";
    }
    return "Unknown";
}

class RollingWindow {
private:
    static constexpr int WINDOW_SEC = 45;
    std::deque<Measurement> data;  // Kept sorted by timestamp
    double sum_rtt = 0.0;
    double sum_throughput = 0.0;
    double sum_loss = 0.0;
    double sum_jitter = 0.0;
    size_t count = 0;

    void addToSums(const Measurement& m) {
        sum_rtt += m.rtt;
        sum_throughput += m.throughput;
        sum_loss += m.loss;
        sum_jitter += m.jitter;
        ++count;
    }

    void removeFromSums(const Measurement& m) {
        sum_rtt -= m.rtt;
        sum_throughput -= m.throughput;
        sum_loss -= m.loss;
        sum_jitter -= m.jitter;
        --count;
    }

public:
    void add(const Measurement& m, int current_time) {
        const int oldest_allowed = current_time - (WINDOW_SEC - 1);
        if (m.timestamp < oldest_allowed) {
            std::cerr << "Discarding old sample at t=" << m.timestamp << std::endl;
            return;
        }
        // Find insert position (binary search)
        auto it = std::lower_bound(data.begin(), data.end(), m,
                                   [](const Measurement& a, const Measurement& b) {
                                       return a.timestamp < b.timestamp;
                                   });
        data.insert(it, m);
        addToSums(m);
        // Evict old
        while (!data.empty() && data.front().timestamp < oldest_allowed) {
            removeFromSums(data.front());
            data.pop_front();
        }
    }

    double getAvgRTT() const { return count > 0 ? sum_rtt / count : 0.0; }
    double getAvgThroughput() const { return count > 0 ? sum_throughput / count : 0.0; }
    double getAvgLoss() const { return count > 0 ? sum_loss / count : 0.0; }
    double getAvgJitter() const { return count > 0 ? sum_jitter / count : 0.0; }
    size_t size() const { return count; }
};

struct Scorer {
    static double normalizeThroughput(double val) { return std::min(1.0, val / 200.0); }
    static double normalizeRTT(double val) { return std::max(0.0, 1.0 - (val - 10.0) / 790.0); }
    static double normalizeLoss(double val) { return std::max(0.0, 1.0 - val / 30.0); }
    static double normalizeJitter(double val) { return std::max(0.0, 1.0 - val / 200.0); }

    // Strategy 1: Weighted sum on averages
    static double computeScore(const RollingWindow& window) {
        double n_tp = normalizeThroughput(window.getAvgThroughput());
        double n_rtt = normalizeRTT(window.getAvgRTT());
        double n_loss = normalizeLoss(window.getAvgLoss());
        double n_jit = normalizeJitter(window.getAvgJitter());
        return 0.3 * n_tp + 0.3 * n_rtt + 0.2 * n_loss + 0.2 * n_jit;
    }

    // Strategy 2 (Bonus): EWMA on metrics + trend penalty
    static double computeScoreEWMA(const RollingWindow& window, double prev_ewma_score) {
        double alpha = 0.2;
        double current_score = computeScore(window);
        double ewma = alpha * current_score + (1 - alpha) * prev_ewma_score;
        // Simple trend: if current < prev, penalty
        double penalty = (current_score < prev_ewma_score) ? -0.1 : 0.0;
        return std::clamp(ewma + penalty, 0.0, 1.0);
    }
};

class HysteresisStatus {
private:
    static constexpr int CONSECUTIVE_REQ = 5;
    static constexpr double THRESH_HEALTHY = 0.8;
    static constexpr double THRESH_DEGRADED = 0.4;

    Status current = Status::Healthy;
    int consec_below_healthy = 0;
    int consec_below_degraded = 0;
    int consec_above_degraded = 0;
    int consec_above_healthy = 0;

public:
    std::pair<Status, bool> update(double score) {
        bool changed = false;
        switch (current) {
            case Status::Healthy:
                if (score < THRESH_HEALTHY) {
                    ++consec_below_healthy;
                    if (consec_below_healthy >= CONSECUTIVE_REQ) {
                        current = Status::Degraded;
                        changed = true;
                    }
                } else {
                    consec_below_healthy = 0;
                }
                break;
            case Status::Degraded:
                if (score < THRESH_DEGRADED) {
                    ++consec_below_degraded;
                    if (consec_below_degraded >= CONSECUTIVE_REQ) {
                        current = Status::Down;
                        changed = true;
                    }
                } else if (score > THRESH_HEALTHY) {
                    ++consec_above_healthy;
                    if (consec_above_healthy >= CONSECUTIVE_REQ) {
                        current = Status::Healthy;
                        changed = true;
                    }
                } else {
                    consec_below_degraded = 0;
                    consec_above_healthy = 0;
                }
                break;
            case Status::Down:
                if (score > THRESH_DEGRADED) {
                    ++consec_above_degraded;
                    if (consec_above_degraded >= CONSECUTIVE_REQ) {
                        current = Status::Degraded;
                        changed = true;
                    }
                } else {
                    consec_above_degraded = 0;
                }
                break;
        }
        return {current, changed};
    }

    Status getStatus() const { return current; }
};

class InterfaceState {
private:
    RollingWindow window;
    HysteresisStatus status_mgr;
    double last_score = 0.0;
    double ewma_score = 0.0;  // For Strategy 2
    double sum_scores = 0.0;  // For end summary
    int score_count = 0;

public:
    void addMeasurement(const Measurement& m, int current_time) {
        window.add(m, current_time);
    }

    void computeScore(bool use_ewma = false) {
        if (use_ewma) {
            ewma_score = Scorer::computeScoreEWMA(window, ewma_score);
            last_score = ewma_score;
        } else {
            last_score = Scorer::computeScore(window);
        }
        sum_scores += last_score;
        ++score_count;
    }

    std::pair<double, Status> getLatest() {
        auto [status, changed] = status_mgr.update(last_score);
        return {last_score, status};
    }

    double getAvgScore() const { return score_count > 0 ? sum_scores / score_count : 0.0; }
};

class TelemetryAgent {
private:
    std::map<std::string, InterfaceState> interfaces = {
        {"eth0", {}}, {"wifi0", {}}, {"lte0", {}}, {"sat0", {}}
    };
    std::map<std::string, Status> last_statuses;
    bool use_ewma = false;  // Toggle for comparison

public:
    TelemetryAgent(bool ewma = false) : use_ewma(ewma) {}

    void processMeasurement(const std::string& iface, const Measurement& m, int current_time) {
        if (interfaces.count(iface)) {
            interfaces[iface].addMeasurement(m, current_time);
        }
    }

    void tick(int current_time) {
        for (auto& [iface, state] : interfaces) {
            state.computeScore(use_ewma);
            auto [score, status] = state.getLatest();
            std::printf("t=%d %s: score=%.2f status=%s\n",
                        current_time,
                        iface.c_str(),
                        score,
                        statusToString(status).c_str());

            if (last_statuses.count(iface) && last_statuses[iface] != status) {
                std::printf("Transition: %s from %s to %s (score=%.2f)\n",
                            iface.c_str(),
                            statusToString(last_statuses[iface]).c_str(),
                            statusToString(status).c_str(),
                            score);
            }
            last_statuses[iface] = status;
        }
    }

    void printSummary() {
        std::vector<std::pair<std::string, double>> rankings;
        for (const auto& [iface, state] : interfaces) {
            rankings.emplace_back(iface, state.getAvgScore());
        }
        std::sort(rankings.begin(), rankings.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        std::printf("End-of-run summary (ranked by avg score):\n");
        for (const auto& [iface, avg] : rankings) {
            std::printf("%s: %.2f\n", iface.c_str(), avg);
        }
    }
};

namespace Simulator {
    // Deterministic generation; hardcode qualitative behaviors
    // For imperfections: hardcoded missing (e.g., skip t=10,20) and late (add at t+2)
    std::map<std::string, std::vector<Measurement>> generateScenarioA(int duration) {
        std::map<std::string, std::vector<Measurement>> seq;
        for (int t = 0; t < duration; ++t) {
            // eth0: stable good
            seq["eth0"].push_back({t, 20.0, 100.0, 0.0, 5.0});
            // wifi0: degrade over 40s (rtt up, tp down), recover
            double deg_factor = (t < 40) ? t / 40.0 : (t < 80 ? (80 - t) / 40.0 : 0.0);
            seq["wifi0"].push_back({t, 20.0 + 300.0 * deg_factor, 100.0 - 80.0 * deg_factor, 0.0 + 10.0 * deg_factor, 5.0 + 50.0 * deg_factor});
            // lte0: moderate stable
            seq["lte0"].push_back({t, 50.0, 50.0, 2.0, 10.0});
            // sat0: high latency stable
            seq["sat0"].push_back({t, 500.0, 20.0, 1.0, 20.0});
        }
        // Imperfections: miss some, late some (deterministic)
        seq["wifi0"].erase(seq["wifi0"].begin() + 10);  // miss t=10
        // Late: move t=15 to later (will add at t=17)
        Measurement late = seq["wifi0"][15];
        seq["wifi0"].erase(seq["wifi0"].begin() + 15);
        seq["wifi0"].insert(seq["wifi0"].begin() + 17, late);  // But timestamp still 15
        return seq;
    }

    std::map<std::string, std::vector<Measurement>> generateScenarioB(int duration) {
        std::map<std::string, std::vector<Measurement>> seq;
        for (int t = 0; t < duration; ++t) {
            // eth0: stable
            seq["eth0"].push_back({t, 20.0, 100.0, 0.0, 5.0});
            // wifi0: spikes every 15s, 3-5s long
            bool spike = (t % 15 < 5 && t % 15 > 1);
            seq["wifi0"].push_back({t, spike ? 200.0 : 30.0, spike ? 20.0 : 80.0, spike ? 15.0 : 1.0, spike ? 100.0 : 10.0});
            // lte0: mild noise
            seq["lte0"].push_back({t, 50.0 + (t % 10), 50.0 - (t % 5), 2.0 + (t % 3), 10.0});
            // sat0: stable high RTT
            seq["sat0"].push_back({t, 500.0, 20.0, 1.0, 20.0});
        }
        // Imperfections similar
        seq["lte0"].erase(seq["lte0"].begin() + 20);
        Measurement late = seq["wifi0"][30];
        seq["wifi0"].erase(seq["wifi0"].begin() + 30);
        seq["wifi0"].insert(seq["wifi0"].begin() + 33, late);
        return seq;
    }

    std::map<std::string, std::vector<Measurement>> generateScenarioC(int duration) {
        std::map<std::string, std::vector<Measurement>> seq;
        for (int t = 0; t < duration; ++t) {
            // eth0: strong
            seq["eth0"].push_back({t, 20.0, 100.0, 0.0, 5.0});
            // wifi0: low tp but low loss/jitter
            seq["wifi0"].push_back({t, 30.0, 30.0, 0.5, 5.0});
            // lte0: high tp but high loss/jitter
            seq["lte0"].push_back({t, 50.0, 150.0, 10.0, 100.0});
            // sat0: mod tp, high RTT, low loss
            seq["sat0"].push_back({t, 600.0, 50.0, 0.5, 10.0});
        }
        // Imperfections
        seq["sat0"].erase(seq["sat0"].begin() + 40);
        Measurement late = seq["lte0"][50];
        seq["lte0"].erase(seq["lte0"].begin() + 50);
        seq["lte0"].insert(seq["lte0"].begin() + 52, late);
        return seq;
    }

    auto getScenario(char id) {
        const int DURATION = 90;
        if (id == 'A') return generateScenarioA(DURATION);
        if (id == 'B') return generateScenarioB(DURATION);
        if (id == 'C') return generateScenarioC(DURATION);
        throw std::invalid_argument("Invalid scenario");
    }
};

// Simple tests
void testHysteresis() {
    HysteresisStatus hs;
    // Noisy: alternate 0.9 and 0.7, should not flap
    for (int i = 0; i < 10; ++i) {
        auto [s, c] = hs.update((i % 2 == 0) ? 0.9 : 0.7);
        assert(!c && s == Status::Healthy);  // Requires 5 consec
    }
    // 5 low: flap
    for (int i = 0; i < 5; ++i) hs.update(0.7);
    assert(hs.getStatus() == Status::Degraded);
    std::printf("Hysteresis test passed\n");
}

void testWindowBounded() {
    RollingWindow w;
    for (int t = 0; t < 100; ++t) {
        w.add({t, 1.0, 1.0, 1.0, 1.0}, t);
        assert(w.size() <= 45);
    }
    std::printf("Window bounded test passed\n");
}

void testLateSample() {
    RollingWindow w;
    w.add({10, 10.0, 10.0, 10.0, 10.0}, 50);  // Within window
    w.add({6, 6.0, 6.0, 6.0, 6.0}, 50);      // Late but within window
    assert(w.getAvgRTT() == 8.0);             // Average of 6 and 10
    w.add({0, 0.0, 0.0, 0.0, 0.0}, 50);      // Too old, discard
    assert(w.getAvgRTT() == 8.0);
    std::printf("Late sample test passed\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "run" || std::string(argv[2]) != "--scenario" || argc != 4) {
        std::cerr << "Usage: telemetry_agent run --scenario A|B|C" << std::endl;
        return 1;
    }
    char scenario = argv[3][0];

    // Run tests
    testHysteresis();
    testWindowBounded();
    testLateSample();

    // Simulate
    auto sequences = Simulator::getScenario(scenario);
    TelemetryAgent agent(false);  // Strategy 1
    // Bonus comparison: Run Strategy 2 separately if desired
    // TelemetryAgent agent_ewma(true);

    size_t current_time = 0;
    while (current_time < 90) {
        for (const auto& [iface, meas_vec] : sequences) {
            // Feed if sample at this time (vectors are indexed by t, but with erasures for missing)
            if (current_time < meas_vec.size()) {
                agent.processMeasurement(iface, meas_vec[current_time], current_time);
            }
        }
        agent.tick(current_time);
        ++current_time;
    }
    agent.printSummary();

    // For comparison, briefly: Strategy 2 would be smoother but lagged; test by toggling use_ewma.
    return 0;
}
