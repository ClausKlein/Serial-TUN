cmake_minimum_required(VERSION 3.11)
project(serial_tun LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -Wpedantic")

find_package(Threads REQUIRED)
find_library(SerialPort_lib serialport)
find_file(SerialPort_header libserialport.h)

if(SerialPort_lib)
    set(SOURCE_FILES main.c tun-driver.cpp tun-driver.h ${SerialPort_header} slip.cpp slip.h)
    add_executable(serial_tun ${SOURCE_FILES})
    target_link_libraries(serial_tun ${SerialPort_lib} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(simpletap simpletap.cpp tun-driver.cpp tun-driver.h)
target_link_libraries(simpletap ${CMAKE_THREAD_LIBS_INIT})
set_target_properties(simpletap PROPERTIES CXX_STANDARD 14)
add_test(simpletap0 COMMAND ./simpletap -d /dev/XXX -i tap0 -p)
add_test(simpletapPipe COMMAND ./simpletap -d /dev/XXX -p)

if(LINUX)
    add_executable(simpletun simpletun.cpp)
endif()

enable_testing()

find_package(doctest 2.3.4 REQUIRED)

add_executable(test_slip test_slip.cpp slip.cpp slip.h)
target_link_libraries(test_slip PRIVATE doctest::doctest)
set_target_properties(test_slip PROPERTIES CXX_STANDARD 14)
add_test(test_slip test_slip)

list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/Workspace/cmake)
list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/cmake)
option(USE_LCOV "Use GNU Test Coverage framework" OFF)
if(USE_LCOV)
    set(CMAKE_BUILD_TYPE "Coverage" CACHE STRING "build for lcov" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    include(CodeCoverage)

    set(COVERAGE_EXCLUDES '/usr/local/*' '/Applications/Xcode.app/*' '/opt/local/*')
    setup_target_for_coverage(
         NAME lcov      # NAME for custom target.
         EXECUTABLE ctest     # EXECUTABLE of the test driver executable that runs the tests.
                   # NOTE! This should always have a ZERO as exit code
                   # otherwise the coverage generation will not complete.
        #XXX coverage   # Name of output directory.
    )
endif() #CMAKE_BUILD_TYPE STREQUAL "Coverage"

