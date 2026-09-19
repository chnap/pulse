#include "pulse/collectors/cpu.hpp"
#include "test.hpp"

#include <cmath>

TEST_CASE(cpu_parser_reads_aggregate_and_cores) {
    const auto sample = pulse::parse_cpu_stat(test::fixture("proc_stat.txt"));
    REQUIRE(sample.has_value());
    CHECK(sample->counters.size() == 3);
    CHECK(sample->counters.front().total() == 979);
}

TEST_CASE(cpu_usage_uses_counter_deltas) {
    const auto first = pulse::parse_cpu_stat("cpu 100 0 50 850 0 0 0 0\n");
    const auto second = pulse::parse_cpu_stat("cpu 140 0 70 890 0 0 0 0\n");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const auto usage = pulse::calculate_cpu_usage(*first, *second);
    CHECK(usage.size() == 1);
    CHECK(std::abs(usage.front().total_percent - 60.0) < 0.001);
    CHECK(std::abs(usage.front().user_percent - 40.0) < 0.001);
}

TEST_CASE(cpu_parser_rejects_malformed_aggregate) {
    CHECK(!pulse::parse_cpu_stat("cpu bad data\n").has_value());
}
