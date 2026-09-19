#include "pulse/collectors/cpu.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace pulse {
namespace {

// Subtract monotonic kernel counters while tolerating reset or wraparound.
std::uint64_t safe_delta(std::uint64_t before, std::uint64_t after) {
    return after >= before ? after - before : 0;
}

} // namespace

std::uint64_t CpuTimes::idle_all() const { return idle + iowait; }

std::uint64_t CpuTimes::non_idle() const {
    return user + nice + system + irq + softirq + steal;
}

std::uint64_t CpuTimes::total() const { return idle_all() + non_idle(); }

std::optional<CpuSample> parse_cpu_stat(std::string_view text) {
    std::istringstream input{std::string{text}};
    CpuSample sample;
    std::string line;

    // Read only the leading aggregate and per-core lines from /proc/stat.
    while (std::getline(input, line)) {
        if (!line.starts_with("cpu")) {
            break;
        }

        std::istringstream fields(line);
        CpuTimes times;
        if (!(fields >> times.name >> times.user >> times.nice >> times.system >> times.idle)) {
            return std::nullopt;
        }

        // Missing optional counters on older kernels are treated as zero.
        fields >> times.iowait >> times.irq >> times.softirq >> times.steal;
        sample.counters.push_back(times);
    }

    if (sample.counters.empty() || sample.counters.front().name != "cpu") {
        return std::nullopt;
    }
    return sample;
}

std::vector<CpuUsage> calculate_cpu_usage(const CpuSample& previous,
                                          const CpuSample& current) {
    std::unordered_map<std::string, CpuTimes> previous_by_name;
    for (const auto& counter : previous.counters) {
        previous_by_name.emplace(counter.name, counter);
    }

    std::vector<CpuUsage> result;
    result.reserve(current.counters.size());
    for (const auto& now : current.counters) {
        const auto old = previous_by_name.find(now.name);
        if (old == previous_by_name.end()) {
            continue;
        }

        const auto total_delta = safe_delta(old->second.total(), now.total());
        if (total_delta == 0) {
            result.push_back({.name = now.name});
            continue;
        }

        const auto percent = [total_delta](std::uint64_t delta) {
            return 100.0 * static_cast<double>(delta) / static_cast<double>(total_delta);
        };
        const auto idle_delta = safe_delta(old->second.idle_all(), now.idle_all());
        result.push_back({
            .name = now.name,
            .total_percent = std::clamp(100.0 - percent(idle_delta), 0.0, 100.0),
            .user_percent = percent(safe_delta(old->second.user + old->second.nice,
                                               now.user + now.nice)),
            .system_percent = percent(safe_delta(old->second.system, now.system)),
            .idle_percent = percent(idle_delta),
            .iowait_percent = percent(safe_delta(old->second.iowait, now.iowait)),
        });
    }
    return result;
}

CpuCollector::CpuCollector(std::filesystem::path proc_root) : proc_root_(std::move(proc_root)) {}

std::optional<CpuSample> CpuCollector::collect() const {
    std::ifstream input(proc_root_ / "stat");
    if (!input) {
        return std::nullopt;
    }
    return parse_cpu_stat(std::string{std::istreambuf_iterator<char>{input}, {}});
}

} // namespace pulse

