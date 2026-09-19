#include "pulse/collectors/memory.hpp"
#include "test.hpp"

TEST_CASE(memory_parser_uses_mem_available) {
    const auto memory = pulse::parse_meminfo(
        "MemTotal:       1000000 kB\n"
        "MemFree:         100000 kB\n"
        "MemAvailable:    400000 kB\n"
        "Cached:          200000 kB\n"
        "SReclaimable:     10000 kB\n"
        "SwapTotal:       200000 kB\n"
        "SwapFree:        150000 kB\n");
    CHECK(memory.has_value());
    CHECK(memory->used_bytes == 600000ULL * 1024ULL);
    CHECK(memory->cached_bytes == 210000ULL * 1024ULL);
    CHECK(memory->swap_used_bytes == 50000ULL * 1024ULL);
}

TEST_CASE(memory_parser_requires_core_fields) {
    CHECK(!pulse::parse_meminfo("MemTotal: 100 kB\n").has_value());
}

