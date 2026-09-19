#include "pulse/collectors/memory.hpp"

#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace pulse {
namespace {

// Convert the KiB units used by /proc/meminfo into bytes without overflow.
std::optional<std::uint64_t> kib_to_bytes(std::uint64_t kib) {
    constexpr std::uint64_t scale = 1024;
    if (kib > std::numeric_limits<std::uint64_t>::max() / scale) {
        return std::nullopt;
    }
    return kib * scale;
}

// Saturate optional cache components before their final unit conversion.
std::uint64_t safe_add(std::uint64_t left, std::uint64_t right) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

} // namespace

std::optional<MemoryMetrics> parse_meminfo(std::string_view text) {
    std::istringstream input{std::string{text}};
    std::unordered_map<std::string, std::uint64_t> values;
    std::string line;

    // Parse each line independently because several valid fields have no unit.
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string key;
        std::uint64_t value{};
        if (!(fields >> key >> value)) {
            continue;
        }
        if (!key.empty() && key.back() == ':') {
            key.pop_back();
        }
        values[key] = value;
    }

    if (!values.contains("MemTotal") || !values.contains("MemAvailable")) {
        return std::nullopt;
    }

    const auto total = kib_to_bytes(values["MemTotal"]);
    const auto available = kib_to_bytes(values["MemAvailable"]);
    const auto cached = kib_to_bytes(safe_add(values["Cached"], values["SReclaimable"]));
    const auto swap_total = kib_to_bytes(values["SwapTotal"]);
    const auto swap_free = kib_to_bytes(values["SwapFree"]);
    if (!total || !available || !cached || !swap_total || !swap_free) {
        return std::nullopt;
    }

    MemoryMetrics metrics;
    metrics.total_bytes = *total;
    metrics.available_bytes = std::min(*available, *total);
    metrics.used_bytes = metrics.total_bytes - metrics.available_bytes;
    metrics.cached_bytes = *cached;
    metrics.swap_total_bytes = *swap_total;
    metrics.swap_used_bytes = *swap_total >= *swap_free ? *swap_total - *swap_free : 0;
    return metrics;
}

MemoryCollector::MemoryCollector(std::filesystem::path proc_root)
    : proc_root_(std::move(proc_root)) {}

std::optional<MemoryMetrics> MemoryCollector::collect() const {
    std::ifstream input(proc_root_ / "meminfo");
    if (!input) {
        return std::nullopt;
    }
    return parse_meminfo(std::string{std::istreambuf_iterator<char>{input}, {}});
}

} // namespace pulse
