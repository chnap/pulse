#include "pulse/collectors/process.hpp"
#include "test.hpp"

TEST_CASE(process_stat_parser_handles_spaces_and_parentheses) {
    const auto stat = pulse::parse_process_stat(
        "4821 (worker (main)) S 1820 1 2 3 4 5 6 7 8 9 100 25 12 13 14 15 37 18 5000 20 256");
    CHECK(stat.has_value());
    CHECK(stat->pid == 4821);
    CHECK(stat->name == "worker (main)");
    CHECK(stat->parent_pid == 1820);
    CHECK(stat->user_ticks == 100);
    CHECK(stat->system_ticks == 25);
    CHECK(stat->threads == 37);
    CHECK(stat->start_time_ticks == 5000);
    CHECK(stat->resident_pages == 256);
}

TEST_CASE(process_stat_parser_rejects_truncated_input) {
    CHECK(!pulse::parse_process_stat("12 (short) S 1 2").has_value());
}
