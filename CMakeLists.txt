cmake_minimum_required(VERSION 3.11)
project(serial_tun LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -Wpedantic")

find_package(Threads REQUIRED)
find_library(SerialPort_lib serialport)
find_file(SerialPort_header libserialport.h)

if(SerialPort_lib)
    set(SOURCE_FILES main.c tun-driver.c tun-driver.h ${SerialPort_header} slip.c slip.h)
    add_executable(serial_tun ${SOURCE_FILES})
    target_link_libraries(serial_tun ${SerialPort_lib} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(simpletap simpletap.cpp tun-driver.c tun-driver.h)
target_link_libraries(simpletap ${CMAKE_THREAD_LIBS_INIT})

if(LINUX)
    add_executable(simpletun simpletun.cpp)
endif()

enable_testing()

find_package(doctest 2.3.4 REQUIRED)

add_executable(test_slip test_slip.cpp slip.c slip.h)
target_link_libraries(test_slip PRIVATE doctest::doctest)
set_target_properties(test_slip PROPERTIES CXX_STANDARD 17)
add_test(test_slip test_slip)

list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/Workspace/cmake)
option(USE_LCOV "Use GNU Test Coverage framework" OFF)
if(USE_LCOV)
    set(CMAKE_BUILD_TYPE "Coverage" CACHE STRING "build for lcov" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    include(CodeCoverage)

    set(COVERAGE_EXCLUDES '/usr/local/*' '/opt/local/*')
    setup_target_for_coverage(
         lcov      # NAME for custom target.
         ctest     # EXECUTABLE of the test driver executable that runs the tests.
                   # NOTE! This should always have a ZERO as exit code
                   # otherwise the coverage generation will not complete.
        coverage   # Name of output directory.
    )
endif() #CMAKE_BUILD_TYPE STREQUAL "Coverage"

