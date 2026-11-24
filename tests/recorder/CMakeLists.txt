# Isolated Linux-only diagnostic copies; never installed or linked into products.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()
set(recorder_inputs
  src/device-runtime/device-runtime.cpp src/device-runtime/recorder-diagnostics.hpp
  src/device-runtime/recorder-diagnostics.cpp src/artifact-store/artifact-store.cpp
  src/artifact-store/segments.cpp src/pipeline-api/include/mantis/pipeline_api.hpp)
set(recorder_identity "recorder-internal-v1")
foreach(input IN LISTS recorder_inputs)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/${input})
  file(SHA256 ${PROJECT_SOURCE_DIR}/${input} digest)
  string(APPEND recorder_identity "\n${input}:${digest}")
endforeach()
string(SHA256 recorder_digest "${recorder_identity}")
add_library(mantis-recorder-observer STATIC
  ${PROJECT_SOURCE_DIR}/src/device-runtime/recorder-diagnostics.cpp)
target_compile_definitions(mantis-recorder-observer PRIVATE MANTIS_RECORDER_DIAGNOSTICS
  MANTIS_RECORDER_BUILD_ID="${recorder_digest}")
target_link_libraries(mantis-recorder-observer PRIVATE mantis-device-runtime nlohmann_json::nlohmann_json)
add_library(mantis-recorder-store STATIC
  ${PROJECT_SOURCE_DIR}/src/artifact-store/artifact-store.cpp
  ${PROJECT_SOURCE_DIR}/src/artifact-store/segments.cpp)
target_include_directories(mantis-recorder-store PUBLIC ${PROJECT_SOURCE_DIR}/src/artifact-store/include)
target_compile_definitions(mantis-recorder-store PRIVATE MANTIS_RECORDER_DIAGNOSTICS)
target_link_libraries(mantis-recorder-store PUBLIC mantis-artifact-api mantis-platform
  PRIVATE mantis-device-runtime SQLite::SQLite3 nlohmann_json::nlohmann_json)
add_library(mantis-recorder-session OBJECT ${PROJECT_SOURCE_DIR}/src/device-runtime/device-runtime.cpp)
target_compile_definitions(mantis-recorder-session PRIVATE MANTIS_RECORDER_DIAGNOSTICS)
target_link_libraries(mantis-recorder-session PRIVATE mantis-device-runtime nlohmann_json::nlohmann_json)
add_executable(mantis-recorder-tests writer.cpp $<TARGET_OBJECTS:mantis-recorder-session>)
target_compile_definitions(mantis-recorder-tests PRIVATE MANTIS_RECORDER_DIAGNOSTICS)
target_link_libraries(mantis-recorder-tests PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,mantis-recorder-store>" mantis-recorder-observer
  mantis-device-runtime SQLite::SQLite3 nlohmann_json::nlohmann_json)
add_executable(mantis-recorder-mantisd ${PROJECT_SOURCE_DIR}/apps/mantisd/main.cpp
  $<TARGET_OBJECTS:mantis-recorder-session>)
target_link_libraries(mantis-recorder-mantisd PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,mantis-recorder-store>" mantis-recorder-observer mantis-service-adapter)
add_executable(mantis-recorder-disabled-tests writer.cpp)
target_compile_definitions(mantis-recorder-disabled-tests PRIVATE MANTIS_RECORDER_DIAGNOSTICS)
target_link_libraries(mantis-recorder-disabled-tests PRIVATE mantis-recorder-observer
  mantis-artifact-store mantis-device-runtime SQLite::SQLite3 nlohmann_json::nlohmann_json)
add_executable(mantis-recorder-observer-probe observer-probe.cpp)
target_compile_definitions(mantis-recorder-observer-probe PRIVATE MANTIS_RECORDER_DIAGNOSTICS)
target_link_libraries(mantis-recorder-observer-probe PRIVATE mantis-recorder-observer
  mantis-device-runtime nlohmann_json::nlohmann_json)
add_test(NAME recorder-writer-diagnostics COMMAND ${Python3_EXECUTABLE} -B
  ${CMAKE_CURRENT_SOURCE_DIR}/controls.py $<TARGET_FILE:mantis-recorder-tests>
  ${CMAKE_CURRENT_BINARY_DIR}/evidence $<TARGET_FILE:mantis-recorder-observer-probe>
  $<TARGET_FILE:mantis-recorder-disabled-tests>)
set_tests_properties(recorder-writer-diagnostics PROPERTIES TIMEOUT 60)
add_test(NAME recorder-observed-acceptance COMMAND ${Python3_EXECUTABLE} -B
  ${CMAKE_CURRENT_SOURCE_DIR}/acceptance.py ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/acceptance-evidence)
set_tests_properties(recorder-observed-acceptance PROPERTIES TIMEOUT 90)
add_test(NAME recorder-diagnostic-boundaries COMMAND ${Python3_EXECUTABLE} -B
  ${CMAKE_CURRENT_SOURCE_DIR}/boundaries.py $<TARGET_FILE:mantis-device-runtime>
  $<TARGET_FILE:mantis-artifact-store> $<TARGET_FILE:mantisd>
  $<TARGET_FILE:mantis-recorder-mantisd>)
