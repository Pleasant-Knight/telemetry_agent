cmake_minimum_required(VERSION 3.16)

# Check for Ninja and suggest using it for faster builds
find_program(NINJA_EXECUTABLE ninja)
if(NINJA_EXECUTABLE)
    message(STATUS "Ninja found: ${NINJA_EXECUTABLE}")
    message(STATUS "For faster builds, consider running: cmake -GNinja ..")
else()
    message(STATUS "Ninja not found. Install Ninja for faster builds: sudo apt install ninja-build")
endif()

project(telemetry_agent VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build optimizations
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add compiler warnings and optimizations
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Add optimization flags for release builds
    add_compile_options($<$<CONFIG:Release>:-O3>)
    add_compile_options($<$<CONFIG:Release>:-march=native>)
endif()

# Collect all src files into a library
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

set(CLI_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/telemetry_agent_cli.cpp")
list(REMOVE_ITEM SRC_FILES "${CLI_SOURCE}")

add_library(telemetry_agent STATIC ${SRC_FILES})

target_include_directories(telemetry_agent
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# CLI executable
add_executable(telemetry_agent_cli "${CLI_SOURCE}")
target_link_libraries(telemetry_agent_cli PRIVATE telemetry_agent)

# Create a test executable for each cpp under tests/
file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

set(TEST_TARGETS "")
foreach(test_src IN LISTS TEST_FILES)
    get_filename_component(test_name "${test_src}" NAME_WE)
    add_executable("${test_name}" "${test_src}")
    target_link_libraries("${test_name}" PRIVATE telemetry_agent)
    list(APPEND TEST_TARGETS "${test_name}")
endforeach()

# Enable testing
enable_testing()

# Add tests
foreach(test_name IN LISTS TEST_TARGETS)
    add_test(NAME "${test_name}" COMMAND "${test_name}")
endforeach()

# Optional: Add a custom target to run tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${TEST_TARGETS}
)

# Create a benchmark executable for each cpp under bench/
file(GLOB_RECURSE BENCH_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp"
)

set(BENCH_TARGETS "")
foreach(bench_src IN LISTS BENCH_FILES)
    get_filename_component(bench_name "${bench_src}" NAME_WE)
    add_executable("${bench_name}" "${bench_src}")
    target_link_libraries("${bench_name}" PRIVATE telemetry_agent)
    list(APPEND BENCH_TARGETS "${bench_name}")
endforeach()

add_custom_target(run_bench
    DEPENDS ${BENCH_TARGETS}
)
