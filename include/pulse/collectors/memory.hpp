#pragma once

#include "pulse/models/metrics.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace pulse {

// Parse memory fields independently from the live /proc filesystem.
[[nodiscard]] std::optional<MemoryMetrics> parse_meminfo(std::string_view text);

// Read memory data from a configurable proc root for tests and Linux hosts.
class MemoryCollector {
  public:
    explicit MemoryCollector(std::filesystem::path proc_root = "/proc");

    [[nodiscard]] std::optional<MemoryMetrics> collect() const;

  private:
    std::filesystem::path proc_root_;
};

} // namespace pulse

