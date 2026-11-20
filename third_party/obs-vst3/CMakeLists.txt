# obs-vst3: VST3 audio-filter host for libobs, by pkv (OBS Studio pull request #12752,
# commit 695a90e), GPL-3.0-or-later. Vendored for KovaLive! with two small changes
# (no OBS frontend API) — see README.md in this folder.
#
# Produces:
#   obs-vst3 (module)       → <install>/obs-plugins/64bit/obs-vst3.dll
#   obs-vst3-scanner (exe)  → next to KovaLive.exe (the module looks for it there)
# Needs the Steinberg VST3 SDK (MIT): pass -DSTEINBERG_VST3_PATH=<dir containing vst3sdk/>.

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(SteinbergVST3 REQUIRED)

# Shared scanner code (module + scanner process).
add_library(obs-vst3-library INTERFACE)
target_sources(obs-vst3-library INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/VST3Scanner.cpp ${CMAKE_CURRENT_SOURCE_DIR}/VST3Scanner.h)
target_include_directories(obs-vst3-library INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(obs-vst3-library INTERFACE Steinberg::VST3)

function(kovalive_obs_plugin_defaults target)
    # Same language level and defines OBS builds this code with.
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_include_directories(${target} PRIVATE ${LIBOBS_EXTRA_INCLUDE_DIRS})
    if(MSVC)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS NOMINMAX)
        target_compile_options(${target} PRIVATE /W3 /utf-8 /MP /Zi)
        target_link_options(${target} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
endfunction()

add_executable(obs-vst3-scanner vst3-scan/vst3-scanner.cpp)
target_link_libraries(obs-vst3-scanner PRIVATE ${LIBOBS_TARGET} obs-vst3-library)
kovalive_obs_plugin_defaults(obs-vst3-scanner)

add_library(obs-vst3 MODULE
    VST3ComponentHolder.cpp VST3ComponentHolder.h
    VST3EditorWindow.cpp VST3EditorWindow.h
    VST3HostApp.cpp VST3HostApp.h
    VST3Plugin.cpp VST3Plugin.h
    obs-vst3.cpp obs-vst3.h
    plugin-main.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(obs-vst3 PRIVATE RunLoopImpl.cpp RunLoopImpl.h)
endif()
target_link_libraries(obs-vst3 PRIVATE ${LIBOBS_TARGET} obs-vst3-library Qt6::Widgets)
set_target_properties(obs-vst3 PROPERTIES PREFIX "" AUTOMOC ON)
kovalive_obs_plugin_defaults(obs-vst3)
add_dependencies(obs-vst3 obs-vst3-scanner)

if(WIN32)
    set(KOVALIVE_ICON "${PROJECT_SOURCE_DIR}/resources/windows/app.ico")
    configure_file(cmake/windows/obs-module.rc.in ${CMAKE_CURRENT_BINARY_DIR}/obs-vst3.rc @ONLY)
    target_sources(obs-vst3 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/obs-vst3.rc)
endif()

install(TARGETS obs-vst3 LIBRARY DESTINATION obs-plugins/64bit RUNTIME DESTINATION obs-plugins/64bit)
install(TARGETS obs-vst3-scanner RUNTIME DESTINATION bin/64bit)
install(DIRECTORY data/ DESTINATION data/obs-plugins/obs-vst3)
if(MSVC)
    install(FILES $<TARGET_PDB_FILE:obs-vst3> DESTINATION obs-plugins/64bit OPTIONAL)
endif()
