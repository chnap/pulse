#include "pulse/collectors/memory.hpp"
#include "test.hpp"

TEST_CASE(memory_parser_uses_mem_available) {
    const auto memory = pulse::parse_meminfo(test::fixture("meminfo.txt"));
    REQUIRE(memory.has_value());
    CHECK(memory->used_bytes == 600000ULL * 1024ULL);
    CHECK(memory->cached_bytes == 210000ULL * 1024ULL);
    CHECK(memory->swap_used_bytes == 50000ULL * 1024ULL);
}

TEST_CASE(memory_parser_requires_core_fields) {
    CHECK(!pulse::parse_meminfo("MemTotal: 100 kB\n").has_value());
}
