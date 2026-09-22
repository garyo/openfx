// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// Unit tests for the openfx-cpp bindings. Arguments select tests by name or by
// source file, so `openfx-cpp-tests test_param` runs one file's worth.

#include <openfx/ofxLog.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "harness.h"

int main(int argc, char** argv) {
  try {
    // Many of the tests are about what the bindings do with a bad property
    // name, a wrong type or a missing suite, and each of those logs on its way
    // to the status code the test checks. The log would drown the results, so
    // it goes nowhere unless OPENFX_TEST_LOG is set.
    if (!std::getenv("OPENFX_TEST_LOG"))
      openfx::Logger::setLogHandler([](openfx::Logger::Level,
                                       std::chrono::system_clock::time_point,
                                       const std::string&) {});
    return testing::runAll(argc, argv) == 0 ? 0 : 1;
  } catch (const std::exception& e) {  // the harness itself, not a test
    std::fprintf(stderr, "the test run failed: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "the test run failed\n");
  }
  return 2;
}
