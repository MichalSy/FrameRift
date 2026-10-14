# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Michal Sytko and FrameRift contributors
# Original project: https://github.com/MichalSy/FrameRift
# Additional attribution terms: see NOTICE in the repository root.

cmake_minimum_required(VERSION 3.20)
project(FrameRift VERSION 0.1.0 LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
pkg_check_modules(VTE REQUIRED IMPORTED_TARGET vte-2.91-gtk4>=0.70)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.85)
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(SOUP REQUIRED IMPORTED_TARGET libsoup-3.0>=3.2)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebpdemux libwebp)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
if(TARGET SQLite3::SQLite3)
    set(FRAMERIFT_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(FRAMERIFT_SQLITE_TARGET SQLite::SQLite3)
endif()
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

file(GLOB_RECURSE UI_ASSETS CONFIGURE_DEPENDS assets/ui/*)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/resources.c
    COMMAND ${GLIB_COMPILE_RESOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/assets/framerift.gresource.xml
            --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/assets
            --target=${CMAKE_CURRENT_BINARY_DIR}/resources.c --generate-source
    DEPENDS assets/framerift.gresource.xml ${UI_ASSETS}
    VERBATIM)

add_library(framerift_library src/library/library.cpp src/library/steamgriddb.cpp)
target_compile_features(framerift_library PUBLIC cxx_std_20)
set_target_properties(framerift_library PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(framerift_library PUBLIC src)
target_link_libraries(framerift_library PUBLIC ${FRAMERIFT_SQLITE_TARGET} PRIVATE PkgConfig::CURL PkgConfig::JSON_GLIB PkgConfig::LIBSECRET)

add_library(framerift_runtime src/runtime/launch.cpp src/runtime/elf_input.cpp src/runtime/log.cpp src/runtime/bridge.cpp src/runtime/mcp.cpp src/runtime/session.cpp)
target_compile_features(framerift_runtime PUBLIC cxx_std_20)
set_target_properties(framerift_runtime PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(framerift_runtime PUBLIC src)
target_link_libraries(framerift_runtime PUBLIC framerift_library PkgConfig::GIO PkgConfig::JSON_GLIB PkgConfig::SOUP Threads::Threads)

add_executable(framerift src/ui/app.cpp src/ui/hero.cpp src/ui/profiler.cpp ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
target_link_libraries(framerift PRIVATE framerift_library framerift_runtime PkgConfig::GTK4 PkgConfig::VTE PkgConfig::WEBP)
if(MSVC)
    target_compile_options(framerift_library PRIVATE /W4)
    target_compile_options(framerift_runtime PRIVATE /W4)
    target_compile_options(framerift PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(framerift_library PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(framerift_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(framerift PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(elf_input_check tests/elf_input_check.cpp)
    target_link_libraries(elf_input_check PRIVATE framerift_runtime)
    add_test(NAME elf_input COMMAND elf_input_check)
    add_executable(bridge_check tests/bridge_check.cpp)
    target_link_libraries(bridge_check PRIVATE framerift_runtime)
    add_test(NAME debug_bridge COMMAND bridge_check)
    set_tests_properties(debug_bridge PROPERTIES TIMEOUT 30)
    add_executable(library_check tests/library_check.cpp)
    target_link_libraries(library_check PRIVATE framerift_library)
    add_test(NAME library_cache COMMAND library_check)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(session_check tests/session_check.cpp)
        target_link_libraries(session_check PRIVATE framerift_runtime)
        add_test(NAME game_mcp COMMAND session_check)
        set_tests_properties(game_mcp PROPERTIES TIMEOUT 30)
        add_executable(launch_check tests/launch_check.cpp)
        target_link_libraries(launch_check PRIVATE framerift_runtime)
        add_test(NAME game_launch COMMAND launch_check)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS framerift RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    configure_file(assets/io.github.framerift.FrameRift.desktop.in io.github.framerift.FrameRift.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.framerift.FrameRift.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/ui/mark.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME io.github.framerift.FrameRift.svg)
endif()
