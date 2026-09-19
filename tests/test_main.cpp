#include "test.hpp"

int main() {
    for (const auto& test_case : test::cases()) {
        test_case.function();
    }
    if (test::failures == 0) {
        std::cout << test::cases().size() << " tests passed\n";
    }
    return test::failures == 0 ? 0 : 1;
}
