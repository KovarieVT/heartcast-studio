cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
#  HeartCast Studio — native Windows live-streaming studio (Qt 6 + libobs)
#  Rename the product in ONE place: change APP_NAME / APP_EXE below.
# ---------------------------------------------------------------------------
set(APP_NAME        "HeartCast Studio")
set(APP_EXE         "HeartCastStudio")
set(APP_ORG         "HeartCast")
set(APP_VERSION     "0.1.0")

project(HeartCastStudio VERSION ${APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HEARTCAST_BUILD_TESTS "Build unit tests" ON)
set(HEARTCAST_UPDATE_FEED "" CACHE STRING "HTTPS URL of the update feed JSON (empty = updates disabled)")

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets WebSockets Svg Concurrent Test)

# --- libobs ----------------------------------------------------------------
# On Windows point CMAKE_PREFIX_PATH at an obs-studio install prefix
# (see docs/BUILD-INSTRUCTIONS.md). On Linux, `apt install libobs-dev` works.
find_package(libobs CONFIG QUIET)
if(TARGET OBS::libobs)
    set(LIBOBS_TARGET OBS::libobs)
elseif(TARGET libobs)
    set(LIBOBS_TARGET libobs)
else()
    find_path(LIBOBS_INCLUDE_DIR obs.h PATH_SUFFIXES obs libobs REQUIRED)
    find_library(LIBOBS_LIBRARY NAMES obs libobs REQUIRED)
    add_library(libobs_imported UNKNOWN IMPORTED)
    set_target_properties(libobs_imported PROPERTIES
        IMPORTED_LOCATION "${LIBOBS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}")
    set(LIBOBS_TARGET libobs_imported)
endif()
# obs-studio build trees keep the generated obsconfig.h outside libobs/
set(LIBOBS_EXTRA_INCLUDE_DIRS "" CACHE STRING "Extra include dirs for libobs (e.g. <obs-build>/config)")
message(STATUS "Using libobs target: ${LIBOBS_TARGET}")

configure_file(src/app/BuildInfo.h.in ${CMAKE_BINARY_DIR}/generated/BuildInfo.h @ONLY)

# Everything except main.cpp goes into a static core library so tests can link it.
file(GLOB_RECURSE BC_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp src/core/*.cpp src/engine/*.cpp src/ui/*.cpp
    src/platforms/*.cpp src/auth/*.cpp src/overlays/*.cpp
    src/plugins/*.cpp src/updater/*.cpp)
file(GLOB_RECURSE BC_HEADERS CONFIGURE_DEPENDS src/*.h)

add_library(heartcast_core STATIC ${BC_SOURCES} ${BC_HEADERS} resources/resources.qrc)
target_include_directories(heartcast_core PUBLIC src ${CMAKE_BINARY_DIR}/generated ${LIBOBS_EXTRA_INCLUDE_DIRS})
target_link_libraries(heartcast_core PUBLIC
    Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::MultimediaWidgets Qt6::WebSockets Qt6::Svg Qt6::Concurrent
    ${LIBOBS_TARGET})
target_compile_definitions(heartcast_core PUBLIC HEARTCAST_UPDATE_FEED="${HEARTCAST_UPDATE_FEED}")

if(WIN32)
    target_compile_definitions(heartcast_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(heartcast_core PUBLIC crypt32 dbghelp dxgi pdh wintrust user32)
endif()
if(MSVC)
    target_compile_options(heartcast_core PUBLIC /W4 /permissive- /utf-8 /wd4100 /Zi)
    target_link_options(heartcast_core PUBLIC /DEBUG /OPT:REF /OPT:ICF)
else()
    target_compile_options(heartcast_core PUBLIC -Wall -Wextra -Wno-unused-parameter)
    # Debian/Ubuntu libobs-dev routes SSE through system SIMDe with native aliases,
    # which clashes with GCC 13's AVX-512 headers unless they are seen first.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(heartcast_core PUBLIC -include immintrin.h)
    endif()
endif()

add_executable(${APP_EXE} WIN32 src/main.cpp resources/windows/app.rc)
if(NOT WIN32)
    set_source_files_properties(resources/windows/app.rc PROPERTIES HEADER_FILE_ONLY ON)
endif()
target_link_libraries(${APP_EXE} PRIVATE heartcast_core)

if(WIN32)
    set_target_properties(${APP_EXE} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(MSVC)
        # app.rc embeds our own manifest (PerMonitorV2 DPI, Win10/11, long paths)
        target_link_options(${APP_EXE} PRIVATE /MANIFEST:NO)
    endif()
endif()

# Windows packaging (OBS-compatible layout) is done by tools/package_windows.ps1.
install(TARGETS ${APP_EXE} RUNTIME DESTINATION bin/64bit)

if(HEARTCAST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
