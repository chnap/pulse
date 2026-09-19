#pragma once

#include "pulse/models/metrics.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pulse {

// Hold the /proc/<pid>/stat fields required by the process monitor.
struct ProcessStat {
    Pid pid{};
    Pid parent_pid{};
    std::string name;
    char state{'?'};
    std::uint64_t user_ticks{};
    std::uint64_t system_ticks{};
    unsigned long threads{};
    std::uint64_t start_time_ticks{};
    std::int64_t resident_pages{};
};

// Parse the parenthesized comm field safely even when it contains spaces.
[[nodiscard]] std::optional<ProcessStat> parse_process_stat(std::string_view text);

// Discover processes and calculate CPU deltas while tolerating /proc races.
class ProcessCollector {
  public:
    explicit ProcessCollector(std::filesystem::path proc_root = "/proc");

    [[nodiscard]] std::vector<ProcessInfo> collect(double uptime_seconds,
                                                   std::uint64_t total_memory_bytes);

  private:
    struct PreviousProcess {
        std::uint64_t start_time_ticks{};
        std::uint64_t cpu_time_ticks{};
    };

    std::filesystem::path proc_root_;
    long clock_ticks_{};
    long page_size_{};
    std::uint64_t previous_total_cpu_ticks_{};
    std::unordered_map<Pid, PreviousProcess> previous_processes_;
};

} // namespace pulse
