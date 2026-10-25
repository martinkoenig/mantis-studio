add_executable(mantis-x1-f2-reference-tests reference.cpp)
target_link_libraries(mantis-x1-f2-reference-tests PRIVATE mantis-x1-f2-host mantis-options Threads::Threads)
add_test(NAME x1-f2-reference-interoperability COMMAND mantis-x1-f2-reference-tests)
set_tests_properties(x1-f2-reference-interoperability PROPERTIES TIMEOUT 30)

# External sources are compiled only into this opt-in, uninstalled test executable.
option(MANTIS_F2_LIVE_INTEROP "Test Studio F2 against the exact accepted firmware source" OFF)
set(MANTIS_F2_FIRMWARE_SOURCE_DIR "" CACHE PATH "Read-only exact-SHA firmware checkout for live tests")
if(MANTIS_F2_LIVE_INTEROP)
  find_package(Git REQUIRED)
  set(FIRMWARE_DIR "${MANTIS_F2_FIRMWARE_SOURCE_DIR}")
  include(${CMAKE_CURRENT_SOURCE_DIR}/verify-firmware.cmake)
  add_custom_target(mantis-x1-f2-verify-firmware
    COMMAND ${CMAKE_COMMAND} "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
      "-DFIRMWARE_DIR=${MANTIS_F2_FIRMWARE_SOURCE_DIR}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/verify-firmware.cmake
    VERBATIM)
  add_executable(mantis-x1-f2-live-tests live.cpp firmware-transport.cpp
    ${MANTIS_F2_FIRMWARE_SOURCE_DIR}/shared/protocol/cpp/src/transport.cpp
    ${MANTIS_F2_FIRMWARE_SOURCE_DIR}/shared/protocol/cpp/src/messages.cpp
    ${MANTIS_F2_FIRMWARE_SOURCE_DIR}/shared/controller/cpp/src/controller.cpp)
  target_include_directories(mantis-x1-f2-live-tests PRIVATE
    ${MANTIS_F2_FIRMWARE_SOURCE_DIR}/shared/protocol/cpp/include
    ${MANTIS_F2_FIRMWARE_SOURCE_DIR}/shared/controller/cpp/include)
  target_link_libraries(mantis-x1-f2-live-tests PRIVATE mantis-x1-f2-host mantis-options Threads::Threads)
  add_dependencies(mantis-x1-f2-live-tests mantis-x1-f2-verify-firmware)
  add_test(NAME x1-f2-live-interoperability COMMAND mantis-x1-f2-live-tests
    --check-fixtures ${CMAKE_CURRENT_SOURCE_DIR}/wire-fixtures.hpp)
  set_tests_properties(x1-f2-live-interoperability PROPERTIES TIMEOUT 30)
endif()
set(interop_binaries $<TARGET_FILE:mantis-x1-f2-reference-tests>)
if(MANTIS_F2_LIVE_INTEROP)
  list(APPEND interop_binaries $<TARGET_FILE:mantis-x1-f2-live-tests>)
endif()
add_test(NAME x1-f2-interoperability-boundaries COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/tests/contract/x1_f2_interop_boundaries.py ${PROJECT_SOURCE_DIR} ${interop_binaries})
