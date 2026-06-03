#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace filegroup {

// ============================================================================
// Prometheus metrics endpoint (Phase 1: simple HTTP, Phase 2: prometheus-cpp)
// Serves /metrics on configurable port (default 9090).
// ============================================================================

class MetricsServer {
public:
    MetricsServer(uint16_t port = 9090);
    ~MetricsServer();

    void start();
    void stop();

    // ---- Gauges ----
    void set_gauge(const std::string& name, const std::string& help,
                   const std::unordered_map<std::string,std::string>& labels, double value);
    void set_upload_sessions_active(uint32_t count);
    void set_node_health(uint16_t node_id, const std::string& state);
    void set_disk_used_bytes(uint16_t node_id, uint64_t bytes);

    // ---- Counters ----
    void inc_counter(const std::string& name, double value = 1.0);
    void inc_chunks_confirmed() { inc_counter("file_upload_chunks_confirmed_total"); }
    void inc_read_failover() { inc_counter("file_read_failover_total"); }

    // ---- Histograms ----
    void observe_histogram(const std::string& name, double value);
    void observe_chunk_write_latency_ms(double ms);
    void observe_chunk_read_latency_ms(double ms);

    // ---- Simple HTTP server (embedded) ----
    void serve_http();

private:
    std::string render_metrics() const;

    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;

    struct Metric {
        std::string help;
        std::string type; // gauge, counter, histogram
        double value = 0;
        std::unordered_map<std::string,std::string> labels;
    };
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<Metric>> metrics_;
};

}  // namespace filegroup
