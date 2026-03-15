# Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
# SPDX-License-Identifier: Apache-2.0

# Root CMakeLists.txt - Thread Pool Library Project Configuration
cmake_minimum_required(VERSION 3.28.0)
project(thread-pool
  VERSION 1.1.0
  DESCRIPTION "A compact, cross-platform thread pool library written for C++23 using modules and dependency injection. The design emphasizes flexibility, zero-cost abstractions where possible, and safe, explicit semantics."
  HOMEPAGE_URL "https://github.com/asdhij/thread-pool"
  LANGUAGES CXX
)

# Set C++ standard to 23 if not already set or if current standard is lower
if(NOT CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD LESS 23)
  set(CMAKE_CXX_STANDARD 23)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)  # Enable C++ module support
set(CMAKE_CXX_EXTENSIONS OFF)       # Disable compiler-specific extensions

option(THREAD_POOL_BUILD_TESTS "Build tests" ON)  # Option to build tests (enabled by default)

# Add library modules (EXCLUDE_FROM_ALL prevents building by default)
add_subdirectory(module EXCLUDE_FROM_ALL)
if (PROJECT_IS_TOP_LEVEL)
  add_subdirectory(cmake)
endif()

# Conditionally build tests
if(THREAD_POOL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

# Conditionally build documentation
find_package(Doxygen OPTIONAL_COMPONENTS dot mscgen dia)
if (DOXYGEN_FOUND)
  if (TARGET Doxygen::dot)
    set(DOXYGEN_CLASS_DIAGRAMS YES)
    set(DOXYGEN_CALLER_GRAPH YES)
    set(DOXYGEN_CALL_GRAPH YES)
  endif()

  doxygen_add_docs(${PROJECT_NAME}-docs
    ${CMAKE_CURRENT_SOURCE_DIR}/module
    COMMENT "Generating ${PROJECT_NAME} API documentation with Doxygen"
  )
endif()