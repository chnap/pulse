#pragma once

#include "pulse/collectors/cpu.hpp"
#include "pulse/collectors/memory.hpp"
#include "pulse/collectors/network.hpp"
#include "pulse/collectors/process.hpp"
#include "pulse/collectors/storage.hpp"
#include "pulse/collectors/system.hpp"
#include "pulse/collectors/temperature.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace pulse {

// Publish one internally consistent view of all metrics to UI readers.
struct MonitorSnapshot {
    std::uint64_t generation{};
    std::chrono::system_clock::time_point collected_at{};
    std::vector<CpuUsage> cpu;
    std::optional<MemoryMetrics> memory;
    std::optional<SystemMetrics> system;
    std::vector<ProcessInfo> processes;
    std::vector<NetworkInterface> network;
    std::vector<DiskActivity> disks;
    std::vector<FilesystemUsage> filesystems;
    std::vector<Temperature> temperatures;
};

// Run collectors off the UI thread and expose snapshots under one short lock.
class Monitor {
  public:
    explicit Monitor(std::chrono::milliseconds refresh_interval);
    ~Monitor();

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    void start();
    void stop();
    [[nodiscard]] MonitorSnapshot snapshot() const;
    [[nodiscard]] const SystemInfo& system_info() const noexcept;

  private:
    void run(const std::stop_token& stop_token);

    std::chrono::milliseconds refresh_interval_;
    CpuCollector cpu_collector_;
    MemoryCollector memory_collector_;
    SystemCollector system_collector_;
    ProcessCollector process_collector_;
    NetworkCollector network_collector_;
    DiskCollector disk_collector_;
    FilesystemCollector filesystem_collector_;
    TemperatureCollector temperature_collector_;
    SystemInfo system_info_;
    mutable std::mutex mutex_;
    std::condition_variable_any wakeup_;
    MonitorSnapshot snapshot_;
    std::jthread worker_;
};

} // namespace pulse
