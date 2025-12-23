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
set(CMAKE_CXX_STANDARD 17)
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

# Create the RollingWindow library
add_library(rolling_window STATIC
    src/rolling_window.cpp
    src/hysteresis_fsm.cpp
    include/hysteresis_fsm.hpp
)

target_include_directories(rolling_window
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Create the test executable
add_executable(test_rolling_window
    tests/test_rolling_window.cpp
)

target_link_libraries(test_rolling_window
    PRIVATE
        rolling_window
)

# Enable testing
enable_testing()

# Add test
add_test(
    NAME rolling_window_tests
    COMMAND test_rolling_window
)

# Optional: Add a custom target to run tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_rolling_window
)
