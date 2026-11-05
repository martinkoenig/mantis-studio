# No product target, install rule, environment activation or external dependency.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()
add_library(mantis-storage-qualification STATIC
  ${PROJECT_SOURCE_DIR}/src/artifact-store/artifact-store.cpp
  ${PROJECT_SOURCE_DIR}/src/artifact-store/segments.cpp faults.cpp)
target_include_directories(mantis-storage-qualification PUBLIC ${PROJECT_SOURCE_DIR}/src/artifact-store/include)
target_compile_definitions(mantis-storage-qualification PRIVATE MANTIS_STORAGE_FAULT_TESTING)
target_link_libraries(mantis-storage-qualification PUBLIC mantis-artifact-api mantis-platform
  PRIVATE SQLite::SQLite3 nlohmann_json::nlohmann_json mantis-options)
add_executable(mantis-storage-durability-tests durability.cpp ../unit/x1_store_failure.cpp)
target_link_libraries(mantis-storage-durability-tests PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,mantis-storage-qualification>" mantis-device-runtime SQLite::SQLite3)
target_link_options(mantis-storage-durability-tests PRIVATE "LINKER:--wrap=fsync")
add_test(NAME projected-storage-durability COMMAND mantis-storage-durability-tests)
set_tests_properties(projected-storage-durability PROPERTIES TIMEOUT 90)
# Compile the unchanged Session path with a private admission sampler. It and
# the existing fault Store are absent from product targets and installed SDKs.
add_executable(mantis-rig-storage-qualification rig.cpp
  ${PROJECT_SOURCE_DIR}/src/device-runtime/device-runtime.cpp)
target_compile_definitions(mantis-rig-storage-qualification PRIVATE MANTIS_SESSION_QUALIFICATION)
target_link_libraries(mantis-rig-storage-qualification PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,mantis-storage-qualification>" mantis-services
  nlohmann_json::nlohmann_json)
target_link_options(mantis-rig-storage-qualification PRIVATE "LINKER:--wrap=fsync")
add_dependencies(mantis-rig-storage-qualification virtual-rig)
add_test(NAME virtual-rig-storage COMMAND mantis-rig-storage-qualification
  regression $<TARGET_FILE:virtual-rig> ${PROJECT_SOURCE_DIR}/profiles/virtual-rig-trigger.json)
set_tests_properties(virtual-rig-storage PROPERTIES TIMEOUT 40)
# Full-rate/collapse modes are explicitly invoked, never registered as CTests.
# Keep all 56 ordinary X1 scenarios on the product Store. Only this new case
# uses the existing qualified Store binary and its private short-write control.
add_test(NAME x1-pairing-stream-GREY-post-commit-store-failure-recorder
  COMMAND mantis-storage-durability-tests --x1-post-commit-store-failure ${CMAKE_BINARY_DIR}/plugins)
set_tests_properties(x1-pairing-stream-GREY-post-commit-store-failure-recorder PROPERTIES TIMEOUT 30)
add_executable(mantis-storage-fault-mantisd daemon.cpp)
target_link_libraries(mantis-storage-fault-mantisd PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,mantis-storage-qualification>" mantis-service-adapter)
target_link_options(mantis-storage-fault-mantisd PRIVATE "LINKER:--wrap=fsync")
add_executable(mantis-storage-fault-client daemon-client.cpp)
target_link_libraries(mantis-storage-fault-client PRIVATE mantis-client mantis-service-adapter)
add_test(NAME projected-storage-daemon COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/daemon.py ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR})
set_tests_properties(projected-storage-daemon PROPERTIES TIMEOUT 45)
add_test(NAME projected-storage-boundaries COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/tests/contract/storage_durability_boundaries.py ${PROJECT_SOURCE_DIR}
  $<TARGET_FILE:mantis-artifact-store> $<TARGET_FILE:mantisd>
  $<TARGET_FILE:mantis-storage-fault-client> $<TARGET_FILE:mantis-x1-pairing-stream-tests>
  $<TARGET_FILE:mantis-device-runtime>
  $<TARGET_FILE:mantis-storage-fault-mantisd> $<TARGET_FILE:mantis-storage-durability-tests>
  $<TARGET_FILE:mantis-rig-storage-qualification>)
