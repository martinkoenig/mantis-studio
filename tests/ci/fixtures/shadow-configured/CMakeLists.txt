cmake_minimum_required(VERSION 3.25)
project(ShadowMetadataFixture LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
configure_file(generated.cpp.in generated.cpp COPYONLY)
option(MANTIS_BUILD_STUDIO "Studio mode of the configured evidence fixture" OFF)
option(BUILD_TESTING "Testing mode of the configured evidence fixture" ON)
# A tiny stand-in for the product generator: no Protobuf toolchain is needed
# here. Ninja records the real input and both generated outputs.
set(proto "${CMAKE_CURRENT_SOURCE_DIR}/protocol/protobuf/mantis.proto")
add_custom_command(OUTPUT mantis.pb.h mantis.pb.cc
    COMMAND "${CMAKE_COMMAND}" -E copy "${proto}" mantis.pb.h
    COMMAND "${CMAKE_COMMAND}" -E touch mantis.pb.cc
    DEPENDS "${proto}" VERBATIM)
add_library(mantis-wire STATIC mantis.pb.cc mantis.pb.h)
set(python_output "${CMAKE_CURRENT_BINARY_DIR}/python/mantis/mantis_pb2.py")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python/mantis")
configure_file(sdk/python/mantis/__init__.py ${CMAKE_BINARY_DIR}/python/mantis/__init__.py COPYONLY)
configure_file(sdk/python/mantis/calibration.py ${CMAKE_BINARY_DIR}/python/mantis/calibration.py COPYONLY)
add_custom_command(OUTPUT "${python_output}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${python_output}"
    DEPENDS "${proto}" VERBATIM)
add_custom_target(mantis-python ALL DEPENDS "${python_output}")
add_library(core STATIC core.cpp public.hpp)
add_library(objects OBJECT core.cpp)
target_include_directories(core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(middle STATIC middle.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated.cpp" schema.proto view.qml)
set_source_files_properties(schema.proto view.qml PROPERTIES HEADER_FILE_ONLY TRUE)
target_link_libraries(middle PUBLIC core mantis-wire)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE middle)
add_executable(peer peer.cpp)
add_executable(python-consumer python-consumer.cpp)
add_dependencies(python-consumer mantis-python)
add_test(NAME app-contract COMMAND app)
set_tests_properties(app-contract PROPERTIES FIXTURES_REQUIRED prepared)
add_test(NAME peer-contract COMMAND peer)
add_test(NAME python-calibration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/python_calibration.py ${CMAKE_BINARY_DIR})
add_test(NAME python-generator-contract COMMAND python-consumer)
add_test(NAME fixture-setup COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(fixture-setup PROPERTIES FIXTURES_SETUP prepared)
# Mirror only the supported command shape, not the product test behavior.
add_executable(mantis-alg03-numeric-tests python-consumer.cpp)
add_test(NAME alg03-numeric COMMAND mantis-alg03-numeric-tests)
add_test(NAME alg03-enclosure COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/alg03_enclosure.py"
    "$<TARGET_FILE:mantis-alg03-numeric-tests>")
