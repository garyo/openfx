cmake_minimum_required(VERSION 3.16.0)
if(OFX_SUPPORTS_CUDARENDER AND NOT APPLE)
  project(
    openfx
    VERSION 1.5.1
    LANGUAGES CXX CUDA)
else()
  project(
    openfx
    VERSION 1.5.1
    LANGUAGES CXX) # no CUDA
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_definitions(-D_HAS_AUTO_PTR_ETC)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

option(OFX_SUPPORTS_OPENGLRENDER
       "Build with support for GPU rendering (OpenGL/CUDA/Metal/OpenCL)" ON)
option(BUILD_EXAMPLE_PLUGINS "Build example plugins" OFF)
option(OFX_SUPPORTS_OPENCLRENDER
       "Build examples with support for OpenCL GPU rendering" OFF)
option(OFX_SUPPORTS_CUDARENDER
       "Build examples with support for CUDA GPU rendering" OFF)
option(PLUGIN_INSTALLDIR "Location to install plugins (default depends on OS)"
       "")
option(OFX_BUILD_OPENFX_CPP_CHECK
       "Compile-check the openfx-cpp C++ bindings" ON)
option(OFX_BUILD_OPENFX_CPP_TESTS
       "Build the openfx-cpp unit tests and ofxtesthost, with CTest tests" ON)

if(APPLE)
  set(OFX_SUPPORTS_CUDARENDER OFF)
endif()

if(OFX_SUPPORTS_OPENGLRENDER)
  add_definitions(-DOFX_SUPPORTS_OPENGLRENDER)
endif()
if(OFX_SUPPORTS_OPENCLRENDER)
  find_package(OpenCLHeaders REQUIRED)
  find_package(OpenCLICDLoader REQUIRED)
  add_definitions(-DOFX_SUPPORTS_OPENCLRENDER)
endif()
if(OFX_SUPPORTS_CUDARENDER)
  add_definitions(-DOFX_SUPPORTS_CUDARENDER)
  set(CMAKE_CUDA_ARCHITECTURES "all-major")
endif()

# We use #if DEBUG in the examples
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

# Flags
if(NOT MSVC)
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-deprecated -Wno-deprecated-declarations"
  )
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${CMAKE_BINARY_DIR})

# Conan packages
find_package(EXPAT)
if(BUILD_EXAMPLE_PLUGINS)
  # Used only by the example plugins, not by the OpenFX libraries.
  find_package(opengl_system REQUIRED)
  find_package(cimg REQUIRED)
  find_package(spdlog REQUIRED)
endif()

# Macros
include(OpenFX)
set_property(GLOBAL PROPERTY OFX_PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

# The plugins' bundles in the build tree (see add_ofx_plugin), which a host
# such as ofxtesthost can load without installing them.
get_property(OFX_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(OFX_MULTI_CONFIG)
  set(OFX_PLUGIN_BUILD_DIR "${PROJECT_BINARY_DIR}/plugins/$<CONFIG>")
else()
  set(OFX_PLUGIN_BUILD_DIR "${PROJECT_BINARY_DIR}/plugins")
endif()

# C Headers
set(OFX_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
file(GLOB_RECURSE OFX_HEADER_FILES "${OFX_HEADER_DIR}/*.h")

# Defines
if(OFX_SUPPORTS_OPENGLRENDER)
  add_compile_definitions(OFX_SUPPORTS_OPENGLRENDER)
endif()

if(OFX_BUILD_OPENFX_CPP_TESTS)
  enable_testing()
endif()

# Build
add_subdirectory(openfx-cpp)
add_subdirectory(HostSupport)
add_subdirectory(Support)
if(BUILD_EXAMPLE_PLUGINS)
  add_subdirectory(Examples)
endif()
if(OFX_BUILD_OPENFX_CPP_TESTS)
  add_subdirectory(TestHost)
endif()
