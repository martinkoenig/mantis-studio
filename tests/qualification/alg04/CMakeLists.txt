# Native Linux software qualification in the existing serial Architecture CTest suite.
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
 return()
endif()
add_executable(mantis-alg04-recorded-tests recorded.cpp probe.cpp ../../support/observed_wait.cpp)
target_include_directories(mantis-alg04-recorded-tests SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(mantis-alg04-recorded-tests PRIVATE mantis-alg03 mantis-calibration-solver-opencv
 mantis-plugin-runtime opencv_core opencv_calib3d nlohmann_json::nlohmann_json)
add_dependencies(mantis-alg04-recorded-tests virtual-scanner example-points mantis-plugin-host)
target_compile_definitions(mantis-alg04-recorded-tests PRIVATE MANTIS_TEST_WAIT_WRAP)
target_link_options(mantis-alg04-recorded-tests PRIVATE
 "LINKER:--wrap=_ZNSt18condition_variable4waitERSt11unique_lockISt5mutexE"
 "LINKER:--wrap=pthread_cond_clockwait"
 "LINKER:--wrap=pthread_cond_timedwait"
 "LINKER:--wrap=mmap" "LINKER:--wrap=mmap64" "LINKER:--wrap=munmap"
 "LINKER:--wrap=memcpy" "LINKER:--wrap=memmove")
add_test(NAME alg04-recorded-qualification COMMAND "${Python3_EXECUTABLE}" -B
 "${CMAKE_CURRENT_SOURCE_DIR}/campaign.py" "$<TARGET_FILE:mantis-alg04-recorded-tests>"
 "${CMAKE_BINARY_DIR}/plugins" "$<TARGET_FILE:mantis-plugin-host>"
 "$<CONFIG>" "${MANTIS_BUILD_STUDIO}" "${MANTIS_SANITIZE}" "${CMAKE_CXX_COMPILER_ID}" "${CMAKE_CXX_COMPILER_VERSION}"
 "${CMAKE_BINARY_DIR}/CMakeCache.txt")
set_tests_properties(alg04-recorded-qualification PROPERTIES TIMEOUT 120)
