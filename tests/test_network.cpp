#include "pulse/collectors/network.hpp"
#include "test.hpp"

TEST_CASE(network_parser_reads_variable_interface_names) {
    const auto interfaces = pulse::parse_net_dev(
        "Inter-| Receive | Transmit\n"
        " face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed\n"
        "  enp5s0: 1024 1 0 0 0 0 0 0 2048 2 0 0 0 0 0 0\n"
        "wlp2s0.10: 4096 3 0 0 0 0 0 0 8192 4 0 0 0 0 0 0\n");
    CHECK(interfaces.has_value());
    CHECK(interfaces->size() == 2);
    CHECK(interfaces->at(0).name == "enp5s0");
    CHECK(interfaces->at(1).transmitted_bytes == 8192);
}
