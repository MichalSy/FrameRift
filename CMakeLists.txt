# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Michal Sytko and FrameRift contributors
# Original project: https://github.com/MichalSy/FrameRift
# Additional attribution terms: see NOTICE in the repository root.

cmake_minimum_required(VERSION 3.20)
project(FrameRift VERSION 0.1.0 LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.85)
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)
find_package(SQLite3 REQUIRED)
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
target_link_libraries(framerift_library PUBLIC ${FRAMERIFT_SQLITE_TARGET} PRIVATE PkgConfig::CURL PkgConfig::JSON_GLIB)

add_executable(framerift src/ui/app.cpp ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
target_link_libraries(framerift PRIVATE framerift_library PkgConfig::GTK4)
if(MSVC)
    target_compile_options(framerift_library PRIVATE /W4)
    target_compile_options(framerift PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(framerift_library PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(framerift PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(library_check tests/library_check.cpp)
    target_link_libraries(library_check PRIVATE framerift_library)
    add_test(NAME library_cache COMMAND library_check)
endif()

install(TARGETS framerift RUNTIME DESTINATION bin)
