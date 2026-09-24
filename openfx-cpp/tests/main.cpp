// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// Unit tests for the openfx-cpp bindings. Arguments select tests by name or by
// source file, so `openfx-cpp-tests test_param` runs one file's worth.

#include <cstdio>
#include <exception>

#include "harness.h"
#include "log_capture.h"

int main(int argc, char** argv) {
  try {
    tests::useRunLogHandler();
    return testing::runAll(argc, argv) == 0 ? 0 : 1;
  } catch (const std::exception& e) {  // the harness itself, not a test
    std::fprintf(stderr, "the test run failed: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "the test run failed\n");
  }
  return 2;
}
