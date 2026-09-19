#include "pulse/collectors/network.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace pulse {
namespace {

// Return a monotonic counter difference and treat resets as a fresh baseline.
std::uint64_t delta(std::uint64_t before, std::uint64_t after) {
    return after >= before ? after - before : 0;
}

} // namespace

std::optional<std::vector<NetworkInterface>> parse_net_dev(std::string_view text) {
    std::istringstream input{std::string{text}};
    std::string line;
    std::vector<NetworkInterface> interfaces;
    std::getline(input, line);
    std::getline(input, line);
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        auto name = line.substr(0, colon);
        const auto begin = name.find_first_not_of(" \t");
        const auto finish = name.find_last_not_of(" \t");
        if (begin == std::string::npos) {
            continue;
        }
        name = name.substr(begin, finish - begin + 1);

        std::istringstream fields(line.substr(colon + 1));
        std::uint64_t receive{};
        std::uint64_t transmit{};
        std::uint64_t ignored{};
        if (!(fields >> receive)) {
            return std::nullopt;
        }
        for (int index = 0; index < 7; ++index) {
            if (!(fields >> ignored)) {
                return std::nullopt;
            }
        }
        if (!(fields >> transmit)) {
            return std::nullopt;
        }
        interfaces.push_back({.name = std::move(name),
                              .received_bytes = receive,
                              .transmitted_bytes = transmit});
    }
    return interfaces;
}

NetworkCollector::NetworkCollector(std::filesystem::path proc_root)
    : proc_root_(std::move(proc_root)) {}

std::vector<NetworkInterface> NetworkCollector::collect() {
    std::ifstream input(proc_root_ / "net/dev");
    if (!input) {
        return {};
    }
    const auto parsed = parse_net_dev(std::string{std::istreambuf_iterator<char>{input}, {}});
    if (!parsed) {
        return {};
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = previous_time_ == std::chrono::steady_clock::time_point{}
                             ? 0.0
                             : std::chrono::duration<double>(now - previous_time_).count();
    std::unordered_map<std::string, NetworkInterface> next;
    auto result = *parsed;
    for (auto& interface : result) {
        const auto old = previous_.find(interface.name);
        if (old != previous_.end() && elapsed > 0.0) {
            interface.receive_bytes_per_second =
                static_cast<double>(delta(old->second.received_bytes, interface.received_bytes)) /
                elapsed;
            interface.transmit_bytes_per_second =
                static_cast<double>(delta(old->second.transmitted_bytes,
                                          interface.transmitted_bytes)) /
                elapsed;
        }
        next.emplace(interface.name, interface);
    }
    previous_ = std::move(next);
    previous_time_ = now;
    return result;
}

} // namespace pulse

