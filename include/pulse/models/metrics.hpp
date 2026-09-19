#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pulse {

// Identify a Linux process with the signed type used by system calls.
using Pid = int;

// Store the cumulative counters exposed for one line in /proc/stat.
struct CpuTimes {
    std::string name;
    std::uint64_t user{};
    std::uint64_t nice{};
    std::uint64_t system{};
    std::uint64_t idle{};
    std::uint64_t iowait{};
    std::uint64_t irq{};
    std::uint64_t softirq{};
    std::uint64_t steal{};

    [[nodiscard]] std::uint64_t idle_all() const;
    [[nodiscard]] std::uint64_t non_idle() const;
    [[nodiscard]] std::uint64_t total() const;
};

// Describe utilization calculated from two consecutive CPU counter samples.
struct CpuUsage {
    std::string name;
    double total_percent{};
    double user_percent{};
    double system_percent{};
    double idle_percent{};
    double iowait_percent{};
};

// Group the aggregate CPU and every logical core from a single read.
struct CpuSample {
    std::vector<CpuTimes> counters;
};

// Represent Linux memory values after converting /proc/meminfo KiB to bytes.
struct MemoryMetrics {
    std::uint64_t total_bytes{};
    std::uint64_t available_bytes{};
    std::uint64_t used_bytes{};
    std::uint64_t cached_bytes{};
    std::uint64_t swap_total_bytes{};
    std::uint64_t swap_used_bytes{};
};

// Hold identity and slowly changing system metadata.
struct SystemInfo {
    std::string hostname;
    std::string kernel;
    std::string operating_system;
    std::string cpu_model;
    unsigned int logical_cpus{};
};

// Store fast-changing load and uptime values.
struct SystemMetrics {
    double uptime_seconds{};
    double load_one{};
    double load_five{};
    double load_fifteen{};
};

} // namespace pulse

