# Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
# SPDX-License-Identifier: Apache-2.0

# Module Directory - Library Target Definition

# Find dependency
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES
  thread_pool.ixx
  task.ixx
  task_queue.ixx
  policy.ixx
)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})