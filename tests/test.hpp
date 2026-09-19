#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {

using Function = std::function<void()>;

struct Case {
    std::string name;
    Function function;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> registered;
    return registered;
}

struct Registrar {
    Registrar(std::string name, Function function) {
        cases().push_back({std::move(name), std::move(function)});
    }
};

inline int failures = 0;

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        ++failures;
        std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    }
}

} // namespace test

#define TEST_CASE(name)                                                                            \
    static void name();                                                                            \
    static test::Registrar name##_registrar{#name, name};                                           \
    static void name()

#define CHECK(expression) test::check((expression), #expression, __FILE__, __LINE__)

