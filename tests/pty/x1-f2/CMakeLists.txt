if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()
# Only BUILD_TESTING reaches this directory. No product/installation linkage.
add_library(mantis-x1-f2-posix STATIC
  ${PROJECT_SOURCE_DIR}/plugins/first-party/devices/x1/f2-posix/fd-transport.cpp)
target_link_libraries(mantis-x1-f2-posix PUBLIC mantis-x1-f2-host PRIVATE mantis-options Threads::Threads)
target_compile_definitions(mantis-x1-f2-posix PUBLIC MANTIS_F2_POSIX_TESTING)
add_executable(mantis-x1-f2-pty-tests transport.cpp peer.cpp)
target_link_libraries(mantis-x1-f2-pty-tests PRIVATE mantis-x1-f2-posix mantis-x1-f2-simulation mantis-options Threads::Threads)
add_test(NAME x1-f2-pty COMMAND mantis-x1-f2-pty-tests)
set_tests_properties(x1-f2-pty PROPERTIES TIMEOUT 45)
add_executable(mantis-x1-f2-pty-abandonment-tests abandonment.cpp peer.cpp)
target_link_libraries(mantis-x1-f2-pty-abandonment-tests PRIVATE mantis-x1-f2-posix mantis-x1-f2-simulation mantis-options Threads::Threads)
add_test(NAME x1-f2-pty-abandonment COMMAND mantis-x1-f2-pty-abandonment-tests)
set_tests_properties(x1-f2-pty-abandonment PROPERTIES TIMEOUT 45)
add_executable(mantis-x1-f2-pty-performance performance.cpp)
target_link_libraries(mantis-x1-f2-pty-performance PRIVATE mantis-x1-f2-posix mantis-options Threads::Threads)
add_test(NAME x1-f2-pty-performance COMMAND mantis-x1-f2-pty-performance)
set_tests_properties(x1-f2-pty-performance PROPERTIES TIMEOUT 30)
add_test(NAME x1-f2-posix-boundaries COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/tests/contract/x1_f2_posix_boundaries.py ${PROJECT_SOURCE_DIR}
  $<TARGET_FILE:mantis-x1-f2-posix> $<TARGET_FILE:mantis-x1> $<TARGET_FILE:mantis-x1-f2-fixture>)
# Borrowed-FD terminal policy is separate from the accepted no-termios transport.
add_library(mantis-x1-f2-serial-settings STATIC
  ${PROJECT_SOURCE_DIR}/plugins/first-party/devices/x1/f2-serial/serial-settings.cpp)
target_link_libraries(mantis-x1-f2-serial-settings PRIVATE mantis-options)
add_executable(mantis-x1-f2-serial-settings-tests serial-settings.cpp peer.cpp)
target_link_libraries(mantis-x1-f2-serial-settings-tests PRIVATE
  mantis-x1-f2-serial-settings mantis-x1-f2-posix mantis-x1-f2-simulation mantis-options Threads::Threads)
target_link_options(mantis-x1-f2-serial-settings-tests PRIVATE
  "LINKER:--wrap=tcsetattr" "LINKER:--wrap=tcgetattr")
add_test(NAME x1-f2-serial-settings COMMAND mantis-x1-f2-serial-settings-tests)
set_tests_properties(x1-f2-serial-settings PROPERTIES TIMEOUT 45)
add_test(NAME x1-uart-readiness COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/tests/unit/x1_uart_readiness.py)
add_test(NAME x1-uart-readiness-boundaries COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/tests/contract/x1_uart_readiness_boundaries.py ${PROJECT_SOURCE_DIR}
  $<TARGET_FILE:mantis-x1-f2-serial-settings> $<TARGET_FILE:mantis-x1>
  $<TARGET_FILE:mantisd> $<TARGET_FILE:mantis-x1-f2-fixture> $<TARGET_FILE:mantis-x1-f2-host>)
# Separate daemon-loadable PTY fixture. Same frozen plugin implementation, with
# test-only internal transport composition; never installed or linked into X1.
set_target_properties(mantis-x1-f2-posix mantis-x1-f2-serial-settings PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(mantis-x1-f2-pty-backend STATIC ../../fixtures/x1-f2-pty/backend.cpp)
set_target_properties(mantis-x1-f2-pty-backend PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(mantis-x1-f2-pty-backend PUBLIC mantis-x1-f2-posix mantis-x1-f2-simulation
  PRIVATE mantis-x1-f2-serial-settings mantis-options Threads::Threads)
add_library(mantis-x1-f2-pty-fixture MODULE ../../fixtures/x1-f2/plugin.cpp
  ${PROJECT_SOURCE_DIR}/plugins/first-party/devices/x1/profile.cpp
  ${PROJECT_SOURCE_DIR}/plugins/first-party/devices/x1/media.cpp
  ${PROJECT_SOURCE_DIR}/plugins/first-party/devices/x1/fake.cpp)
target_compile_definitions(mantis-x1-f2-pty-fixture PRIVATE MANTIS_X1_F2_PTY_FIXTURE)
target_link_libraries(mantis-x1-f2-pty-fixture PRIVATE mantis-x1-f2-pty-backend mantis-sdk-cpp
  mantis-options nlohmann_json::nlohmann_json Threads::Threads)
set_target_properties(mantis-x1-f2-pty-fixture PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/x1-f2-pty-plugins)
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/x1-f2-pty-plugins/x1.json CONTENT
  "{\"manifest_version\":1,\"id\":\"org.mantis.x1\",\"version\":\"1.1.0\",\"kind\":\"device\",\"execution\":\"in_process\",\"abi_version\":1,\"library\":\"$<TARGET_FILE_NAME:mantis-x1-f2-pty-fixture>\",\"permissions\":[]}")
add_executable(mantis-x1-f2-pty-fixture-tests ../../integration/x1_f2_fixture.cpp)
target_compile_definitions(mantis-x1-f2-pty-fixture-tests PRIVATE MANTIS_X1_F2_PTY_FIXTURE)
target_link_libraries(mantis-x1-f2-pty-fixture-tests PRIVATE mantis-plugin-runtime nlohmann_json::nlohmann_json)
add_test(NAME x1-f2-pty-fixture COMMAND mantis-x1-f2-pty-fixture-tests
  $<TARGET_FILE:mantis-x1-f2-pty-fixture> ${CMAKE_BINARY_DIR}/plugins
  ${PROJECT_SOURCE_DIR}/tests/fixtures/x1-projected/profile.json ${PROJECT_SOURCE_DIR}/tests/fixtures/x1-f2/config.json)
set_tests_properties(x1-f2-pty-fixture PROPERTIES TIMEOUT 45)
add_executable(mantis-x1-f2-pty-client-tests ../../integration/x1_f2_client.cpp)
target_compile_definitions(mantis-x1-f2-pty-client-tests PRIVATE MANTIS_X1_F2_PTY_FIXTURE)
target_link_libraries(mantis-x1-f2-pty-client-tests PRIVATE mantis-client mantis-service-adapter)
add_test(NAME x1-f2-pty-end-to-end COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/tests/integration/x1_f2.py ${CMAKE_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}/tests/fixtures/x1-projected/profile.json
  ${PROJECT_SOURCE_DIR}/tests/fixtures/x1-f2/config.json --pty)
set_tests_properties(x1-f2-pty-end-to-end PROPERTIES TIMEOUT 180)
add_executable(mantis-x1-f2-pty-backend-tests daemon-backend.cpp)
target_link_libraries(mantis-x1-f2-pty-backend-tests PRIVATE mantis-x1-f2-pty-backend mantis-options Threads::Threads)
target_link_options(mantis-x1-f2-pty-backend-tests PRIVATE "LINKER:--wrap=eventfd")
add_test(NAME x1-f2-pty-backend COMMAND mantis-x1-f2-pty-backend-tests)
set_tests_properties(x1-f2-pty-backend PROPERTIES TIMEOUT 45)
add_test(NAME x1-f2-pty-daemon-boundaries COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/tests/contract/x1_f2_pty_daemon_boundaries.py ${PROJECT_SOURCE_DIR}
  $<TARGET_FILE:mantis-x1-f2-pty-fixture> $<TARGET_FILE:mantis-x1-f2-fixture>
  $<TARGET_FILE:mantis-x1> $<TARGET_FILE:mantisd> $<TARGET_FILE:mantis-x1-f2-pty-client-tests>)
