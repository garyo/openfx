# SPDX-License-Identifier: BSD-3-Clause

# openfx-cpp-tests: unit tests for the bindings. The plugin-side wrappers run
# against the host-side objects in process, so they need no plugin bundles.
# The program takes source file names as filters, which makes each test_*.cpp
# a test of its own. It is built at C++20, and at C++17 as
# openfx-cpp-tests-cxx17, where openfx/ofxSpan.h stands tcb-span in for
# std::span.

find_package(Threads REQUIRED)

file(GLOB OFX_CPP_TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB OFX_CPP_TEST_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

# Build TARGET at STANDARD, with a test PREFIX.NAME for each test_NAME.cpp.
function(ofx_cpp_add_tests TARGET STANDARD PREFIX)
  add_executable(${TARGET} ${OFX_CPP_TEST_SOURCES})
  target_link_libraries(${TARGET} PRIVATE OpenFX::openfx-cpp Threads::Threads)
  set_target_properties(
    ${TARGET} PROPERTIES CXX_STANDARD ${STANDARD} CXX_STANDARD_REQUIRED ON
                         CXX_EXTENSIONS OFF FOLDER openfx-cpp)
  foreach(source IN LISTS OFX_CPP_TEST_FILES)
    get_filename_component(file "${source}" NAME)
    get_filename_component(stem "${source}" NAME_WE)
    string(REGEX REPLACE "^test_" "" name "${stem}")
    add_test(NAME ${PREFIX}.${name} COMMAND ${TARGET} ${file})
    set_tests_properties(${PREFIX}.${name} PROPERTIES LABELS openfx-cpp)
  endforeach()
endfunction()

if(cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  ofx_cpp_add_tests(openfx-cpp-tests 20 openfx-cpp)
else()
  message(STATUS "openfx-cpp: the compiler has no C++20, skipping "
                 "openfx-cpp-tests")
endif()
if(TARGET tcb-span::tcb-span)
  ofx_cpp_add_tests(openfx-cpp-tests-cxx17 17 openfx-cpp.cxx17)
else()
  message(
    STATUS "openfx-cpp: tcb-span not found, skipping openfx-cpp-tests-cxx17 "
           "(openfx/ofxSpan.h needs it below C++20)")
endif()
