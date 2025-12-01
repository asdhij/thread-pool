# Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
# SPDX-License-Identifier: Apache-2.0

# CMake Directory - Installation and Packaging Configuration
include(GNUInstallDirs)

# Install targets - copy module files to installation directory
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../module/.
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
  FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ  # Set files as read-only
  DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE  # Full permissions for owner
    GROUP_READ GROUP_EXECUTE              # Read and execute for group
    WORLD_READ WORLD_EXECUTE              # Read and execute for others
  PATTERN "CMakeLists.txt" EXCLUDE
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/../module/CMakeLists.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeLists.txt"
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeLists.txt
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
)

message(STATUS "Module files of ${PROJECT_NAME} will be installed to: ${CMAKE_INSTALL_FULL_INCLUDEDIR}/${PROJECT_NAME}")

# Generate package configuration files
include(CMakePackageConfigHelpers)

set(PROJECT_TARGETS ${PROJECT_NAME})

# Generate package configuration file
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME}
  PATH_VARS PROJECT_TARGETS
)

# Generate package version file
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
  ARCH_INDEPENDENT
)

# Install configuration files
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME}
)

# Create uninstall target if it doesn't exist
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Uninstall.cmake"
    @ONLY
  )

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/Uninstall.cmake
    COMMENT "Uninstalling ${PROJECT_NAME}"
  )
endif()