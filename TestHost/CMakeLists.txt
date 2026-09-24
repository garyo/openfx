# SPDX-License-Identifier: BSD-3-Clause

# ofxtesthost: a command-line host for exercising plugins, built on the
# openfx-cpp host headers at C++20. See README.md.

if(NOT cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(STATUS "TestHost: the compiler has no C++20, skipping ofxtesthost")
  return()
endif()

find_package(Threads REQUIRED)

file(GLOB OFX_TESTHOST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(ofxtesthost ${OFX_TESTHOST_SOURCES})
target_compile_features(ofxtesthost PRIVATE cxx_std_20)
target_link_libraries(ofxtesthost PRIVATE OpenFX::openfx-cpp Threads::Threads
                                          ${CMAKE_DL_LIBS})
