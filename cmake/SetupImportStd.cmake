# Copyright (c) 2026 asdhij (169761929+asdhij@users.noreply.github.com)
# SPDX-License-Identifier: Apache-2.0

# Auto‑provision experimental import std UUID if needed
if (NOT CMAKE_CXX_COMPILER_IMPORT_STD)
  block()
  if (DEFINED CACHE{CMAKE_EXPERIMENTAL_CXX_IMPORT_STD})
    message(STATUS "Using cached experimental import std UUID: ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD}")
  else()
    message(STATUS "Attempting to obtain experimental import std UUID automatically...")
    set(_exp_tmp "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/experimental_uuid.rst")

    file(DOWNLOAD "https://raw.githubusercontent.com/Kitware/CMake/v${CMAKE_VERSION}/Help/dev/experimental.rst" "${_exp_tmp}" STATUS _status TIMEOUT 15)
    list(GET _status 0 _status_code)
    list(GET _status 1 _status_msg)

    if (NOT _status_code EQUAL 0)
      message(FATAL_ERROR
        "Failed to download experimental.rst: ${_status_msg}\n"
        "Please enable import std support manually with:\n"
        "  -DCMAKE_EXPERIMENTAL_CXX_IMPORT_STD=<uuid>\n\n"
        "Find the correct UUID for your CMake version using:\n"
        "  cmake -LAH ${CMAKE_CURRENT_BINARY_DIR} | grep CMAKE_EXPERIMENTAL_CXX_IMPORT_STD")
    endif()

    file(READ "${_exp_tmp}" _exp_content)
    file(REMOVE "${_exp_tmp}")  # clean up immediately

    # Corrected regex: matches UUID in value ``...`` (double backticks)
    string(REGEX MATCH "CMAKE_EXPERIMENTAL_CXX_IMPORT_STD[^\n]*\n[^\n]*``([a-f0-9\-]+)``" _match "${_exp_content}")
    if (CMAKE_MATCH_1)
      set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "${CMAKE_MATCH_1}" CACHE STRING "Auto‑detected UUID for import std" FORCE)
      message(STATUS "Experimental import std UUID set to ${CMAKE_MATCH_1}")
    else()
      message(FATAL_ERROR
        "Failed to parse UUID from experimental.rst.\n"
        "Please enable import std support manually with:\n"
        "  -DCMAKE_EXPERIMENTAL_CXX_IMPORT_STD=<uuid>\n\n"
        "Find the correct UUID for your CMake version using:\n"
        "  cmake -LAH ${CMAKE_CURRENT_BINARY_DIR} | grep CMAKE_EXPERIMENTAL_CXX_IMPORT_STD")
    endif()
  endif()
  endblock()
endif()