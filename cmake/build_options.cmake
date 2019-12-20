# ===========================================================================
# CMAKE PART: BUILD-OPTIONS: Setup CMAKE Build Options
# ===========================================================================
# USE: INCLUDE-AFTER project() !!!

include_guard(DIRECTORY)

# ---------------------------------------------------------------------------
# general user options for ninja and cppcheck
# ---------------------------------------------------------------------------
option(CMAKE_C_DEPFILE_EXTENSION_REPLACE "name depend files as main.d instead of main.c.d" OFF)
option(CMAKE_C_OUTPUT_EXTENSION_REPLACE "name object files as main.o instead of main.c.o" OFF)
option(CMAKE_CXX_DEPFILE_EXTENSION_REPLACE "name depend files as main.d instead of main.cpp.d" ON)
option(CMAKE_CXX_OUTPUT_EXTENSION_REPLACE "name object files as main.o instead of main.cpp.o" ON)


option(CMAKE_DEPENDS_IN_PROJECT_ONLY "do not use system header files" ON)
if(CMAKE_DEPENDS_IN_PROJECT_ONLY)
    set(CMAKE_DEPFILE_FLAGS_C "-MMD" CACHE STRING "dependency flag" FORCE)
    set(CMAKE_DEPFILE_FLAGS_CXX "-MMD" CACHE STRING "dependency flag" FORCE)
else()
    set(CMAKE_DEPFILE_FLAGS_C "-MD" CACHE STRING "dependency flag" FORCE)
    set(CMAKE_DEPFILE_FLAGS_CXX "-MD" CACHE STRING "dependency flag" FORCE)
endif()


#----------------------------------------------------------------------------
# Compiler config
#----------------------------------------------------------------------------
option(CXX_STANDARD_REQUIRED "Require C++17 standard" ON)
if(CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_STANDARD 17)  # Use C++17 standard
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

option(COMPILER_WARNINGS_ARE_ERRORS "To be pedantic! ;-)" ON)
if(COMPILER_WARNINGS_ARE_ERRORS)
    if(MSVC)
        # warning level 4 and all warnings as errors
        add_compile_options(/W4 /WX)
    else()
        # lots of warnings and all warnings as errors
        add_compile_options(-Wall -Wextra -Wpedantic
          ##FIXME -Werror
          ##TBD -Wno-unknown-warning-option
          -Wno-unused-parameter
          -Wno-unused-variable
        )
    endif()
endif()


option(USE_CXX_CPPCHECK "run cppcheck along with the compiler and report any problems" OFF)
if(USE_CXX_CPPCHECK)
    find_program(CMAKE_CXX_CPPCHECK cppcheck
        HINTS /usr/local/bin "C:/Program Files/Cppcheck" REQUIRED
    )
else()
    unset(CMAKE_CXX_CPPCHECK)
endif()


option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
if(NOT ${BUILD_SHARED_LIBS})
    add_definitions(-DSTATIC_LIB)
endif()


option(USE_OUTPUT_PATH "build all libaries and runtime files at build/lib and build/bin" ON)
if(USE_OUTPUT_PATH)
    # -----------------------------------------------------------------------
    # Where to put all the LIBRARY targets when built.  This variable is used
    # to initialize the LIBRARY_OUTPUT_DIRECTORY property on all the targets.
    # -----------------------------------------------------------------------
    set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

    # -----------------------------------------------------------------------
    # Make sure the linker can find a library once it is built.
    # -----------------------------------------------------------------------
    link_directories(${LIBRARY_OUTPUT_PATH})

    # -----------------------------------------------------------------------
    # Where to put all the RUNTIME targets when built.  This variable is used
    # to initialize the RUNTIME_OUTPUT_DIRECTORY property on all the targets.
    # -----------------------------------------------------------------------
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()


# test coverage options
option(USE_LCOV "Use GNU Test Coverage framework" OFF)
if(USE_LCOV)
    set(CMAKE_BUILD_TYPE "Coverage" CACHE STRING "build for lcov" FORCE)
endif()


# ---------------------------------------------------------------------------
# helper for older cmake versions
# ---------------------------------------------------------------------------
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

option(USE_PRINT_SYSTEM_INFO "Use CMakePrintSystemInformation" OFF)
if(USE_PRINT_SYSTEM_INFO)
    include(CMakePrintSystemInformation)
    message("CMAKE_CXX_LIBRARY_ARCHITECTURE ${CMAKE_CXX_LIBRARY_ARCHITECTURE}")
endif()
