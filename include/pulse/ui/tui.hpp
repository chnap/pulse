#pragma once

#include "pulse/app/monitor.hpp"

namespace pulse {

// Run the interactive ncurses interface until the user requests shutdown.
int run_tui(Monitor& monitor, bool use_color);

} // namespace pulse
