cmake_minimum_required(VERSION 3.25)
project(ShadowMetadataFixture LANGUAGES CXX)
enable_testing()
configure_file(generated.cpp.in generated.cpp COPYONLY)
option(MANTIS_BUILD_STUDIO "Studio mode of the configured evidence fixture" OFF)
# A tiny stand-in for the product generator: no Protobuf toolchain is needed
# here. Ninja records the real input and both generated outputs.
set(proto "${CMAKE_CURRENT_SOURCE_DIR}/protocol/protobuf/mantis.proto")
add_custom_command(OUTPUT mantis.pb.h mantis.pb.cc
    COMMAND "${CMAKE_COMMAND}" -E copy "${proto}" mantis.pb.h
    COMMAND "${CMAKE_COMMAND}" -E touch mantis.pb.cc
    DEPENDS "${proto}" VERBATIM)
add_library(mantis-wire STATIC mantis.pb.cc mantis.pb.h)
add_library(core STATIC core.cpp public.hpp)
add_library(objects OBJECT core.cpp)
target_include_directories(core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(middle STATIC middle.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated.cpp" schema.proto view.qml)
set_source_files_properties(schema.proto view.qml PROPERTIES HEADER_FILE_ONLY TRUE)
target_link_libraries(middle PUBLIC core mantis-wire)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE middle)
add_executable(peer peer.cpp)
add_test(NAME app-contract COMMAND app)
set_tests_properties(app-contract PROPERTIES FIXTURES_REQUIRED prepared)
add_test(NAME peer-contract COMMAND peer)
add_test(NAME fixture-setup COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(fixture-setup PROPERTIES FIXTURES_SETUP prepared)
