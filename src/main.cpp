#include "pulse/app/doctor.hpp"
#include "pulse/app/monitor.hpp"
#include "pulse/ui/tui.hpp"
#include "pulse/version.hpp"

#include <charconv>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

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

// Parse a decimal refresh interval without locale or exception behavior.
std::optional<int> parse_refresh(std::string_view value) {
    int milliseconds{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), milliseconds);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        milliseconds < 100 || milliseconds > 10000) {
        return std::nullopt;
    }
    return milliseconds;
}

} // namespace

int main(int argc, char** argv) {
    int refresh = 1000;
    bool use_color = true;
    for (int index = 1; index < argc; ++index) {
        const std::string_view command{argv[index]};
        if (command == "--help" || command == "-h") {
            print_help();
            return 0;
        }
        if (command == "--version" || command == "-v") {
            std::cout << "pulse " << PULSE_VERSION << '\n';
            return 0;
        }
        if (command == "doctor") {
            return pulse::run_doctor(std::cout) > 0 ? 1 : 0;
        }
        if (command == "--no-color") {
            use_color = false;
            continue;
        }
        if (command == "--refresh" && index + 1 < argc) {
            const auto parsed = parse_refresh(argv[++index]);
            if (!parsed) {
                std::cerr << "pulse: refresh must be between 100 and 10000 milliseconds\n";
                return 2;
            }
            refresh = *parsed;
            continue;
        }
        std::cerr << "pulse: unknown argument: " << command << "\nTry 'pulse --help'.\n";
        return 2;
    }

    if (::isatty(STDIN_FILENO) == 0 || ::isatty(STDOUT_FILENO) == 0) {
        std::cerr
            << "pulse: interactive mode requires a terminal; use 'pulse doctor' for text output\n";
        return 2;
    }
    pulse::Monitor monitor{std::chrono::milliseconds{refresh}};
    return pulse::run_tui(monitor, use_color);
}
