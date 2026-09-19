#include "pulse/collectors/temperature.hpp"

#include <fstream>
#include <set>
#include <string>

namespace pulse {
namespace {

// Read a sysfs text file and trim its trailing newline.
std::string read_line(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string value;
    std::getline(input, value);
    return value;
}

// Add a millidegree sensor only when its value is physically plausible.
void add_sensor(std::vector<Temperature>& sensors, std::set<std::string>& labels,
                const std::filesystem::path& input_path, std::string label) {
    std::ifstream input(input_path);
    double millidegrees{};
    if (!(input >> millidegrees)) {
        return;
    }
    const auto celsius = millidegrees / 1000.0;
    if (celsius < -100.0 || celsius > 250.0) {
        return;
    }
    if (label.empty()) {
        label = input_path.parent_path().filename().string();
    }
    if (!labels.insert(label).second) {
        label.append(" (").append(input_path.parent_path().filename().string()).append(")");
        if (!labels.insert(label).second) {
            return;
        }
    }
    sensors.push_back({.label = std::move(label), .celsius = celsius});
}

} // namespace

TemperatureCollector::TemperatureCollector(std::filesystem::path sys_root)
    : sys_root_(std::move(sys_root)) {}

std::vector<Temperature> TemperatureCollector::collect() const {
    std::vector<Temperature> sensors;
    std::set<std::string> labels;
    std::error_code error;
    const auto thermal_root = sys_root_ / "class/thermal";
    for (std::filesystem::directory_iterator entries(thermal_root, error), end; entries != end;
         entries.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (entries->path().filename().string().starts_with("thermal_zone")) {
            add_sensor(sensors, labels, entries->path() / "temp",
                       read_line(entries->path() / "type"));
        }
    }

    const auto hwmon_root = sys_root_ / "class/hwmon";
    error.clear();
    for (std::filesystem::directory_iterator entries(hwmon_root, error), end; entries != end;
         entries.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        const auto chip = read_line(entries->path() / "name");
        for (int index = 1; index <= 32; ++index) {
            const auto prefix = "temp" + std::to_string(index);
            const auto input_path = entries->path() / (prefix + "_input");
            if (!std::filesystem::exists(input_path, error)) {
                continue;
            }
            auto label = read_line(entries->path() / (prefix + "_label"));
            if (label.empty()) {
                label = chip;
                if (!label.empty()) {
                    label += ' ';
                }
                label += prefix;
            }
            add_sensor(sensors, labels, input_path, std::move(label));
        }
    }
    return sensors;
}

} // namespace pulse
