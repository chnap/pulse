#pragma once

#include "pulse/models/metrics.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pulse {

// Parse absolute interface counters independently from rate calculation.
[[nodiscard]] std::optional<std::vector<NetworkInterface>> parse_net_dev(std::string_view text);

// Convert successive /proc/net/dev samples into per-interface transfer rates.
class NetworkCollector {
  public:
    explicit NetworkCollector(std::filesystem::path proc_root = "/proc");

    [[nodiscard]] std::vector<NetworkInterface> collect();

  private:
    std::filesystem::path proc_root_;
    std::unordered_map<std::string, NetworkInterface> previous_;
    std::chrono::steady_clock::time_point previous_time_{};
};

} // namespace pulse

