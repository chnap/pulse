cmake_minimum_required(VERSION 3.20)

project(Pulse VERSION 0.1.0 DESCRIPTION "A terminal system monitor for Linux" LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

option(PULSE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_file(cmake/version.hpp.in generated/pulse/version.hpp @ONLY)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

add_library(pulse_core
    src/collectors/cpu.cpp
    src/collectors/memory.cpp
    src/collectors/process.cpp
    src/collectors/network.cpp
    src/collectors/storage.cpp
    src/collectors/temperature.cpp
    src/collectors/system.cpp
    src/utils/format.cpp
)

target_include_directories(pulse_core
    PUBLIC
        include
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_options(pulse_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

add_executable(pulse src/main.cpp)
target_link_libraries(pulse PRIVATE pulse_core Threads::Threads ${CURSES_LIBRARIES})
target_include_directories(pulse PRIVATE ${CURSES_INCLUDE_DIRS})
target_compile_options(pulse PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

if(PULSE_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pulse_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pulse_core PUBLIC -fsanitize=address,undefined)
endif()

install(TARGETS pulse RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    add_executable(pulse_tests
        tests/test_main.cpp
        tests/test_cpu.cpp
        tests/test_memory.cpp
        tests/test_process.cpp
        tests/test_network.cpp
        tests/test_format.cpp
    )
    target_link_libraries(pulse_tests PRIVATE pulse_core)
    target_include_directories(pulse_tests PRIVATE tests)
    add_test(NAME pulse_tests COMMAND pulse_tests)
endif()
