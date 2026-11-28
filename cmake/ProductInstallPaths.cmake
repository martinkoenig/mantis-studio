# Internal install contract v1. Relative GNUInstallDirs destinations only.
include(GNUInstallDirs)
set(MANTIS_INSTALL_PLUGINSDIR "${CMAKE_INSTALL_LIBDIR}/mantis/${PROJECT_VERSION}/plugins" CACHE STRING "Relative product plugin directory")
set(MANTIS_INSTALL_HOSTDIR "${CMAKE_INSTALL_LIBEXECDIR}/mantis/${PROJECT_VERSION}" CACHE STRING "Relative private plugin host directory")
set(MANTIS_INSTALL_RECIPESDIR "${CMAKE_INSTALL_DATADIR}/mantis/${PROJECT_VERSION}/recipes" CACHE STRING "Relative recipe directory")
foreach(directory IN ITEMS CMAKE_INSTALL_BINDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_LIBEXECDIR
                           CMAKE_INSTALL_DATADIR CMAKE_INSTALL_INCLUDEDIR
                           MANTIS_INSTALL_PLUGINSDIR MANTIS_INSTALL_HOSTDIR MANTIS_INSTALL_RECIPESDIR)
 if(IS_ABSOLUTE "${${directory}}" OR "${${directory}}" STREQUAL ""
    OR "${${directory}}" MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)|[;\n\r]|^[A-Za-z]:")
  message(FATAL_ERROR "${directory} must be a nonempty prefix-relative path without parent traversal")
 endif()
endforeach()
# Staging a shared internal graph would require a distinct private-library ownership
# contract. Do not silently install incomplete executables or promise a C++ SONAME.
# Existing shared build-tree use remains supported; this guard runs only on install.
if(BUILD_SHARED_LIBS)
 install(CODE "message(FATAL_ERROR \"Product staging v1 requires BUILD_SHARED_LIBS=OFF; shared build-tree use is unchanged\")" ALL_COMPONENTS)
endif()
file(RELATIVE_PATH MANTIS_PLUGINS_FROM_BIN "/${CMAKE_INSTALL_BINDIR}" "/${MANTIS_INSTALL_PLUGINSDIR}")
file(RELATIVE_PATH MANTIS_HOST_FROM_BIN "/${CMAKE_INSTALL_BINDIR}" "/${MANTIS_INSTALL_HOSTDIR}")
file(RELATIVE_PATH MANTIS_RECIPES_FROM_BIN "/${CMAKE_INSTALL_BINDIR}" "/${MANTIS_INSTALL_RECIPESDIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/product_paths.hpp.in" "${CMAKE_BINARY_DIR}/generated/product_paths.hpp" @ONLY)
install(SCRIPT "${CMAKE_BINARY_DIR}/generated/ProductInstallGuard.cmake" ALL_COMPONENTS)
