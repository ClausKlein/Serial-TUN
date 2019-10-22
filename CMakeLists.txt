cmake_minimum_required(VERSION 3.13.1)
project(serial_tun LANGUAGES C CXX)

#---------------------------------------------------------------------------------------
# Compiler config
#---------------------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_FLAGS "-Wall -Wextra -Wpedantic")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS ${CMAKE_C_FLAGS})

list(APPEND CMAKE_MODULE_PATH /usr/local/lib/cmake)

find_package(Threads REQUIRED)
find_library(SerialPort_lib serialport)
find_file(SerialPort_header libserialport.h)

if(SerialPort_lib)
    set(SOURCE_FILES main.c tun-driver.cpp tun-driver.h ${SerialPort_header} slip.cpp slip.h)
    add_executable(serial_tun ${SOURCE_FILES})
    target_link_libraries(serial_tun ${SerialPort_lib} ${CMAKE_THREAD_LIBS_INIT})
endif()

if(LINUX)
    add_executable(simpletun simpletun.cpp)
endif()

#---------------------------------------------------------------------------------------
# Set SERIAL_TUN_MASTER_PROJECT to ON if we are building Serial-TUN
#---------------------------------------------------------------------------------------
# Check if Serial-TUN is being used directly or via add_subdirectory, but allow overriding
if(NOT SERIAL_TUN_MASTER_PROJECT)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        message(STATUS "${CMAKE_CURRENT_SOURCE_DIR} == ${CMAKE_SOURCE_DIR} => SERIAL_TUN_MASTER_PROJECT")
        set(SERIAL_TUN_MASTER_PROJECT ON)
    else()
        set(SERIAL_TUN_MASTER_PROJECT OFF)
    endif()
endif()

include(FetchContent)
if(CMAKE_VERSION VERSION_LESS "3.14")
    function(fetchcontent_makeavailable name)
        fetchcontent_getproperties(${name})
        if(NOT ${name}_POPULATED)
            fetchcontent_populate(${name})
            add_subdirectory(${${name}_SOURCE_DIR} ${${name}_BINARY_DIR})
        endif()
    endfunction()
endif()

if(SERIAL_TUN_MASTER_PROJECT)
    find_package(spdlog 1.4.2)
    if(NOT spdlog_DIR)
        fetchcontent_declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG        v1.4.2
        )
        fetchcontent_makeavailable(spdlog)
    endif()
else()
    find_package(spdlog 1.4.2 REQUIRED)
endif()

add_executable(simpletap simpletap.cpp tun-driver.cpp tun-driver.h)
target_link_libraries(simpletap PRIVATE spdlog::spdlog ${CMAKE_THREAD_LIBS_INIT})
set_target_properties(simpletap PROPERTIES CXX_STANDARD 14)

# testing options
option(SERIAL_TUN_BUILD_TESTS "Build tests" ${SERIAL_TUN_MASTER_PROJECT})
if(SERIAL_TUN_BUILD_TESTS)
    enable_testing()

    find_package(doctest 2.3.5)
    if(NOT doctest_DIR)
        fetchcontent_declare(
            doctest
            GIT_REPOSITORY https://github.com/onqtam/doctest.git
            GIT_TAG        2.3.5
        )
        fetchcontent_makeavailable(doctest)
    endif()

    add_test(NAME simpletap0 COMMAND simpletap -d /dev/XXX -i tap0 -p)
    add_test(NAME simpletapPipe COMMAND simpletap -d /dev/XXX -p)

    add_executable(test_slip test_slip.cpp slip.cpp slip.h)
    target_link_libraries(test_slip PRIVATE doctest::doctest)
    set_target_properties(test_slip PROPERTIES CXX_STANDARD 14)
    add_test(test_slip test_slip)


    # test coverage options
    list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/Workspace/cmake)
    list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/cmake)
    option(USE_LCOV "Use GNU Test Coverage framework" OFF)
    if(USE_LCOV)
        set(CMAKE_BUILD_TYPE "Coverage" CACHE STRING "build for lcov" FORCE)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        include(CodeCoverage)

        set(COVERAGE_EXCLUDES
            '${FETCHCONTENT_BASE_DIR}/*' '/usr/local/*'
            '/Applications/Xcode.app/*' '/opt/local/*'
        )
        setup_target_for_coverage(
            NAME lcov   # NAME for custom target.
            EXECUTABLE ctest    # EXECUTABLE of the test driver executable that runs the tests.
                        # NOTE! This should always have a ZERO as exit code
                        # otherwise the coverage generation will not complete.
            #NO! coverage   # Name of output directory.
        )
    endif() #CMAKE_BUILD_TYPE STREQUAL "Coverage"
endif()
