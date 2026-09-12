cmake_minimum_required(VERSION 3.21)
project(BiemRadiaDispatcher
    VERSION 0.1.0
    DESCRIPTION "BIEM Radia Dispatcher - SDR/DMR radio call recorder, logger and playback"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# --- Optional feature detection --------------------------------------------
# The project is designed to configure and build its core logic (DSP, DMR,
# database, recording, network ingest) on ANY platform with just a C++20
# compiler + SQLite3. The GUI (Qt6) and the live RTL-SDR hardware backend
# (librtlsdr) are optional: if not found, those targets are skipped and a
# status message explains what was disabled. This lets the algorithmic core
# be built and unit-tested in minimal/headless/CI environments, while the
# full desktop app builds on a dev machine that has Qt6 + librtlsdr
# installed (see docs/BUILD_WINDOWS.md).

find_package(SQLite3 REQUIRED)
# The imported target name for SQLite3 was renamed between CMake versions
# (older FindSQLite3 modules create SQLite::SQLite3; newer ones create
# SQLite3::SQLite3 and mark the old name deprecated - CMake 4.x on Windows
# vs 3.28 on Linux disagreed on this during development). Pick whichever
# actually exists instead of hardcoding one, so configure is clean on both.
if(TARGET SQLite3::SQLite3)
    set(BIEM_SQLITE3_TARGET SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(BIEM_SQLITE3_TARGET SQLite::SQLite3)
else()
    message(FATAL_ERROR "SQLite3 was found but neither SQLite3::SQLite3 nor SQLite::SQLite3 exists as a target")
endif()

find_package(Qt6 QUIET COMPONENTS Core Widgets Multimedia)
if(Qt6_FOUND)
    message(STATUS "Qt6 found (${Qt6_VERSION}) -> GUI target 'biem_gui' will be built")
else()
    message(STATUS "Qt6 NOT found -> GUI target 'biem_gui' will be SKIPPED (headless build)")
endif()

find_package(PkgConfig QUIET)
set(BIEM_HAVE_RTLSDR FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(RTLSDR QUIET librtlsdr)
    if(RTLSDR_FOUND)
        set(BIEM_HAVE_RTLSDR TRUE)
        message(STATUS "librtlsdr found -> live RTL-SDR hardware backend enabled")
    endif()
endif()
if(NOT BIEM_HAVE_RTLSDR)
    message(STATUS "librtlsdr NOT found -> live RTL-SDR hardware backend SKIPPED (WavIqSource file/offline backend still built)")
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
