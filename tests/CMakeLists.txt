# Copyright © 2026 Kyle J. Cardoza.
# SPDX-FileCopyrightText: 2026 Kyle J. Cardoza
# SPDX-License-Identifier: GPL-3.0-or-later

# libagora-launchservices' own suite.
#
# THE FIXTURES ARE AGORA BUNDLES AND NOTHING ELSE, which is the point of F1: the "applications" this suite
# scans are three `.app` directories carrying an `Info.plist`, and there is no `.desktop` file anywhere on
# the fixture tree to be imported. The suite COPIES them to a private directory before pointing
# `AGORA_APPLICATIONS` at it, so a test may move one without touching the source tree.
#
# THE LOADER NEEDS THE PREFIX ON THE PATH, because `DT_RUNPATH` is not consulted for a dependency of a
# dependency: `libagora-core.so` needs `libobjc.so`, and the directory comes from the imported target rather
# than being written down.

add_executable(launchservices launchservices.c)
target_compile_definitions(launchservices PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(launchservices PRIVATE LaunchServices CoreFoundation::CoreFoundation)

add_test(NAME launchservices COMMAND launchservices)

set(_agora_runtime_dirs
    "$<TARGET_FILE_DIR:LaunchServices>"
    "$<TARGET_FILE_DIR:CoreFoundation::CoreFoundation>")
list(REMOVE_DUPLICATES _agora_runtime_dirs)
string(REPLACE ";" ":" _agora_ld_path "${_agora_runtime_dirs}")
set_tests_properties(launchservices PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${_agora_ld_path}")

# THE OPENING DOOR: launch an application bundle, and open a document in its default application. The fixture
# is a bundle the test builds itself — no binary is committed.
add_executable(launch launch.c)
target_link_libraries(launch PRIVATE LaunchServices CoreFoundation::CoreFoundation)

add_test(NAME launch COMMAND launch)
set_tests_properties(launch PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${_agora_ld_path}")
