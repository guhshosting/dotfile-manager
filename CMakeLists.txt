cmake_minimum_required(VERSION 3.16)

project(dotfile-manager
    VERSION 1.0.0
    DESCRIPTION "Safe command-line utility for discovering, inventorying, comparing, backing up, and restoring user dotfiles"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(DFM_BUILD_TESTS "Build the dotfile-manager test suite" ON)
option(DFM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

find_package(nlohmann_json 3.9 REQUIRED)

add_library(dfm_core STATIC
    src/env.cpp
    src/path_safety.cpp
    src/file_types.cpp
    src/ignore_rules.cpp
    src/atomic_io.cpp
    src/config.cpp
    src/hashing.cpp
    src/diff_engine.cpp
    src/compare.cpp
    src/discovery.cpp
    src/backup.cpp
    src/output.cpp
    src/cli.cpp
    src/commands.cpp
    src/version.cpp
)

target_include_directories(dfm_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(dfm_core PUBLIC nlohmann_json::nlohmann_json)

set(DFM_WARNING_FLAGS -Wall -Wextra -Wpedantic)
if(DFM_WARNINGS_AS_ERRORS)
    list(APPEND DFM_WARNING_FLAGS -Werror)
endif()
target_compile_options(dfm_core PRIVATE ${DFM_WARNING_FLAGS})

add_executable(dotfile-manager src/main.cpp)
target_link_libraries(dotfile-manager PRIVATE dfm_core)
target_compile_options(dotfile-manager PRIVATE ${DFM_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS dotfile-manager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES man/dotfile-manager.1.gz
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
    OPTIONAL
)
install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(DFM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
