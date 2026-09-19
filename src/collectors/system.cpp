#include "pulse/collectors/system.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>

namespace pulse {
namespace {

// Read one text file and return an empty string when it is unavailable.
std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    return input ? std::string{std::istreambuf_iterator<char>{input}, {}} : std::string{};
}

// Extract a named colon-separated value from Linux metadata files.
std::string find_value(std::string_view text, std::string_view name) {
    std::istringstream input{std::string{text}};
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon != std::string::npos && line.substr(0, colon) == name) {
            const auto start = line.find_first_not_of(" \t", colon + 1);
            return start == std::string::npos ? std::string{} : line.substr(start);
        }
    }
    return {};
}

} // namespace

SystemCollector::SystemCollector(std::filesystem::path proc_root)
    : proc_root_(std::move(proc_root)) {}

SystemInfo SystemCollector::collect_info() const {
    SystemInfo info;
    char hostname[256]{};
    if (::gethostname(hostname, sizeof(hostname) - 1) == 0) {
        info.hostname = hostname;
    }

    struct utsname system_name {};
    if (::uname(&system_name) == 0) {
        info.kernel = std::string{system_name.sysname} + " " + system_name.release;
    }

    const auto os_release = read_text("/etc/os-release");
    const auto pretty = os_release.find("PRETTY_NAME=");
    if (pretty != std::string::npos) {
        const auto begin = pretty + std::string{"PRETTY_NAME="}.size();
        const auto end = os_release.find('\n', begin);
        info.operating_system = os_release.substr(begin, end - begin);
        if (info.operating_system.size() >= 2 && info.operating_system.front() == '"' &&
            info.operating_system.back() == '"') {
            info.operating_system = info.operating_system.substr(1, info.operating_system.size() - 2);
        }
    }

    info.cpu_model = find_value(read_text(proc_root_ / "cpuinfo"), "model name");
    info.logical_cpus = std::thread::hardware_concurrency();
    return info;
}

std::optional<SystemMetrics> SystemCollector::collect_metrics() const {
    SystemMetrics metrics;
    std::ifstream uptime(proc_root_ / "uptime");
    std::ifstream load(proc_root_ / "loadavg");
    if (!(uptime >> metrics.uptime_seconds) ||
        !(load >> metrics.load_one >> metrics.load_five >> metrics.load_fifteen)) {
        return std::nullopt;
    }
    return metrics;
}

} // namespace pulse

