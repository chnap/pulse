#pragma once

#include "pulse/models/metrics.hpp"

#include <filesystem>
#include <vector>

namespace pulse {

// Discover temperature sensors dynamically across thermal and hwmon sysfs trees.
class TemperatureCollector {
  public:
    explicit TemperatureCollector(std::filesystem::path sys_root = "/sys");

    [[nodiscard]] std::vector<Temperature> collect() const;

  private:
    std::filesystem::path sys_root_;
};

} // namespace pulse

