cmake_minimum_required(VERSION 3.20)

project(Pulse VERSION 0.1.0 DESCRIPTION "A terminal system monitor for Linux" LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

option(PULSE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PULSE_ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PULSE_ENABLE_CLANG_TIDY)
    find_program(PULSE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${PULSE_CLANG_TIDY_EXECUTABLE}")
endif()

configure_file(cmake/version.hpp.in generated/pulse/version.hpp @ONLY)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

find_program(PULSE_CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(PULSE_CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE PULSE_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
    )
    add_custom_target(format
        COMMAND "${PULSE_CLANG_FORMAT_EXECUTABLE}" -i ${PULSE_FORMAT_FILES}
        COMMENT "Formatting Pulse sources"
    )
    add_custom_target(format-check
        COMMAND "${PULSE_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${PULSE_FORMAT_FILES}
        COMMENT "Checking Pulse source formatting"
    )
endif()

add_library(pulse_core
    src/collectors/cpu.cpp
    src/collectors/memory.cpp
    src/collectors/process.cpp
    src/collectors/network.cpp
    src/collectors/storage.cpp
    src/collectors/temperature.cpp
    src/app/monitor.cpp
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

add_executable(pulse
    src/main.cpp
    src/app/doctor.cpp
    src/ui/tui.cpp
)
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
    target_compile_definitions(pulse_tests PRIVATE
        PULSE_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    add_test(NAME pulse_tests COMMAND pulse_tests)
endif()
