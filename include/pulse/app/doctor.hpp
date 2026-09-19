#pragma once

#include <iosfwd>

namespace pulse {

// Produce a non-interactive rule-based diagnostic and return its warning count.
int run_doctor(std::ostream& output);

} // namespace pulse
