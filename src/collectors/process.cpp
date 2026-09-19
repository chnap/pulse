#include "pulse/collectors/process.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <pwd.h>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace pulse {
namespace {

// Read a small proc file without throwing when the process disappears.
std::optional<std::string> read_file(const std::filesystem::path& path, bool binary = false) {
    std::ifstream input(path, binary ? std::ios::binary : std::ios::in);
    if (!input) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{input}, {}};
}

// Recognize numeric directory names without accepting signs or partial text.
std::optional<Pid> parse_pid(std::string_view name) {
    Pid pid{};
    const auto result = std::from_chars(name.data(), name.data() + name.size(), pid);
    if (result.ec != std::errc{} || result.ptr != name.data() + name.size() || pid <= 0) {
        return std::nullopt;
    }
    return pid;
}

// Parse a complete integer field without exceptions or partial conversions.
template <typename T> bool parse_number(std::string_view text, T& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

// Add cumulative process ticks without wrapping an unsigned counter.
std::uint64_t safe_add(std::uint64_t left, std::uint64_t right) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

// Resolve an effective UID from /proc status to a displayable account name.
std::string read_user(const std::filesystem::path& status_path) {
    const auto status = read_file(status_path);
    if (!status) {
        return "?";
    }
    const auto marker = status->find("Uid:");
    if (marker == std::string::npos) {
        return "?";
    }
    std::istringstream uid_line(status->substr(marker + 4));
    uid_t uid{};
    if (!(uid_line >> uid)) {
        return "?";
    }

    long size = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    size = size > 0 ? size : 16384;
    size = std::clamp(size, 1024L, 1024L * 1024L);
    std::vector<char> buffer(static_cast<std::size_t>(size));
    struct passwd entry{};
    struct passwd* result = nullptr;
    if (::getpwuid_r(uid, &entry, buffer.data(), buffer.size(), &result) == 0 &&
        result != nullptr) {
        return entry.pw_name;
    }
    return std::to_string(uid);
}

// Replace NUL argument separators with spaces for a readable command line.
std::string read_command(const std::filesystem::path& path, std::string_view fallback) {
    auto command = read_file(path, true).value_or("");
    std::replace(command.begin(), command.end(), '\0', ' ');
    while (!command.empty() && command.back() == ' ') {
        command.pop_back();
    }
    return command.empty() ? std::string{fallback} : command;
}

// Read aggregate CPU ticks so process CPU can use the same sampling interval.
std::uint64_t read_total_cpu_ticks(const std::filesystem::path& stat_path) {
    std::ifstream input(stat_path);
    std::string label;
    std::uint64_t value{};
    std::uint64_t total{};
    if (!(input >> label) || label != "cpu") {
        return 0;
    }
    while (input >> value) {
        total = safe_add(total, value);
        if (input.peek() == '\n') {
            break;
        }
    }
    return total;
}

} // namespace

std::optional<ProcessStat> parse_process_stat(std::string_view text) {
    const auto open = text.find('(');
    const auto close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open) {
        return std::nullopt;
    }

    ProcessStat stat;
    const auto pid_text = text.substr(0, open);
    const auto pid = parse_pid(pid_text.substr(0, pid_text.find_last_not_of(' ') + 1));
    if (!pid) {
        return std::nullopt;
    }
    stat.pid = *pid;
    stat.name = text.substr(open + 1, close - open - 1);

    std::istringstream fields{std::string{text.substr(close + 1)}};
    std::vector<std::string> values;
    std::string value;
    while (fields >> value) {
        values.push_back(value);
    }

    // Indices are relative to field 3 because pid and comm were parsed separately.
    if (values.size() < 22) {
        return std::nullopt;
    }
    stat.state = values[0].front();
    if (!parse_number(values[1], stat.parent_pid) || !parse_number(values[11], stat.user_ticks) ||
        !parse_number(values[12], stat.system_ticks) || !parse_number(values[17], stat.threads) ||
        !parse_number(values[19], stat.start_time_ticks) ||
        !parse_number(values[21], stat.resident_pages)) {
        return std::nullopt;
    }
    return stat;
}

ProcessCollector::ProcessCollector(std::filesystem::path proc_root)
    : proc_root_(std::move(proc_root)), clock_ticks_(::sysconf(_SC_CLK_TCK)),
      page_size_(::sysconf(_SC_PAGESIZE)) {}

std::vector<ProcessInfo> ProcessCollector::collect(double uptime_seconds,
                                                   std::uint64_t total_memory_bytes) {
    const auto total_cpu_ticks = read_total_cpu_ticks(proc_root_ / "stat");
    const auto total_delta = total_cpu_ticks >= previous_total_cpu_ticks_
                                 ? total_cpu_ticks - previous_total_cpu_ticks_
                                 : 0;
    const auto logical_cpus = std::max(1U, std::thread::hardware_concurrency());
    std::unordered_map<Pid, PreviousProcess> next_previous;
    std::vector<ProcessInfo> processes;

    std::error_code error;
    for (std::filesystem::directory_iterator entries(proc_root_, error), end; entries != end;
         entries.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        const auto pid = parse_pid(entries->path().filename().string());
        if (!pid) {
            continue;
        }
        const auto stat_text = read_file(entries->path() / "stat");
        const auto stat = stat_text ? parse_process_stat(*stat_text) : std::nullopt;
        if (!stat) {
            continue;
        }

        const auto cpu_ticks = safe_add(stat->user_ticks, stat->system_ticks);
        double cpu_percent = 0.0;
        const auto old = previous_processes_.find(*pid);
        if (old != previous_processes_.end() &&
            old->second.start_time_ticks == stat->start_time_ticks &&
            cpu_ticks >= old->second.cpu_time_ticks && total_delta > 0) {
            const auto process_delta = cpu_ticks - old->second.cpu_time_ticks;
            cpu_percent = 100.0 * static_cast<double>(process_delta) * logical_cpus /
                          static_cast<double>(total_delta);
        }

        const auto positive_pages = std::max<std::int64_t>(0, stat->resident_pages);
        const auto pages = static_cast<std::uint64_t>(positive_pages);
        const auto page_size = static_cast<std::uint64_t>(std::max(0L, page_size_));
        const auto resident =
            page_size > 0 && pages > std::numeric_limits<std::uint64_t>::max() / page_size
                ? std::numeric_limits<std::uint64_t>::max()
                : pages * page_size;
        processes.push_back({
            .pid = *pid,
            .parent_pid = stat->parent_pid,
            .name = stat->name,
            .command = read_command(entries->path() / "cmdline", stat->name),
            .user = read_user(entries->path() / "status"),
            .state = stat->state,
            .threads = stat->threads,
            .resident_bytes = resident,
            .memory_percent = total_memory_bytes > 0 ? 100.0 * static_cast<double>(resident) /
                                                           static_cast<double>(total_memory_bytes)
                                                     : 0.0,
            .cpu_percent = cpu_percent,
            .runtime_seconds =
                clock_ticks_ > 0
                    ? std::max(0.0, uptime_seconds - static_cast<double>(stat->start_time_ticks) /
                                                         static_cast<double>(clock_ticks_))
                    : 0.0,
            .start_time_ticks = stat->start_time_ticks,
            .cpu_time_ticks = cpu_ticks,
        });
        next_previous[*pid] = {stat->start_time_ticks, cpu_ticks};
    }

    previous_total_cpu_ticks_ = total_cpu_ticks;
    previous_processes_ = std::move(next_previous);
    return processes;
}

} // namespace pulse
