#include "pulse/utils/format.hpp"
#include "pulse/utils/ring_buffer.hpp"
#include "test.hpp"

#include <chrono>

TEST_CASE(byte_formatter_selects_binary_units) {
    CHECK(pulse::format_bytes(842ULL * 1024ULL) == "842 KiB");
    CHECK(pulse::format_bytes(6ULL * 1024ULL * 1024ULL * 1024ULL) == "6.0 GiB");
}

TEST_CASE(duration_formatter_includes_days) {
    CHECK(pulse::format_duration(std::chrono::seconds{90061}) == "1d 01:01:01");
}

TEST_CASE(ring_buffer_discards_oldest_value) {
    pulse::RingBuffer<int> values{2};
    values.push(1);
    values.push(2);
    values.push(3);
    CHECK(values.size() == 2);
    CHECK(values.values().front() == 2);
    CHECK(values.back() == 3);
}
