#pragma once

#include "pulse/models/metrics.hpp"

#include <filesystem>
#include <optional>

namespace pulse {

// Collect stable host metadata and current load information from Linux.
class SystemCollector {
  public:
    explicit SystemCollector(std::filesystem::path proc_root = "/proc");

    [[nodiscard]] SystemInfo collect_info() const;
    [[nodiscard]] std::optional<SystemMetrics> collect_metrics() const;

  private:
    std::filesystem::path proc_root_;
};

} // namespace pulse
