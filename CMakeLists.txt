cmake_minimum_required(VERSION 3.13.1)

list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/Workspace/cmake)
list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/cmake)
list(APPEND CMAKE_MODULE_PATH /usr/local/lib/cmake)


include(before_project_setup OPTIONAL)
# ===============================
project(serial_tun LANGUAGES CXX)
# ===============================
include(build_options OPTIONAL)


#---------------------------------------------------------------------------------------
# Compiler config
#---------------------------------------------------------------------------------------
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wpedantic")
endif()


#---------------------------------------------------------------------------------------
# search required packages and libs
#---------------------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_library(SerialPort_lib serialport)
find_file(SerialPort_header libserialport.h)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Coverage")
    if(SerialPort_lib)
        set(SOURCE_FILES serial_tun.cpp tun-driver.cpp tun-driver.h ${SerialPort_header} slip.cpp slip.h)
        add_executable(serial_tun ${SOURCE_FILES})
        target_link_libraries(serial_tun ${SerialPort_lib} ${CMAKE_THREAD_LIBS_INIT})
    endif()

    if(LINUX)
        add_executable(simpletun simpletun.cpp)
    endif()
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
            include_directories(${${name}_SOURCE_DIR}/include)
        endif()
    endfunction()
endif()


if(SERIAL_TUN_MASTER_PROJECT)
    find_package(spdlog 1.4.2)
    if (NOT TARGET spdlog::spdlog_header_only)
        fetchcontent_declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG        v1.4.2
        )
        fetchcontent_makeavailable(spdlog)
    endif()
endif()
if (NOT TARGET spdlog::spdlog_header_only)
    find_package(spdlog 1.4.2 REQUIRED)
endif()


add_executable(simpletap simpletap.cpp tun-lib.cpp tun-driver.cpp tun-driver.h)
target_link_libraries(simpletap PRIVATE spdlog::spdlog_header_only ${CMAKE_THREAD_LIBS_INIT})
set_target_properties(simpletap PROPERTIES CXX_STANDARD 14)


# testing options
option(SERIAL_TUN_BUILD_TESTS "Build tests" ${SERIAL_TUN_MASTER_PROJECT})
if(SERIAL_TUN_BUILD_TESTS)
    enable_testing()

    find_package(doctest 2.3.5)
    if (NOT TARGET doctest::doctest)
        fetchcontent_declare(
            doctest
            GIT_REPOSITORY https://github.com/onqtam/doctest.git
            GIT_TAG        2.3.5
        )
        fetchcontent_makeavailable(doctest)
    endif()
    if (NOT TARGET doctest::doctest)
        find_package(doctest 2.3.5 REQUIRED)
    endif()

    add_test(NAME simpletapPipe COMMAND simpletap -d /dev/XXX -i YYY -p)
    add_test(NAME test_setup COMMAND ${CMAKE_SOURCE_DIR}/setup.sh ${CMAKE_BINARY_DIR}/bin/simpletap )

    add_executable(test_slip test_slip.cpp slip.cpp slip.h)
    target_link_libraries(test_slip PRIVATE doctest::doctest)
    set_target_properties(test_slip PROPERTIES CXX_STANDARD 14)
    add_test(NAME test_slip COMMAND test_slip)


    # test coverage options
    option(USE_LCOV "Use GNU Test Coverage framework" OFF)
    if(USE_LCOV)
        set(CMAKE_BUILD_TYPE "Coverage" CACHE STRING "build for lcov" FORCE)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        include(CodeCoverage)

        set(COVERAGE_EXCLUDES
            '${FETCHCONTENT_BASE_DIR}/*' '/usr/*'
            '/Applications/Xcode.app/*' '/opt/*'
        )
        setup_target_for_coverage(
            NAME lcov   # NAME for custom target.
            EXECUTABLE ctest    # EXECUTABLE of the test driver executable that runs the tests.
                        # NOTE! This should always have a ZERO as exit code
                        # otherwise the coverage generation will not complete.
            #NO YET! OUTDIR coverage   # Name of output directory.
        )
    endif() #CMAKE_BUILD_TYPE STREQUAL "Coverage"
endif()

if(SERIAL_TUN_MASTER_PROJECT)
    include(CMakePrintSystemInformation)
    message("CMAKE_CXX_LIBRARY_ARCHITECTURE ${CMAKE_CXX_LIBRARY_ARCHITECTURE}")
endif()
