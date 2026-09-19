# Pulse

[![CI](https://github.com/chnap/pulse/actions/workflows/ci.yml/badge.svg)](https://github.com/chnap/pulse/actions/workflows/ci.yml)

Pulse is a lightweight terminal system monitor for Linux, built with C++20 and ncurses.

It reads kernel data directly from interfaces such as `/proc` and `/sys` and turns it into a responsive dashboard for CPU, memory, processes, network activity, storage, and temperatures. Metrics stay on the local machine: Pulse does not invoke monitoring commands, send telemetry, or make network requests during normal operation.

## Why I built Pulse

I built Pulse while learning C++, Linux, and systems programming. I wanted a project that went beyond small exercises and required me to understand how Linux exposes real system information, how applications calculate rates from cumulative counters, and how to keep an interactive terminal interface responsive.

The code is intentionally structured so I can keep improving it as I learn. It uses modern C++ where it makes the implementation clearer, while keeping the collectors and parsing logic straightforward enough to study.

## Screenshot / demo

A screenshot will be added here after the first public release. The repository does not include a generated or placeholder screenshot.

## Features

- Aggregate and per-core CPU usage calculated from counter differences
- RAM, cache, and swap usage based on Linux `MemAvailable`
- Sortable process table with CPU, memory, user, state, and thread data
- Interactive process search by PID, name, or command line
- Process inspector with bounded CPU and memory histories
- Confirmed `SIGTERM`, `SIGINT`, and `SIGKILL` actions using normal user permissions
- Per-interface network rates and block-device I/O rates
- Filesystem capacity with virtual filesystems filtered out
- Dynamic thermal and hwmon sensor discovery
- Responsive ncurses UI with a safe small-terminal fallback
- Non-interactive, threshold-based `pulse doctor` report
- Clean worker shutdown using C++20 `std::jthread` and `std::stop_token`

## Requirements

Pulse currently targets Linux. Building requires:

- A C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.20 or newer
- ncurses development headers and library
- POSIX threads

On Ubuntu or Debian:

```bash
sudo apt update
sudo apt install build-essential cmake libncurses-dev
```

## Build from source

```bash
git clone https://github.com/chnap/pulse.git
cd pulse
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/pulse
```

Install the binary using the prefix selected by CMake:

```bash
sudo cmake --install build
pulse
```

## Usage

```text
pulse
pulse --refresh 1000
pulse --no-color
pulse doctor
pulse --help
pulse --version
```

Refresh intervals are specified in milliseconds and must be between 100 and 10000. Interactive mode requires a terminal; `pulse doctor` is intended for scripts and plain-text output.

## Keyboard controls

| Key | Action |
| --- | --- |
| `Up` / `Down`, `k` / `j` | Move through processes |
| `Page Up` / `Page Down` | Move ten rows |
| `/` | Search processes interactively |
| `Enter` | Inspect the selected process |
| `c` / `m` / `p` / `n` | Sort by CPU, memory, PID, or name |
| `Esc` | Leave search or the process inspector |
| `t` / `i` / `x` | Request SIGTERM, SIGINT, or SIGKILL in the inspector |
| `y` / `n` | Confirm or cancel a requested signal |
| `q` | Quit |

Process signaling never bypasses Linux permissions. Every signal requires confirmation, and SIGKILL is not the default action.

## Architecture

Pulse keeps collection and presentation separate:

```text
Linux collectors -> metric models -> synchronized monitor snapshot -> ncurses UI
```

Collectors own parsing and delta state. A single background `std::jthread` samples metrics and publishes a consistent snapshot under a short mutex. The main thread handles terminal input, process filtering and sorting, histories, and rendering. The UI never reads `/proc` or `/sys` directly.

This intentionally small concurrency model keeps terminal input responsive without creating one polling thread per metric. Filesystems and temperatures are sampled less often than CPU, processes, network, and disk activity.

## How it works

| Metric | Linux source |
| --- | --- |
| CPU | `/proc/stat` |
| Memory and swap | `/proc/meminfo` |
| Processes | `/proc/<pid>/stat`, `status`, and `cmdline` |
| Load and uptime | `/proc/loadavg` and `/proc/uptime` |
| Network | `/proc/net/dev` |
| Disk I/O | `/proc/diskstats` and `/sys/class/block` |
| Temperature | `/sys/class/thermal` and `/sys/class/hwmon` |
| Filesystems | `/proc/mounts` and `statvfs()` |
| Host and kernel | `gethostname()`, `uname()`, and `/etc/os-release` |

CPU percentages, process CPU percentages, network rates, and disk rates cannot be read as instantaneous values. Pulse stores consecutive cumulative counter samples and divides their differences by the elapsed sampling interval. The first sample establishes a baseline and reports zero rather than inventing a rate.

Processes can exit between directory enumeration and file reads. Missing files, permission errors, malformed samples, counter resets, and PID reuse are handled as normal conditions. PID reuse is detected with the process start-time field.

## Doctor mode

`pulse doctor` takes two short samples and prints a plain-text report. Its warnings are observations based on fixed thresholds:

- CPU usage at or above 90%
- available memory below 10%
- swap usage at or above 75%
- filesystem usage at or above 90%
- a reported temperature at or above 85 C

Temperature readings depend on the sensors exposed by the machine. Doctor mode does not claim to diagnose hardware failures.
The command exits with status 1 when it reports one or more warnings and status 0 otherwise.

## Development

Build and run the tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Parser tests use checked-in fixtures rather than relying on the developer's live `/proc` filesystem.

Format or check all C++ sources when `clang-format` is installed:

```bash
cmake --build build --target format
cmake --build build --target format-check
```

Enable clang-tidy during compilation:

```bash
cmake -S . -B build-tidy -DPULSE_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy -j
```

Build with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DPULSE_ENABLE_SANITIZERS=ON
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
```

## Roadmap

- Add JSON recording and offline report commands
- Add configurable thresholds and process-table columns
- Add cgroup and container context where Linux exposes it safely
- Expand storage and sensor fixtures for more hardware layouts

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the build, formatting, testing, and pull request guidelines.

## License

Pulse is available under the MIT License. See [LICENSE](LICENSE).
