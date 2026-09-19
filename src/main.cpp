#include "pulse/collectors/cpu.hpp"
#include "pulse/collectors/memory.hpp"
#include "pulse/collectors/system.hpp"
#include "pulse/utils/format.hpp"
#include "pulse/version.hpp"

#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

// Print the stable command-line contract without initializing the terminal UI.
void print_help() {
    std::cout << "Pulse " << PULSE_VERSION << "\n\n"
              << "Usage:\n"
              << "  pulse [--refresh MILLISECONDS] [--no-color]\n"
              << "  pulse doctor\n"
              << "  pulse --help\n"
              << "  pulse --version\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string_view command{argv[1]};
        if (command == "--help" || command == "-h") {
            print_help();
            return 0;
        }
        if (command == "--version" || command == "-v") {
            std::cout << "pulse " << PULSE_VERSION << '\n';
            return 0;
        }
    }

    // Exercise the first collector slice until the interactive UI is connected.
    pulse::CpuCollector cpu;
    pulse::MemoryCollector memory;
    pulse::SystemCollector system;
    const auto before = cpu.collect();
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto after = cpu.collect();
    const auto ram = memory.collect();
    const auto metrics = system.collect_metrics();

    std::cout << "Pulse " << PULSE_VERSION << '\n';
    if (before && after) {
        const auto usage = pulse::calculate_cpu_usage(*before, *after);
        if (!usage.empty()) {
            std::cout << "CPU: " << usage.front().total_percent << "%\n";
        }
    }
    if (ram) {
        std::cout << "Memory: " << pulse::format_bytes(ram->used_bytes) << " / "
                  << pulse::format_bytes(ram->total_bytes) << '\n';
    }
    if (metrics) {
        std::cout << "Load: " << metrics->load_one << ' ' << metrics->load_five << ' '
                  << metrics->load_fifteen << '\n';
    }
    return 0;
}

