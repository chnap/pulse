#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace pulse {

// Format byte counts using compact binary units suitable for a terminal.
[[nodiscard]] std::string format_bytes(std::uint64_t bytes, int precision = 1);

// Format seconds as a compact duration without locale-dependent text.
[[nodiscard]] std::string format_duration(std::chrono::seconds duration);

} // namespace pulse
