#pragma once

#include "pulse/models/metrics.hpp"

#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace pulse {

// Read useful mounted filesystems with statvfs instead of invoking df.
class FilesystemCollector {
  public:
    explicit FilesystemCollector(std::filesystem::path proc_root = "/proc");

    [[nodiscard]] std::vector<FilesystemUsage> collect() const;

  private:
    std::filesystem::path proc_root_;
};

// Read block device sector counters and calculate byte rates over time.
class DiskCollector {
  public:
    explicit DiskCollector(std::filesystem::path proc_root = "/proc",
                           std::filesystem::path sys_root = "/sys");

    [[nodiscard]] std::vector<DiskActivity> collect();

  private:
    std::filesystem::path proc_root_;
    std::filesystem::path sys_root_;
    std::unordered_map<std::string, DiskActivity> previous_;
    std::chrono::steady_clock::time_point previous_time_{};
};

} // namespace pulse

