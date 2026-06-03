#include "metrics/metrics.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace filegroup {

MetricsServer::MetricsServer(uint16_t port) : port_(port) {}
MetricsServer::~MetricsServer() { stop(); }

void MetricsServer::start() {
    running_ = true;
    server_thread_ = std::thread(&MetricsServer::serve_http, this);
}

void MetricsServer::stop() {
    running_ = false;
    if (server_thread_.joinable()) server_thread_.join();
}

void MetricsServer::set_gauge(const std::string& name, const std::string& help,
                               const std::unordered_map<std::string,std::string>& labels, double value) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto& vec = metrics_[name];
    Metric m; m.help = help; m.type = "gauge"; m.value = value; m.labels = labels;
    // Replace existing with same labels or append
    bool found = false;
    for (auto& existing : vec) {
        if (existing.labels == labels) { existing.value = value; found = true; break; }
    }
    if (!found) vec.push_back(m);
}

void MetricsServer::set_upload_sessions_active(uint32_t count) {
    set_gauge("file_upload_sessions_active", "Active upload sessions", {}, count);
}

void MetricsServer::set_node_health(uint16_t node_id, const std::string& state) {
    set_gauge("file_node_health_state", "Storage node health state",
              {{"node_id", std::to_string(node_id)}}, state == "HEALTHY" ? 0 : (state == "SUSPECT" ? 1 : 2));
}

void MetricsServer::set_disk_used_bytes(uint16_t node_id, uint64_t bytes) {
    set_gauge("file_disk_used_bytes", "Disk used bytes per node",
              {{"node_id", std::to_string(node_id)}}, static_cast<double>(bytes));
}

void MetricsServer::inc_counter(const std::string& name, double value) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto& vec = metrics_[name];
    if (vec.empty()) {
        Metric m; m.type = "counter"; m.value = 0; vec.push_back(m);
    }
    vec[0].value += value;
}

void MetricsServer::observe_histogram(const std::string& name, double value) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto& vec = metrics_[name];
    Metric m; m.type = "histogram"; m.value = value;
    vec.push_back(m);
}

void MetricsServer::observe_chunk_write_latency_ms(double ms) {
    observe_histogram("file_chunk_write_latency_ms", ms);
}

void MetricsServer::observe_chunk_read_latency_ms(double ms) {
    observe_histogram("file_chunk_read_latency_ms", ms);
}

std::string MetricsServer::render_metrics() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::ostringstream ss;

    for (const auto& [name, vec] : metrics_) {
        for (const auto& m : vec) {
            if (!m.help.empty()) ss << "# HELP " << name << " " << m.help << "\n";
            if (!m.type.empty()) ss << "# TYPE " << name << " " << m.type << "\n";

            ss << name;
            if (!m.labels.empty()) {
                ss << "{";
                bool first = true;
                for (const auto& [k, v] : m.labels) {
                    if (!first) ss << ",";
                    ss << k << "=\"" << v << "\"";
                    first = false;
                }
                ss << "}";
            }
            ss << " " << m.value << "\n";
        }
    }
    return ss.str();
}

void MetricsServer::serve_http() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { std::cerr << "[metrics] socket failed\n"; return; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        std::cerr << "[metrics] bind failed on port " << port_ << "\n";
        return;
    }

    listen(server_fd, 5);
    std::cout << "[metrics] listening on :" << port_ << "\n";

    while (running_) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int client_fd = accept(server_fd, (struct sockaddr*)&client, &client_len);
        if (client_fd < 0) continue;

        char buf[4096];
        ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            std::string body = render_metrics();
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/plain\r\n"
                     << "Content-Length: " << body.size() << "\r\n"
                     << "Connection: close\r\n"
                     << "\r\n"
                     << body;
            std::string resp = response.str();
            send(client_fd, resp.c_str(), resp.size(), 0);
        }
        close(client_fd);
    }
    close(server_fd);
}

}  // namespace filegroup
