# Authoritative immutable role ownership. No runtime internals/worker/test fixtures.
configure_file("${CMAKE_CURRENT_LIST_DIR}/ProductInstallGuard.cmake.in" "${CMAKE_BINARY_DIR}/generated/ProductInstallGuard.cmake" @ONLY)
# GNUInstallDirs permits empty cache entries with nonempty effective normal
# variables (e.g. DATADIR inherits DATAROOTDIR). Serialize actual configure values,
# never infer an effective install contract by parsing raw cache placeholders.
set(install_layout "{\"contract_version\":1,\"application_version\":\"${PROJECT_VERSION}\",\"destinations\":{}}")
foreach(directory IN ITEMS CMAKE_INSTALL_BINDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_LIBEXECDIR
                           CMAKE_INSTALL_DATADIR CMAKE_INSTALL_INCLUDEDIR
                           MANTIS_INSTALL_PLUGINSDIR MANTIS_INSTALL_HOSTDIR MANTIS_INSTALL_RECIPESDIR
                           MantisPluginSDK_INSTALL_CMAKEDIR)
 set(value "${${directory}}")
 string(REPLACE "\\" "\\\\" value "${value}")
 string(REPLACE "\"" "\\\"" value "${value}")
 string(REPLACE "\t" "\\t" value "${value}")
 string(REPLACE "\n" "\\n" value "${value}")
 string(REPLACE "\r" "\\r" value "${value}")
 string(JSON install_layout SET "${install_layout}" destinations "${directory}" "\"${value}\"")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/generated/install-layout-v1.json" "${install_layout}\n")
install(TARGETS mantisd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Daemon)
install(TARGETS mantis-plugin-host RUNTIME DESTINATION "${MANTIS_INSTALL_HOSTDIR}" COMPONENT Daemon)
set(product_plugins virtual-scanner virtual-rig example-points ply-exporter)
if(TARGET mantis-x1)
 list(APPEND product_plugins mantis-x1)
endif()
foreach(plugin IN LISTS product_plugins)
 install(TARGETS ${plugin} LIBRARY DESTINATION "${MANTIS_INSTALL_PLUGINSDIR}" COMPONENT Daemon)
 install(FILES "${CMAKE_BINARY_DIR}/plugins/${plugin}.json" DESTINATION "${MANTIS_INSTALL_PLUGINSDIR}" COMPONENT Daemon)
endforeach()
# Explicit list excludes the deliberate crash recipe. Missing input is an error.
install(FILES "${CMAKE_SOURCE_DIR}/recipes/example.json" DESTINATION "${MANTIS_INSTALL_RECIPESDIR}" COMPONENT Daemon)
install(TARGETS mantis-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT CLI)
if(TARGET mantis-studio)
 install(TARGETS mantis-studio RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Studio)
endif()
# Notices have distinct owners; no cross-role overwrite/removal coupling.
set(product_components Daemon CLI)
if(TARGET mantis-studio)
 list(APPEND product_components Studio)
endif()
foreach(component IN LISTS product_components)
 install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/MantisStudio-${PROJECT_VERSION}/${component}" COMPONENT ${component})
endforeach()
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
 add_test(NAME installed-product COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/install/qualify.py"
   --build "${CMAKE_BINARY_DIR}" --output "${CMAKE_BINARY_DIR}/install-evidence")
 set_tests_properties(installed-product PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
endif()
