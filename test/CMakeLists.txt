# Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
# SPDX-License-Identifier: Apache-2.0

# Test Directory - Testing Configuration

# Find test dependency
find_package(GTest REQUIRED)

# Helper function to add a test for a specific component
function(add_component_test component_name)
  set(test_name ${PROJECT_NAME}-${component_name}-test)
  add_executable(${test_name} ${component_name}.cc)
  target_link_libraries(${test_name} PRIVATE ${PROJECT_NAME} GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "unit-test;${PROJECT_NAME}-unit-test")
endfunction()

# Add test executables for each component
add_component_test(task)
add_component_test(task_queue)
add_component_test(main)