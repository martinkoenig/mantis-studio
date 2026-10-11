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
