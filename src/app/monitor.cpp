#include "pulse/app/monitor.hpp"

#include <algorithm>
#include <utility>

namespace pulse {

Monitor::Monitor(std::chrono::milliseconds refresh_interval)
    : refresh_interval_(refresh_interval), system_info_(system_collector_.collect_info()) {}

Monitor::~Monitor() { stop(); }

void Monitor::start() {
    if (!worker_.joinable()) {
        worker_ = std::jthread([this](const std::stop_token& token) { run(token); });
    }
}

void Monitor::stop() {
    if (worker_.joinable()) {
        worker_.request_stop();
        wakeup_.notify_all();
        worker_.join();
    }
}

MonitorSnapshot Monitor::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

const SystemInfo& Monitor::system_info() const noexcept { return system_info_; }

void Monitor::run(const std::stop_token& stop_token) {
    std::optional<CpuSample> previous_cpu;
    std::uint64_t cycle = 0;

    while (!stop_token.stop_requested()) {
        const auto cycle_start = std::chrono::steady_clock::now();
        MonitorSnapshot next;
        next.collected_at = std::chrono::system_clock::now();
        next.system = system_collector_.collect_metrics();
        next.memory = memory_collector_.collect();

        const auto current_cpu = cpu_collector_.collect();
        if (previous_cpu && current_cpu) {
            next.cpu = calculate_cpu_usage(*previous_cpu, *current_cpu);
        }
        if (current_cpu) {
            previous_cpu = current_cpu;
        }

        const auto uptime = next.system ? next.system->uptime_seconds : 0.0;
        const auto total_memory = next.memory ? next.memory->total_bytes : 0;
        next.processes = process_collector_.collect(uptime, total_memory);
        next.network = network_collector_.collect();
        next.disks = disk_collector_.collect();

        // Filesystems and sensors change slowly, so refresh them every tenth cycle.
        if (cycle % 10 == 0) {
            next.filesystems = filesystem_collector_.collect();
            next.temperatures = temperature_collector_.collect();
        } else {
            std::lock_guard lock(mutex_);
            next.filesystems = snapshot_.filesystems;
            next.temperatures = snapshot_.temperatures;
        }

        {
            std::lock_guard lock(mutex_);
            next.generation = snapshot_.generation + 1;
            snapshot_ = std::move(next);
        }
        ++cycle;

        // Interruptible waiting lets q terminate promptly even with a long refresh.
        const auto deadline = cycle_start + refresh_interval_;
        std::unique_lock lock(mutex_);
        wakeup_.wait_until(lock, stop_token, deadline, [] { return false; });
    }
}

} // namespace pulse
