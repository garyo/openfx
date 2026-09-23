// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// The log handler the test run uses, and a capture of the log for the tests
// that are about what gets logged.

#include <openfx/ofxLog.h>

#include <chrono>
#include <cstdlib>
#include <string>

namespace tests {

// Many of the tests are about what the bindings do with a bad property name,
// a wrong type or a missing suite, and each of those logs on its way to the
// status code the test checks. The log would drown the results, so it goes
// nowhere unless OPENFX_TEST_LOG is set, when the default handler prints it.
inline void useRunLogHandler() {
  openfx::Logger::setLogHandler(
      std::getenv("OPENFX_TEST_LOG")
          ? openfx::Logger::LogHandler()
          : openfx::Logger::LogHandler([](openfx::Logger::Level,
                                          std::chrono::system_clock::time_point,
                                          const std::string&) {}));
}

// Collects what is logged at the given level and above while it lives, one
// message per line, then puts back the run's handler and level.
class LogCapture {
 public:
  explicit LogCapture(openfx::Logger::Level level = openfx::Logger::getLevel())
      : runLevel_(openfx::Logger::getLevel()) {
    openfx::Logger::setLevel(level);
    openfx::Logger::setLogHandler(
        [this](openfx::Logger::Level, std::chrono::system_clock::time_point,
               const std::string& message) { messages += message + "\n"; });
  }
  ~LogCapture() {
    useRunLogHandler();
    openfx::Logger::setLevel(runLevel_);
  }
  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;

  std::string messages;

 private:
  openfx::Logger::Level runLevel_;
};

}  // namespace tests
