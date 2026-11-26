cmake_minimum_required(VERSION 3.24)
# Standalone, header-only Plugin SDK. Never add_subdirectory the product here.
# Product/package version is independent of MANTIS_ABI_V1 in the public C header.
project(MantisPluginSDK VERSION 0.2.0 LANGUAGES NONE)

# No architecture probing is needed for INTERFACE targets. This private-prefix
# default is configurable and is not a decision about the product shipping layout.
set(CMAKE_INSTALL_LIBDIR "lib" CACHE STRING "SDK package metadata library directory")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(MantisPluginSDK_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/MantisPluginSDK-${PROJECT_VERSION}"
    CACHE STRING "Relative SDK CMake package installation directory")
foreach(directory IN ITEMS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_DATADIR
                           MantisPluginSDK_INSTALL_CMAKEDIR)
  if(IS_ABSOLUTE "${${directory}}" OR "${${directory}}" MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)"
     OR "${${directory}}" STREQUAL "")
    message(FATAL_ERROR "${directory} must be a nonempty prefix-relative path without parent traversal")
  endif()
endforeach()
set(sdk_include "${CMAKE_INSTALL_INCLUDEDIR}/mantis-plugin-sdk-${PROJECT_VERSION}")

add_library(mantis-sdk-c INTERFACE)
target_include_directories(mantis-sdk-c INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c/include>"
  "$<INSTALL_INTERFACE:${sdk_include}/c>")
target_compile_features(mantis-sdk-c INTERFACE c_std_11)
add_library(mantis-sdk-cpp INTERFACE)
target_include_directories(mantis-sdk-cpp INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>"
  "$<INSTALL_INTERFACE:${sdk_include}/cpp>")
# Match the selected source baseline's C++23 policy; this is not a C++ ABI promise.
target_compile_features(mantis-sdk-cpp INTERFACE cxx_std_23)
target_link_libraries(mantis-sdk-cpp INTERFACE mantis-sdk-c)

install(TARGETS mantis-sdk-c mantis-sdk-cpp EXPORT MantisPluginSDKTargets)
install(FILES c/include/mantis/plugin.h c/include/mantis/projected_light.h
              c/include/mantis/semantic_views.h DESTINATION "${sdk_include}/c/mantis")
install(FILES cpp/include/mantis/sdk.hpp DESTINATION "${sdk_include}/cpp/mantis")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/MantisPluginSDK-${PROJECT_VERSION}")
install(EXPORT MantisPluginSDKTargets NAMESPACE MantisPluginSDK::
        DESTINATION "${MantisPluginSDK_INSTALL_CMAKEDIR}")

configure_package_config_file(cmake/MantisPluginSDKConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/MantisPluginSDKConfig.cmake"
  INSTALL_DESTINATION "${MantisPluginSDK_INSTALL_CMAKEDIR}"
  PATH_VARS sdk_include)
# Exact package discovery only: no unqualified version-skew compatibility promise.
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MantisPluginSDKConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}" COMPATIBILITY ExactVersion ARCH_INDEPENDENT)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MantisPluginSDKConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/MantisPluginSDKConfigVersion.cmake"
        DESTINATION "${MantisPluginSDK_INSTALL_CMAKEDIR}")
