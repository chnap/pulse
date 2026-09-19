#include "pulse/collectors/cpu.hpp"
#include "test.hpp"

#include <cmath>

TEST_CASE(cpu_parser_reads_aggregate_and_cores) {
    const auto sample = pulse::parse_cpu_stat(
        "cpu  100 10 40 800 20 2 3 4 0 0\n"
        "cpu0 60 5 20 390 10 1 2 2 0 0\n"
        "cpu1 40 5 20 410 10 1 1 2 0 0\n"
        "intr 123\n");
    CHECK(sample.has_value());
    CHECK(sample->counters.size() == 3);
    CHECK(sample->counters.front().total() == 979);
}

TEST_CASE(cpu_usage_uses_counter_deltas) {
    const auto first = pulse::parse_cpu_stat("cpu 100 0 50 850 0 0 0 0\n");
    const auto second = pulse::parse_cpu_stat("cpu 140 0 70 890 0 0 0 0\n");
    CHECK(first.has_value());
    CHECK(second.has_value());
    const auto usage = pulse::calculate_cpu_usage(*first, *second);
    CHECK(usage.size() == 1);
    CHECK(std::abs(usage.front().total_percent - 60.0) < 0.001);
    CHECK(std::abs(usage.front().user_percent - 40.0) < 0.001);
}

TEST_CASE(cpu_parser_rejects_malformed_aggregate) {
    CHECK(!pulse::parse_cpu_stat("cpu bad data\n").has_value());
}

