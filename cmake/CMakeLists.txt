# Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
# SPDX-License-Identifier: Apache-2.0

# CMake Directory - Installation and Packaging Configuration
include(GNUInstallDirs)

# Install the library, module source files, and export package targets
install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}_targets
  ARCHIVE                                         # Static library
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ  # Set files as read-only
  FILE_SET CXX_MODULES
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}"
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
  CXX_MODULES_BMI
    DESTINATION ""                                # Do not install precompiled module interfaces (BMI)
)

# Export the target so that consumers can use find_package(thread-pool)
install(
  EXPORT ${PROJECT_NAME}_targets
  FILE ${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME}"
  PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
)

message(STATUS "Module files of ${PROJECT_NAME} will be installed to: ${CMAKE_INSTALL_FULL_INCLUDEDIR}/${PROJECT_NAME}")

# Generate package configuration files
include(CMakePackageConfigHelpers)

# Generate package configuration file
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME}"
)

# Generate package version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
  ARCH_INDEPENDENT
)

# Install configuration files
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME}"
  PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
)

# Create uninstall target if it doesn't exist
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Uninstall.cmake"
    @ONLY
  )

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/Uninstall.cmake"
    COMMENT "Uninstalling ${PROJECT_NAME}"
  )
endif()