# Copyright © 2026 Kyle J. Cardoza.
# SPDX-FileCopyrightText: 2026 Kyle J. Cardoza
# SPDX-License-Identifier: LGPL-3.0-or-later

#===----------------------------------------------------------------------===#
#
# Agora's build for Launch Services (libagora-launchservices).
#
# APPLE'S LAUNCH SERVICES IS A CORESERVICES FRAMEWORK, AND IT SITS BELOW FOUNDATION. It owns the type model
# (Uniform Type Identifiers) and the questions about applications, and its doors are C functions over Core
# Foundation — `CFStringRef` for a UTI, `CFURLRef` for an application. So this target is `LANGUAGES C`, it
# depends on `libagora-core` ALONE, and it must not depend on `libagora-base`: Foundation sits ABOVE it, and
# a Launch Services that reached up would invert the stack. (The AppKit's `NSWorkspace` is that layer's face
# over this — see WORKSPACE-PLAN.md §2.)
#
# THE MODEL IS APPLE'S AND THE APPLICATION UNIVERSE IS CLOSED. Types arrive from an Agora bundle's
# `Info.plist`; there is no `.desktop` importer, no MIME database and no `xdg`, and no third-party library is
# linked. See README.md.
#
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.24)

project(AgoraLaunchServices LANGUAGES C)

# **EVERY LIBRARY FINDS ITS NEIGHBOURS, AND THE INSTALLED BUNDLE NEEDS NO ENVIRONMENT.** A `DT_RUNPATH` is NOT
# inherited by a dependency's own dependencies: whatever a program puts on its search path covers the libraries IT
# names and nothing below them. So an application whose runpath is the prefix still fails to resolve
# `libagora-base.so`'s `libagora-core.so`, because that lookup asks the LIBRARY for its path and it had none —
# MEASURED as an installed bundle that only started with `LD_LIBRARY_PATH` set to the directory its libraries were
# already in. `$ORIGIN` says "the directory I am in", which is where this tree installs every one of them, so the
# chain resolves on its own and a relocatable install is genuinely relocatable.
set(CMAKE_INSTALL_RPATH "\$ORIGIN")

include(GNUInstallDirs)

set(CMAKE_POSITION_INDEPENDENT_CODE YES)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(CoreFoundation CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Include shim: consumers write <LaunchServices/...>, which is how Apple's
# framework includes its own headers and how a consumer includes them.
# ---------------------------------------------------------------------------
set(_agora_include_dir "${CMAKE_BINARY_DIR}/agora-include")
file(REMOVE_RECURSE "${_agora_include_dir}")
file(MAKE_DIRECTORY "${_agora_include_dir}")
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}" "${_agora_include_dir}/LaunchServices" SYMBOLIC)

# ---------------------------------------------------------------------------
# The library: every .c in the directory. Shared, never static, as every Agora
# library is.
# ---------------------------------------------------------------------------
file(GLOB LS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

if(NOT LS_SOURCES)
  message(FATAL_ERROR "libagora-launchservices has no implementation files.")
endif()

add_library(LaunchServices SHARED ${LS_SOURCES})

set_target_properties(LaunchServices PROPERTIES OUTPUT_NAME agora-launchservices)

# -fconstant-cfstrings, so a CFSTR in a .c file is a constant rather than a call — the flag libagora-text and
# libagora-drawing compile their own C sources with.
target_compile_options(LaunchServices PRIVATE -fconstant-cfstrings)

target_include_directories(LaunchServices
    PUBLIC
        "$<BUILD_INTERFACE:${_agora_include_dir}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")

# CORE FOUNDATION IS PUBLIC: a consumer of Launch Services names a UTI with a CFStringRef and an application
# with a CFURLRef, so it reaches Core Foundation itself.
target_link_libraries(LaunchServices
    PUBLIC
        CoreFoundation::CoreFoundation)

# **THE TESTS ARE A DEVELOPMENT ARTEFACT, AND A RELEASE DOES NOT BUILD THEM.** `include(CTest)` is what defines
# `BUILD_TESTING` — ON when a configure says nothing, which is what a developer's build wants — and turns testing on with
# it; the subdirectory is added only when it is on, so `-DBUILD_TESTING=OFF`, which is what the release driver passes,
# configures and compiles no test executable at all.
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# The public headers, and NOT the internal seam: `LaunchServices_internal.h` is how the two implementation
# files speak, and a consumer has no business with it.
install(DIRECTORY . DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/LaunchServices
        FILES_MATCHING PATTERN "*.h" PATTERN "*_internal.h" EXCLUDE)
install(TARGETS LaunchServices
    EXPORT AgoraLaunchServicesTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CMakePackageConfigHelpers)

install(EXPORT AgoraLaunchServicesTargets
    FILE AgoraLaunchServicesTargets.cmake
    NAMESPACE AgoraLaunchServices::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AgoraLaunchServices)

configure_package_config_file(cmake/AgoraLaunchServicesConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/AgoraLaunchServicesConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AgoraLaunchServices)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/AgoraLaunchServicesConfigVersion.cmake
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/AgoraLaunchServicesConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/AgoraLaunchServicesConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AgoraLaunchServices)
