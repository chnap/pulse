#include "pulse/collectors/storage.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/statvfs.h>

namespace pulse {
namespace {

// Filter pseudo filesystems that do not represent user storage capacity.
bool is_virtual_filesystem(std::string_view type) {
    static const std::set<std::string> virtual_types{
        "autofs",      "bpf",      "cgroup", "cgroup2", "configfs", "debugfs",
        "devpts",      "devtmpfs", "fusectl", "hugetlbfs", "mqueue", "proc",
        "pstore",      "securityfs", "sysfs", "tmpfs", "tracefs", "overlay"};
    return virtual_types.contains(std::string{type});
}

// Decode the octal whitespace escapes used in /proc/mounts.
std::string decode_mount(std::string value) {
    for (const auto& [encoded, decoded] :
         std::array<std::pair<std::string_view, char>, 4>{{{"\\040", ' '},
                                                           {"\\011", '\t'},
                                                           {"\\012", '\n'},
                                                           {"\\134", '\\'}}}) {
        std::size_t position = 0;
        while ((position = value.find(encoded, position)) != std::string::npos) {
            value.replace(position, encoded.size(), 1, decoded);
        }
    }
    return value;
}

// Identify whole block devices by the existence of their sysfs device directory.
bool is_whole_device(const std::filesystem::path& sys_root, std::string_view name) {
    std::error_code error;
    const auto base = sys_root / "class/block" / std::string{name};
    return std::filesystem::exists(base, error) && !std::filesystem::exists(base / "partition", error) &&
           !name.starts_with("loop") && !name.starts_with("ram") && !name.starts_with("zram");
}

} // namespace

FilesystemCollector::FilesystemCollector(std::filesystem::path proc_root)
    : proc_root_(std::move(proc_root)) {}

std::vector<FilesystemUsage> FilesystemCollector::collect() const {
    std::ifstream mounts(proc_root_ / "mounts");
    std::vector<FilesystemUsage> result;
    std::set<std::string> seen;
    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream fields(line);
        std::string device;
        std::string mount;
        std::string type;
        if (!(fields >> device >> mount >> type) || is_virtual_filesystem(type)) {
            continue;
        }
        mount = decode_mount(mount);
        if (!seen.insert(mount).second) {
            continue;
        }
        struct statvfs stats {};
        if (::statvfs(mount.c_str(), &stats) != 0) {
            continue;
        }
        const auto block_size = static_cast<std::uint64_t>(stats.f_frsize);
        const auto total = static_cast<std::uint64_t>(stats.f_blocks) * block_size;
        const auto available = static_cast<std::uint64_t>(stats.f_bavail) * block_size;
        const auto used = total >= available ? total - available : 0;
        result.push_back({.device = decode_mount(device),
                          .mount_point = mount,
                          .type = type,
                          .total_bytes = total,
                          .used_bytes = used,
                          .used_percent = total > 0 ? 100.0 * static_cast<double>(used) /
                                                           static_cast<double>(total)
                                                   : 0.0});
    }
    std::ranges::sort(result, {}, &FilesystemUsage::mount_point);
    return result;
}

DiskCollector::DiskCollector(std::filesystem::path proc_root, std::filesystem::path sys_root)
    : proc_root_(std::move(proc_root)), sys_root_(std::move(sys_root)) {}

std::vector<DiskActivity> DiskCollector::collect() {
    std::ifstream input(proc_root_ / "diskstats");
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = previous_time_ == std::chrono::steady_clock::time_point{}
                             ? 0.0
                             : std::chrono::duration<double>(now - previous_time_).count();
    std::unordered_map<std::string, DiskActivity> next;
    std::vector<DiskActivity> result;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        unsigned int major{};
        unsigned int minor{};
        std::string name;
        std::uint64_t reads{};
        std::uint64_t merged_reads{};
        std::uint64_t sectors_read{};
        std::uint64_t read_ms{};
        std::uint64_t writes{};
        std::uint64_t merged_writes{};
        std::uint64_t sectors_written{};
        if (!(fields >> major >> minor >> name >> reads >> merged_reads >> sectors_read >> read_ms >>
              writes >> merged_writes >> sectors_written) ||
            !is_whole_device(sys_root_, name)) {
            continue;
        }
        constexpr std::uint64_t linux_sector_size = 512;
        DiskActivity disk{.name = name,
                          .read_bytes = sectors_read * linux_sector_size,
                          .write_bytes = sectors_written * linux_sector_size};
        const auto old = previous_.find(name);
        if (old != previous_.end() && elapsed > 0.0) {
            disk.read_bytes_per_second = disk.read_bytes >= old->second.read_bytes
                                             ? static_cast<double>(disk.read_bytes - old->second.read_bytes) /
                                                   elapsed
                                             : 0.0;
            disk.write_bytes_per_second =
                disk.write_bytes >= old->second.write_bytes
                    ? static_cast<double>(disk.write_bytes - old->second.write_bytes) / elapsed
                    : 0.0;
        }
        next.emplace(name, disk);
        result.push_back(disk);
    }
    previous_ = std::move(next);
    previous_time_ = now;
    return result;
}

} // namespace pulse
