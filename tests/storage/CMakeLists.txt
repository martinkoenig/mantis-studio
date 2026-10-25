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
add_executable(mantis-storage-durability-tests durability.cpp)
target_link_libraries(mantis-storage-durability-tests PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,mantis-storage-qualification>" mantis-device-runtime SQLite::SQLite3)
target_link_options(mantis-storage-durability-tests PRIVATE "LINKER:--wrap=fsync")
add_test(NAME projected-storage-durability COMMAND mantis-storage-durability-tests)
set_tests_properties(projected-storage-durability PROPERTIES TIMEOUT 90)
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
  $<TARGET_FILE:mantis-storage-fault-client> $<TARGET_FILE:mantis-storage-fault-mantisd>)
