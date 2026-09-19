#pragma once

#include "pulse/models/metrics.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace pulse {

// Parse CPU counter lines independently from the live /proc filesystem.
[[nodiscard]] std::optional<CpuSample> parse_cpu_stat(std::string_view text);

// Convert cumulative CPU counters into percentages using adjacent samples.
[[nodiscard]] std::vector<CpuUsage> calculate_cpu_usage(const CpuSample& previous,
                                                        const CpuSample& current);

// Read CPU counters from a configurable proc root for tests and Linux hosts.
class CpuCollector {
  public:
    explicit CpuCollector(std::filesystem::path proc_root = "/proc");

    [[nodiscard]] std::optional<CpuSample> collect() const;

  private:
    std::filesystem::path proc_root_;
};

} // namespace pulse
