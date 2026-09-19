#include "pulse/collectors/network.hpp"
#include "test.hpp"

TEST_CASE(network_parser_reads_variable_interface_names) {
    const auto interfaces = pulse::parse_net_dev(test::fixture("net_dev.txt"));
    REQUIRE(interfaces.has_value());
    CHECK(interfaces->size() == 2);
    CHECK(interfaces->at(0).name == "enp5s0");
    CHECK(interfaces->at(1).transmitted_bytes == 8192);
}
