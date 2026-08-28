cmake_minimum_required(VERSION 3.16)
project(ReduceWhitePoint VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Gui Network REQUIRED)

# Enable Interprocedural Optimization (LTO) for smaller/faster binaries if supported
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    message(STATUS "IPO / LTO enabled")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Optional LayerShellQt for Linux Wayland
if(UNIX AND NOT APPLE)
    find_package(LayerShellQt QUIET)
    if(LayerShellQt_FOUND)
        message(STATUS "Found LayerShellQt. Wayland overlay support enabled.")
        add_compile_definitions(HAVE_LAYER_SHELL)
    else()
        message(STATUS "LayerShellQt NOT found. Falling back to standard Qt window flags.")
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/OverlayWindow.cpp
    src/Daemon.cpp
)

# On Windows, we don't want a console window popping up for a background GUI app
if(WIN32)
    add_executable(reduce-white WIN32 ${SOURCES})
else()
    add_executable(reduce-white ${SOURCES})
endif()

target_link_libraries(reduce-white PRIVATE Qt6::Core Qt6::Gui Qt6::Network)

if(LayerShellQt_FOUND)
    target_link_libraries(reduce-white PRIVATE LayerShellQt::Interface)
endif()

include(GNUInstallDirs)
install(TARGETS reduce-white DESTINATION ${CMAKE_INSTALL_BINDIR})
