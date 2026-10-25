# Isolated fixtures only; no installation, product linkage or activation knobs.
add_library(mantis-project-paced-scanner MODULE paced-scanner.cpp)
target_link_libraries(mantis-project-paced-scanner PRIVATE mantis-sdk-cpp mantis-options Threads::Threads)
set_target_properties(mantis-project-paced-scanner PROPERTIES PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/project-capabilities-plugins")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/project-capabilities-plugins/virtual-scanner.json" CONTENT
  "{\"manifest_version\":1,\"id\":\"org.mantis.virtual-scanner\",\"version\":\"0.1.0\",\"abi_version\":1,\"library\":\"$<TARGET_FILE_NAME:mantis-project-paced-scanner>\",\"kind\":\"device\",\"execution\":\"in_process\",\"permissions\":[\"synthetic\"]}\n")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(mantis-project-capabilities-mantisd daemon.cpp)
  target_link_libraries(mantis-project-capabilities-mantisd PRIVATE mantis-service-adapter)
  target_link_options(mantis-project-capabilities-mantisd PRIVATE "LINKER:--wrap=mmap")
  add_test(NAME project-public-capabilities-negative-writer COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/project_capabilities.py ${CMAKE_BINARY_DIR} --negative-writer)
  set_tests_properties(project-public-capabilities-negative-writer PROPERTIES TIMEOUT 45)
  target_compile_definitions(mantis-project-capabilities-tests PRIVATE MANTIS_CAPABILITIES_REPLAY_GATE)
endif()
