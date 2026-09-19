#include "pulse/app/doctor.hpp"

#include "pulse/collectors/cpu.hpp"
#include "pulse/collectors/memory.hpp"
#include "pulse/collectors/network.hpp"
#include "pulse/collectors/storage.hpp"
#include "pulse/collectors/system.hpp"
#include "pulse/collectors/temperature.hpp"
#include "pulse/utils/format.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace pulse {
namespace {

// Render a percentage consistently without implying excessive precision.
std::string percent(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value << '%';
    return output.str();
}

} // namespace

int run_doctor(std::ostream& output) {
    CpuCollector cpu;
    MemoryCollector memory;
    SystemCollector system;
    NetworkCollector network;
    FilesystemCollector filesystems;
    TemperatureCollector temperatures;

    const auto info = system.collect_info();
    const auto first_cpu = cpu.collect();
    [[maybe_unused]] const auto network_baseline = network.collect();
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    const auto second_cpu = cpu.collect();
    const auto ram = memory.collect();
    const auto live = system.collect_metrics();
    const auto nets = network.collect();
    const auto mounts = filesystems.collect();
    const auto sensors = temperatures.collect();
    const auto usage = first_cpu && second_cpu ? calculate_cpu_usage(*first_cpu, *second_cpu)
                                               : std::vector<CpuUsage>{};
    std::vector<std::string> warnings;

    output << "Pulse System Diagnostic\n\nSystem\n";
    output << (info.operating_system.empty() ? "Unknown distribution" : info.operating_system) << '\n';
    output << (info.kernel.empty() ? "Unknown kernel" : info.kernel) << '\n';
    if (live) {
        output << "Uptime: " << format_duration(std::chrono::seconds{
                                     static_cast<long long>(live->uptime_seconds)})
               << "\nLoad: " << live->load_one << " / " << live->load_five << " / "
               << live->load_fifteen << '\n';
    }

    output << "\nCPU\n";
    if (!usage.empty()) {
        output << "Usage: " << percent(usage.front().total_percent) << '\n';
        if (usage.front().total_percent >= 90.0) {
            warnings.emplace_back("CPU usage is currently above 90%.");
        }
    } else {
        output << "Usage: unavailable\n";
    }
    if (!sensors.empty()) {
        const auto hottest = std::ranges::max_element(sensors, {}, &Temperature::celsius);
        output << "Highest temperature: " << std::fixed << std::setprecision(1) << hottest->celsius
               << " C (" << hottest->label << ")\n";
        if (hottest->celsius >= 85.0) {
            warnings.emplace_back("A reported temperature sensor is at or above 85 C.");
        }
    } else {
        output << "Temperature: unavailable\n";
    }

    output << "\nMemory\n";
    if (ram) {
        const auto ram_percent = ram->total_bytes > 0
                                     ? 100.0 * static_cast<double>(ram->used_bytes) /
                                           static_cast<double>(ram->total_bytes)
                                     : 0.0;
        output << "Used: " << format_bytes(ram->used_bytes) << " / "
               << format_bytes(ram->total_bytes) << " (" << percent(ram_percent) << ")\n";
        if (ram_percent >= 90.0) {
            warnings.emplace_back("Available memory is below 10% of total memory.");
        }
        if (ram->swap_total_bytes > 0) {
            const auto swap_percent = 100.0 * static_cast<double>(ram->swap_used_bytes) /
                                      static_cast<double>(ram->swap_total_bytes);
            output << "Swap: " << format_bytes(ram->swap_used_bytes) << " / "
                   << format_bytes(ram->swap_total_bytes) << '\n';
            if (swap_percent >= 75.0) {
                warnings.emplace_back("Swap usage is at or above 75%.");
            }
        }
    } else {
        output << "Unavailable\n";
    }

    output << "\nFilesystems\n";
    if (mounts.empty()) {
        output << "Unavailable\n";
    }
    for (const auto& mount : mounts) {
        output << mount.mount_point << ": " << format_bytes(mount.used_bytes) << " / "
               << format_bytes(mount.total_bytes) << " (" << percent(mount.used_percent) << ")\n";
        if (mount.used_percent >= 90.0) {
            warnings.push_back("Filesystem " + mount.mount_point + " is at or above 90% usage.");
        }
    }

    output << "\nNetwork\n";
    bool printed_network = false;
    for (const auto& interface : nets) {
        if (interface.name != "lo" &&
            (interface.receive_bytes_per_second > 0.0 || interface.transmit_bytes_per_second > 0.0)) {
            output << interface.name << ": down "
                   << format_bytes(static_cast<std::uint64_t>(interface.receive_bytes_per_second))
                   << "/s, up "
                   << format_bytes(static_cast<std::uint64_t>(interface.transmit_bytes_per_second))
                   << "/s\n";
            printed_network = true;
        }
    }
    if (!printed_network) {
        const auto detected = std::ranges::find_if(nets, [](const NetworkInterface& interface) {
            return interface.name != "lo";
        });
        if (detected != nets.end()) {
            output << detected->name << ": detected, idle during sample\n";
        } else {
            output << "No non-loopback interface detected.\n";
        }
    }

    output << "\nWarnings\n";
    if (warnings.empty()) {
        output << "No threshold warnings detected.\n";
    } else {
        for (const auto& warning : warnings) {
            output << "[WARN] " << warning << '\n';
        }
    }
    output << '\n' << warnings.size() << " warning" << (warnings.size() == 1 ? "" : "s")
           << " detected.\n";
    return static_cast<int>(warnings.size());
}

} // namespace pulse
