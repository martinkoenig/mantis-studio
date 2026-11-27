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

add_test(NAME alg03-public-ply COMMAND "${Python3_EXECUTABLE}" -B
 "${CMAKE_CURRENT_SOURCE_DIR}/public_ply.py" "${CMAKE_BINARY_DIR}"
 "$<TARGET_FILE:mantis-alg04-recorded-tests>" "$<TARGET_FILE:mantisd>" "$<TARGET_FILE:mantis-cli>"
 "$<TARGET_FILE:mantis-alg03-ply-probe-daemon>")
set_tests_properties(alg03-public-ply PROPERTIES TIMEOUT 90)

# Same daemon main/adapter/runtime and same loaded exporter, with link-only
# observation and an entered-state gate after the real temporary-file sync.
add_executable(mantis-alg03-ply-probe-daemon ../../../apps/mantisd/main.cpp ply_probe.cpp)
target_link_libraries(mantis-alg03-ply-probe-daemon PRIVATE mantis-service-adapter mantis-alg03)
add_dependencies(mantis-alg03-ply-probe-daemon mantisd mantis-cli mantis-python ply-exporter mantis-alg04-recorded-tests)
set(ply_load _ZN6mantis5alg0311load_resultERKNS_8artifact5StoreERKNS_2IdERKNS_17CancellationTokenE)
set(ply_export _ZN6mantis7plugins8Registry11export_fileERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERKNS_4data6PacketERKNSt10filesystem7__cxx114pathERKNS_17CancellationTokenE)
set(ply_sync _ZN6mantis8platform12durable_fileERKNSt10filesystem7__cxx114pathE)
target_compile_definitions(mantis-alg03-ply-probe-daemon PRIVATE
 PLY_LOAD="${ply_load}" PLY_EXPORT="${ply_export}" PLY_SYNC="${ply_sync}")
target_link_options(mantis-alg03-ply-probe-daemon PRIVATE
 "LINKER:--wrap=${ply_load}" "LINKER:--wrap=${ply_export}" "LINKER:--wrap=${ply_sync}"
 "LINKER:--wrap=_ZnwmSt11align_val_t" "LINKER:--wrap=memcpy" "LINKER:--wrap=memmove")

add_executable(mantis-alg03-ply-rounding-tests ply_rounding.cpp)
target_link_libraries(mantis-alg03-ply-rounding-tests PRIVATE mantis-services)
add_test(NAME alg03-ply-rounding COMMAND mantis-alg03-ply-rounding-tests)
set_tests_properties(alg03-ply-rounding PROPERTIES TIMEOUT 10)
