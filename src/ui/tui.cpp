#include "pulse/ui/tui.hpp"

#include "pulse/utils/format.hpp"
#include "pulse/utils/ring_buffer.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cctype>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <ncurses.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace pulse {
namespace {

// Name the user-selectable process ordering modes shown in the footer.
enum class SortKey { Cpu, Memory, Pid, Name };

// Restore the terminal even when normal C++ stack unwinding leaves the UI.
class TerminalSession {
  public:
    explicit TerminalSession(bool use_color) {
        std::setlocale(LC_ALL, "");
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE);
        curs_set(0);
        if (use_color && has_colors()) {
            start_color();
            use_default_colors();
            init_pair(1, COLOR_CYAN, -1);
            init_pair(2, COLOR_GREEN, -1);
            init_pair(3, COLOR_YELLOW, -1);
            init_pair(4, COLOR_RED, -1);
            init_pair(5, COLOR_BLACK, COLOR_CYAN);
            colors_ = true;
        }
    }

    ~TerminalSession() { endwin(); }

    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    [[nodiscard]] bool colors() const noexcept { return colors_; }

  private:
    bool colors_{};
};

// Convert a string to lowercase without invoking undefined behavior on signed chars.
std::string lowercase(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text) {
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

// Keep terminal writes inside the current screen and truncate long text safely.
void put(int row, int column, std::string_view text, int width = -1) {
    int height{};
    int terminal_width{};
    getmaxyx(stdscr, height, terminal_width);
    if (row < 0 || row >= height || column < 0 || column >= terminal_width) {
        return;
    }
    const auto available = width < 0 ? terminal_width - column : std::min(width, terminal_width - column);
    if (available > 0) {
        mvaddnstr(row, column, text.data(), std::min<int>(available, static_cast<int>(text.size())));
    }
}

// Format a floating-point terminal metric with a fixed number of decimals.
std::string fixed(double value, int precision = 1) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << value;
    return output.str();
}

// Build a width-adaptive ASCII sparkline that works on limited terminals.
std::string graph(const std::deque<double>& values, int width, double maximum) {
    constexpr std::string_view levels = " .:-=+*#%@";
    if (width <= 0 || values.empty() || maximum <= 0.0) {
        return {};
    }
    const auto count = std::min<std::size_t>(values.size(), static_cast<std::size_t>(width));
    const auto start = values.size() - count;
    std::string result(static_cast<std::size_t>(width - static_cast<int>(count)), ' ');
    for (std::size_t index = start; index < values.size(); ++index) {
        const auto normalized = std::clamp(values[index] / maximum, 0.0, 1.0);
        const auto level = static_cast<std::size_t>(std::round(normalized *
                                                               static_cast<double>(levels.size() - 1)));
        result.push_back(levels[level]);
    }
    return result;
}

// Map Linux process state letters to short human-readable labels.
std::string_view state_name(char state) {
    switch (state) {
    case 'R':
        return "Running";
    case 'S':
        return "Sleeping";
    case 'D':
        return "Disk sleep";
    case 'T':
    case 't':
        return "Stopped";
    case 'Z':
        return "Zombie";
    case 'I':
        return "Idle";
    default:
        return "Unknown";
    }
}

// Return the visible label for the active process sort.
std::string_view sort_name(SortKey key) {
    switch (key) {
    case SortKey::Cpu:
        return "CPU";
    case SortKey::Memory:
        return "MEM";
    case SortKey::Pid:
        return "PID";
    case SortKey::Name:
        return "NAME";
    }
    return "CPU";
}

// Pick one representative interface without double-counting bridge and veth traffic.
const NetworkInterface* busiest_interface(const std::vector<NetworkInterface>& interfaces) {
    const NetworkInterface* busiest = nullptr;
    for (const auto& interface : interfaces) {
        if (interface.name == "lo") {
            continue;
        }
        const auto rate = interface.receive_bytes_per_second + interface.transmit_bytes_per_second;
        const auto best_rate = busiest == nullptr
                                   ? -1.0
                                   : busiest->receive_bytes_per_second +
                                         busiest->transmit_bytes_per_second;
        if (busiest == nullptr || rate > best_rate) {
            busiest = &interface;
        }
    }
    return busiest;
}

// Pick the block device with the greatest current combined transfer rate.
const DiskActivity* busiest_disk(const std::vector<DiskActivity>& disks) {
    return disks.empty()
               ? nullptr
               : &*std::ranges::max_element(disks, {}, [](const DiskActivity& disk) {
                     return disk.read_bytes_per_second + disk.write_bytes_per_second;
                 });
}

// Own interactive view state while all Linux collection stays in Monitor.
class Tui {
  public:
    Tui(Monitor& monitor, bool use_color)
        : monitor_(monitor), terminal_(use_color), cpu_history_(120), memory_history_(120),
          download_history_(120), upload_history_(120), process_cpu_history_(120),
          process_memory_history_(120) {}

    int run() {
        monitor_.start();
        while (!quit_) {
            update_snapshot();
            handle_input(getch());
            draw();
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
        }
        monitor_.stop();
        return 0;
    }

  private:
    // Pull a new generation only once and update every bounded history together.
    void update_snapshot() {
        const auto latest = monitor_.snapshot();
        if (latest.generation == 0 || latest.generation == snapshot_.generation) {
            return;
        }
        snapshot_ = latest;
        if (!snapshot_.cpu.empty()) {
            cpu_history_.push(snapshot_.cpu.front().total_percent);
        }
        if (snapshot_.memory && snapshot_.memory->total_bytes > 0) {
            memory_history_.push(100.0 * static_cast<double>(snapshot_.memory->used_bytes) /
                                 static_cast<double>(snapshot_.memory->total_bytes));
        }
        const auto interface = busiest_interface(snapshot_.network);
        const auto download = interface ? interface->receive_bytes_per_second : 0.0;
        const auto upload = interface ? interface->transmit_bytes_per_second : 0.0;
        download_history_.push(download);
        upload_history_.push(upload);
        rebuild_processes();

        if (inspected_pid_) {
            const auto process = find_process(*inspected_pid_);
            if (process) {
                process_cpu_history_.push(process->cpu_percent);
                process_memory_history_.push(process->memory_percent);
            }
        }
    }

    // Filter and sort a private view so keyboard state never mutates shared data.
    void rebuild_processes() {
        const auto old_pid = selected_process() ? std::optional<Pid>{selected_process()->pid} : std::nullopt;
        displayed_.clear();
        const auto needle = lowercase(query_);
        for (const auto& process : snapshot_.processes) {
            const auto haystack = lowercase(process.name + " " + process.command + " " +
                                             std::to_string(process.pid));
            if (needle.empty() || haystack.find(needle) != std::string::npos) {
                displayed_.push_back(process);
            }
        }
        std::ranges::sort(displayed_, [this](const ProcessInfo& left, const ProcessInfo& right) {
            switch (sort_) {
            case SortKey::Cpu:
                return left.cpu_percent != right.cpu_percent ? left.cpu_percent > right.cpu_percent
                                                             : left.pid < right.pid;
            case SortKey::Memory:
                return left.resident_bytes != right.resident_bytes
                           ? left.resident_bytes > right.resident_bytes
                           : left.pid < right.pid;
            case SortKey::Pid:
                return left.pid < right.pid;
            case SortKey::Name:
                return left.name != right.name ? left.name < right.name : left.pid < right.pid;
            }
            return false;
        });
        selected_ = std::min(selected_, displayed_.empty() ? std::size_t{0} : displayed_.size() - 1);
        if (old_pid) {
            const auto found = std::ranges::find(displayed_, *old_pid, &ProcessInfo::pid);
            if (found != displayed_.end()) {
                selected_ = static_cast<std::size_t>(std::distance(displayed_.begin(), found));
            }
        }
    }

    // Return the currently highlighted process when the filtered table is non-empty.
    const ProcessInfo* selected_process() const {
        return selected_ < displayed_.size() ? &displayed_[selected_] : nullptr;
    }

    // Locate an inspected PID in the newest full snapshot instead of the filtered table.
    const ProcessInfo* find_process(Pid pid) const {
        const auto found = std::ranges::find(snapshot_.processes, pid, &ProcessInfo::pid);
        return found == snapshot_.processes.end() ? nullptr : &*found;
    }

    // Handle search text separately so normal hotkeys cannot trigger while typing.
    void handle_input(int key) {
        if (key == ERR || key == KEY_RESIZE) {
            return;
        }
        if (confirm_signal_) {
            handle_confirmation(key);
            return;
        }
        if (searching_) {
            handle_search(key);
            return;
        }
        if (inspected_pid_) {
            handle_inspector(key);
            return;
        }
        switch (key) {
        case 'q':
        case 'Q':
            quit_ = true;
            break;
        case '/':
            searching_ = true;
            curs_set(1);
            break;
        case KEY_UP:
        case 'k':
            if (selected_ > 0) {
                --selected_;
            }
            break;
        case KEY_DOWN:
        case 'j':
            if (selected_ + 1 < displayed_.size()) {
                ++selected_;
            }
            break;
        case KEY_NPAGE:
            selected_ = std::min(selected_ + 10,
                                 displayed_.empty() ? std::size_t{0} : displayed_.size() - 1);
            break;
        case KEY_PPAGE:
            selected_ = selected_ > 10 ? selected_ - 10 : 0;
            break;
        case '\n':
        case KEY_ENTER:
            if (const auto process = selected_process()) {
                inspected_pid_ = process->pid;
                process_cpu_history_ = RingBuffer<double>{120};
                process_memory_history_ = RingBuffer<double>{120};
            }
            break;
        case 'c':
            sort_ = SortKey::Cpu;
            rebuild_processes();
            break;
        case 'm':
            sort_ = SortKey::Memory;
            rebuild_processes();
            break;
        case 'p':
            sort_ = SortKey::Pid;
            rebuild_processes();
            break;
        case 'n':
            sort_ = SortKey::Name;
            rebuild_processes();
            break;
        default:
            break;
        }
    }

    // Update the process filter interactively and preserve it when Escape closes input.
    void handle_search(int key) {
        if (key == 27 || key == '\n' || key == KEY_ENTER) {
            searching_ = false;
            curs_set(0);
            return;
        }
        if (key == KEY_BACKSPACE || key == 127 || key == 8) {
            if (!query_.empty()) {
                query_.pop_back();
                rebuild_processes();
            }
            return;
        }
        if (key >= 32 && key <= 126) {
            query_.push_back(static_cast<char>(key));
            rebuild_processes();
        }
    }

    // Keep destructive process actions behind a second explicit confirmation step.
    void handle_inspector(int key) {
        if (key == 27 || key == 'q' || key == 'Q' || key == KEY_BACKSPACE) {
            inspected_pid_.reset();
            status_.clear();
            return;
        }
        if (key == 't' || key == 'T') {
            confirm_signal_ = SIGTERM;
        } else if (key == 'i' || key == 'I') {
            confirm_signal_ = SIGINT;
        } else if (key == 'x' || key == 'X') {
            confirm_signal_ = SIGKILL;
        }
    }

    // Send a requested signal only after y and report kernel permission failures.
    void handle_confirmation(int key) {
        if (key == 'y' || key == 'Y') {
            errno = 0;
            if (inspected_pid_ && ::kill(*inspected_pid_, *confirm_signal_) == 0) {
                status_ = "Signal sent successfully.";
            } else {
                status_ = "Signal failed: " + std::string{std::strerror(errno)};
            }
            confirm_signal_.reset();
        } else if (key == 'n' || key == 'N' || key == 27) {
            status_ = "Signal cancelled.";
            confirm_signal_.reset();
        }
    }

    // Render either the table or inspector from the latest immutable snapshot.
    void draw() {
        erase();
        int height{};
        int width{};
        getmaxyx(stdscr, height, width);
        if (height < 18 || width < 60) {
            attron(A_BOLD);
            put(height / 2 - 1, std::max(0, (width - 31) / 2), "Pulse needs a 60 x 18 terminal.");
            attroff(A_BOLD);
            put(height / 2 + 1, std::max(0, (width - 24) / 2), "Resize the window or q to quit.");
            refresh();
            return;
        }
        if (inspected_pid_) {
            draw_inspector(height, width);
        } else {
            draw_dashboard(height, width);
        }
        refresh();
    }

    // Draw the responsive dashboard with histories, processes, and device summaries.
    void draw_dashboard(int height, int width) {
        if (terminal_.colors()) {
            attron(COLOR_PAIR(5) | A_BOLD);
        } else {
            attron(A_REVERSE | A_BOLD);
        }
        std::string title = " PULSE  " + monitor_.system_info().hostname;
        title.resize(static_cast<std::size_t>(width), ' ');
        put(0, 0, title, width);
        attrset(A_NORMAL);

        const auto cpu = snapshot_.cpu.empty() ? 0.0 : snapshot_.cpu.front().total_percent;
        std::string summary = "CPU " + fixed(cpu) + "%";
        if (snapshot_.memory) {
            summary += "   RAM " + format_bytes(snapshot_.memory->used_bytes) + " / " +
                       format_bytes(snapshot_.memory->total_bytes);
        }
        if (snapshot_.system) {
            summary += "   LOAD " + fixed(snapshot_.system->load_one, 2);
            summary += "   UP " + format_duration(std::chrono::seconds{
                                     static_cast<long long>(snapshot_.system->uptime_seconds)});
        }
        attron(A_BOLD);
        put(1, 1, summary, width - 2);
        attroff(A_BOLD);

        if (terminal_.colors()) {
            attron(COLOR_PAIR(cpu >= 90.0 ? 4 : cpu >= 70.0 ? 3 : 2));
        }
        put(2, 1, "CPU  " + graph(cpu_history_.values(), width - 7, 100.0), width - 2);
        if (snapshot_.memory) {
            const auto memory_percent = snapshot_.memory->total_bytes > 0
                                            ? 100.0 * static_cast<double>(snapshot_.memory->used_bytes) /
                                                  static_cast<double>(snapshot_.memory->total_bytes)
                                            : 0.0;
            put(3, 1, "MEM  " + graph(memory_history_.values(), width - 14, 100.0) + " " +
                           fixed(memory_percent) + "%",
                width - 2);
        }
        attrset(A_NORMAL);

        // Use the spare row for per-core values and aggregate time categories.
        if (!snapshot_.cpu.empty()) {
            std::string cores = "usr " + fixed(snapshot_.cpu.front().user_percent) + "% sys " +
                                fixed(snapshot_.cpu.front().system_percent) + "% io " +
                                fixed(snapshot_.cpu.front().iowait_percent) + "%   cores ";
            for (std::size_t index = 1; index < snapshot_.cpu.size(); ++index) {
                const auto core_name = snapshot_.cpu[index].name.starts_with("cpu")
                                           ? snapshot_.cpu[index].name.substr(3)
                                           : snapshot_.cpu[index].name;
                const auto value = core_name + ":" +
                                   std::to_string(static_cast<int>(std::round(
                                       snapshot_.cpu[index].total_percent))) +
                                   "% ";
                if (cores.size() + value.size() >= static_cast<std::size_t>(width - 2)) {
                    cores += "+" + std::to_string(snapshot_.cpu.size() - index) + " more";
                    break;
                }
                cores += value;
            }
            put(4, 1, cores, width - 2);
        }

        const int table_top = 5;
        const int footer_lines = height >= 26 ? 5 : 3;
        const int table_bottom = height - footer_lines - 1;
        draw_process_table(table_top, table_bottom, width);
        draw_devices(table_bottom + 1, height, width);
    }

    // Draw process columns that progressively add detail on wider terminals.
    void draw_process_table(int top, int bottom, int width) {
        attron(A_BOLD | (terminal_.colors() ? COLOR_PAIR(1) : 0));
        std::string header = " PID     CPU%   MEM%      MEM  PROCESS";
        if (width >= 100) {
            header = " PID     CPU%   MEM%      MEM  USER         S  THR  PROCESS";
        }
        put(top, 0, header, width);
        attroff(A_BOLD | (terminal_.colors() ? COLOR_PAIR(1) : 0));
        const auto rows = std::max(0, bottom - top - 1);
        if (selected_ < scroll_) {
            scroll_ = selected_;
        }
        if (selected_ >= scroll_ + static_cast<std::size_t>(rows) && rows > 0) {
            scroll_ = selected_ - static_cast<std::size_t>(rows) + 1;
        }
        for (int row = 0; row < rows; ++row) {
            const auto index = scroll_ + static_cast<std::size_t>(row);
            if (index >= displayed_.size()) {
                break;
            }
            const auto& process = displayed_[index];
            std::ostringstream line;
            line << ' ' << std::setw(6) << process.pid << ' ' << std::setw(6) << std::fixed
                 << std::setprecision(1) << process.cpu_percent << ' ' << std::setw(6)
                 << process.memory_percent << ' ' << std::setw(9) << format_bytes(process.resident_bytes, 0)
                 << "  ";
            if (width >= 100) {
                line << std::left << std::setw(12) << process.user.substr(0, 11) << std::right
                     << process.state << ' ' << std::setw(4) << process.threads << "  ";
            }
            line << process.name;
            if (index == selected_) {
                attron(A_REVERSE);
            }
            put(top + 1 + row, 0, line.str(), width);
            if (index == selected_) {
                attroff(A_REVERSE);
            }
        }
    }

    // Summarize network, disk, filesystem, temperature, and active controls.
    void draw_devices(int top, int height, int width) {
        const auto interface = busiest_interface(snapshot_.network);
        const auto disk = busiest_disk(snapshot_.disks);
        const auto down = interface ? interface->receive_bytes_per_second : 0.0;
        const auto up = interface ? interface->transmit_bytes_per_second : 0.0;
        const auto reads = disk ? disk->read_bytes_per_second : 0.0;
        const auto writes = disk ? disk->write_bytes_per_second : 0.0;
        std::string line = "NET " + (interface ? interface->name : std::string{"-"}) + " down " +
                           format_bytes(static_cast<std::uint64_t>(down)) +
                           "/s  up " + format_bytes(static_cast<std::uint64_t>(up)) + "/s";
        line += "    DISK " + (disk ? disk->name : std::string{"-"}) + " read " +
                format_bytes(static_cast<std::uint64_t>(reads)) +
                "/s  write " + format_bytes(static_cast<std::uint64_t>(writes)) + "/s";
        put(top, 1, line, width - 2);
        if (height - top >= 5) {
            const auto net_max = std::max(1.0, std::max(down, up));
            put(top + 1, 1, "NET  " + graph(download_history_.values(), (width - 8) / 2, net_max) +
                                "  " + graph(upload_history_.values(), (width - 8) / 2, net_max),
                width - 2);
            std::string storage;
            if (!snapshot_.filesystems.empty()) {
                const auto root = std::ranges::find(snapshot_.filesystems, std::string{"/"},
                                                    &FilesystemUsage::mount_point);
                const auto& mount = root != snapshot_.filesystems.end() ? *root
                                                                        : snapshot_.filesystems.front();
                storage = mount.mount_point + " " + format_bytes(mount.used_bytes) + " / " +
                          format_bytes(mount.total_bytes) + " (" + fixed(mount.used_percent) + "%)";
            }
            if (!snapshot_.temperatures.empty()) {
                const auto hottest = std::ranges::max_element(snapshot_.temperatures, {},
                                                              &Temperature::celsius);
                storage += "    TEMP " + fixed(hottest->celsius) + " C " + hottest->label;
            }
            put(top + 2, 1, storage, width - 2);
        }
        const std::string search = query_.empty() ? "" : "  FILTER: " + query_;
        const std::string controls = searching_
                                         ? "/ Search: " + query_ + "   Enter/Esc finish"
                                         : "Up/Down Navigate  / Search  Enter Inspect  c/m/p/n Sort [" +
                                               std::string{sort_name(sort_)} + "]  q Quit" + search;
        attron(A_REVERSE);
        std::string footer = " " + controls;
        footer.resize(static_cast<std::size_t>(width), ' ');
        put(height - 1, 0, footer, width);
        attroff(A_REVERSE);
        if (searching_) {
            const auto cursor = std::min(width - 1, 11 + static_cast<int>(query_.size()));
            move(height - 1, cursor);
        }
    }

    // Draw all accessible detail for one process plus bounded personal histories.
    void draw_inspector(int height, int width) {
        const auto process = find_process(*inspected_pid_);
        if (!process) {
            put(2, 2, "Process " + std::to_string(*inspected_pid_) + " has exited.");
            put(height - 1, 0, " Esc Back", width);
            return;
        }
        attron(A_REVERSE | A_BOLD);
        std::string title = " PROCESS INSPECTOR  " + process->name;
        title.resize(static_cast<std::size_t>(width), ' ');
        put(0, 0, title, width);
        attrset(A_NORMAL);

        put(2, 2, "PID          " + std::to_string(process->pid));
        put(3, 2, "PPID         " + std::to_string(process->parent_pid));
        put(4, 2, "User         " + process->user);
        put(5, 2, "State        " + std::string{state_name(process->state)} + " (" +
                       process->state + ")");
        put(6, 2, "Threads      " + std::to_string(process->threads));
        put(7, 2, "Runtime      " + format_duration(std::chrono::seconds{
                                      static_cast<long long>(process->runtime_seconds)}));
        put(2, width / 2, "CPU          " + fixed(process->cpu_percent) + "%");
        put(3, width / 2,
            "Memory       " + format_bytes(process->resident_bytes) + " (" +
                fixed(process->memory_percent) + "%)");

        attron(A_BOLD);
        put(9, 2, "Command");
        attroff(A_BOLD);
        put(10, 2, process->command, width - 4);
        const int graph_width = width - 17;
        put(12, 2, "CPU history  " + graph(process_cpu_history_.values(), graph_width, 100.0), width - 4);
        put(14, 2,
            "MEM history  " + graph(process_memory_history_.values(), graph_width,
                                      std::max(1.0, process->memory_percent * 1.25)),
            width - 4);
        if (!status_.empty()) {
            put(16, 2, status_, width - 4);
        }
        if (confirm_signal_) {
            const auto name = *confirm_signal_ == SIGTERM
                                  ? "SIGTERM"
                                  : *confirm_signal_ == SIGINT ? "SIGINT" : "SIGKILL";
            attron(A_BOLD | (terminal_.colors() ? COLOR_PAIR(4) : 0));
            put(height - 3, 2, "Send " + std::string{name} + " to PID " +
                                   std::to_string(process->pid) + "? y/N",
                width - 4);
            attrset(A_NORMAL);
        }
        attron(A_REVERSE);
        std::string footer = " Esc Back   t SIGTERM   i SIGINT   x SIGKILL (confirmation required)";
        footer.resize(static_cast<std::size_t>(width), ' ');
        put(height - 1, 0, footer, width);
        attroff(A_REVERSE);
    }

    Monitor& monitor_;
    TerminalSession terminal_;
    MonitorSnapshot snapshot_;
    std::vector<ProcessInfo> displayed_;
    RingBuffer<double> cpu_history_;
    RingBuffer<double> memory_history_;
    RingBuffer<double> download_history_;
    RingBuffer<double> upload_history_;
    RingBuffer<double> process_cpu_history_;
    RingBuffer<double> process_memory_history_;
    SortKey sort_{SortKey::Cpu};
    std::size_t selected_{};
    std::size_t scroll_{};
    std::string query_;
    std::string status_;
    std::optional<Pid> inspected_pid_;
    std::optional<int> confirm_signal_;
    bool searching_{};
    bool quit_{};
};

} // namespace

int run_tui(Monitor& monitor, bool use_color) {
    Tui tui{monitor, use_color};
    return tui.run();
}

} // namespace pulse
