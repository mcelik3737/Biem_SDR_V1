# --- biem_core: platform/hardware-independent logic ------------------------
# Builds and is unit-tested with just a C++20 compiler + SQLite3 - no Qt,
# no librtlsdr required. See top-level CMakeLists.txt for the optional
# feature detection this depends on.

set(BIEM_CORE_SOURCES
    core/Database.cpp
    core/TitleBuilder.cpp
    core/WavWriter.cpp
    core/CallRecorder.cpp

    dsp/NbfmDemodulator.cpp
    dsp/WavIqSource.cpp

    dsp/dmr/Hamming1511.cpp
    dsp/dmr/Hamming139.cpp
    dsp/dmr/Golay2087.cpp
    dsp/dmr/Bptc196x96.cpp
    dsp/dmr/DmrFrameSync.cpp
    dsp/dmr/DmrSlotDecoder.cpp
    dsp/dmr/DmrLinkControl.cpp
    dsp/dmr/DmrCallTracker.cpp

    net/UdpRawLogger.cpp
    net/hytera/HyteraHR659Source.cpp
)

add_library(biem_core STATIC ${BIEM_CORE_SOURCES})
target_include_directories(biem_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(biem_core PUBLIC ${BIEM_SQLITE3_TARGET})
target_compile_features(biem_core PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(biem_core PUBLIC ws2_32)
endif()
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(biem_core PUBLIC Threads::Threads)
endif()

# --- Optional: live RTL-SDR hardware backend --------------------------------
if(BIEM_HAVE_RTLSDR)
    add_library(biem_sdr_rtl STATIC dsp/RtlSdrSource.cpp)
    target_include_directories(biem_sdr_rtl PRIVATE ${RTLSDR_INCLUDE_DIRS})
    target_link_directories(biem_sdr_rtl PRIVATE ${RTLSDR_LIBRARY_DIRS})
    target_link_libraries(biem_sdr_rtl PUBLIC biem_core PRIVATE ${RTLSDR_LIBRARIES})
    target_compile_definitions(biem_sdr_rtl PUBLIC BIEM_HAVE_RTLSDR)
endif()

# --- CLI (headless) ----------------------------------------------------------
add_executable(biem_cli cli/biem_cli.cpp)
target_link_libraries(biem_cli PRIVATE biem_core)
if(TARGET biem_sdr_rtl)
    target_link_libraries(biem_cli PRIVATE biem_sdr_rtl)
endif()

# --- GUI (Qt6) -----------------------------------------------------------
if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)

    add_executable(biem_gui
        main.cpp
        ui/MainWindow.cpp
        ui/CallLogWidget.cpp
        ui/ChannelListWidget.cpp
        ui/PlaybackWidget.cpp
    )
    target_link_libraries(biem_gui PRIVATE biem_core Qt6::Core Qt6::Widgets Qt6::Multimedia)
    if(TARGET biem_sdr_rtl)
        target_link_libraries(biem_gui PRIVATE biem_sdr_rtl)
    endif()
endif()
