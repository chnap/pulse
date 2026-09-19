#include "pulse/utils/format.hpp"

#include <array>
#include <iomanip>
#include <sstream>

namespace pulse {

// The distinct names make this compact formatting API unambiguous at call sites.
std::string format_bytes(std::uint64_t bytes, int precision) { // NOLINT
    constexpr double unit = 1024.0;
    constexpr std::array<const char*, 5> suffixes{"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t suffix = 0;
    while (value >= unit && suffix + 1 < suffixes.size()) {
        value /= unit;
        ++suffix;
    }

    std::ostringstream output;
    const auto display_precision = suffix == 0 || value >= 100.0 ? 0 : precision;
    output << std::fixed << std::setprecision(display_precision) << value << ' '
           << suffixes[suffix];
    return output.str();
}

std::string format_duration(std::chrono::seconds duration) {
    const auto total = duration.count();
    const auto days = total / 86400;
    const auto hours = (total % 86400) / 3600;
    const auto minutes = (total % 3600) / 60;
    const auto seconds = total % 60;

    std::ostringstream output;
    if (days > 0) {
        output << days << "d ";
    }
    output << std::setfill('0') << std::setw(2) << hours << ':' << std::setw(2) << minutes << ':'
           << std::setw(2) << seconds;
    return output.str();
}

} // namespace pulse
