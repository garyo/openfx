// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// A self-contained test harness: TEST_CASE registers a function, CHECK records
// a failure with its file and line, and runAll() runs the registered tests and
// returns the number of failures, which main() hands back as its exit code.

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
  const char* name;
  const char* file;
  void (*run)();
};

inline std::vector<TestCase>& tests() {
  static std::vector<TestCase> all;
  return all;
}
inline int& failures() {
  static int count = 0;
  return count;
}

struct Registrar {
  Registrar(const char* name, const char* file, void (*run)()) {
    tests().push_back({name, file, run});
  }
};
inline void fail(const char* file, int line, const std::string& what) {
  std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
  ++failures();
}

// Runs the tests whose name or file contains one of the command-line filters,
// or every test if none is given. Returns the number of failures.
inline int runAll(int argc, char** argv) {
  const std::vector<std::string> filters(argv + 1, argv + argc);
  int selected = 0;
  for (const TestCase& test : tests()) {
    bool wanted = filters.empty();
    for (const std::string& filter : filters)
      wanted = wanted || std::string(test.name).find(filter) != std::string::npos ||
               std::string(test.file).find(filter) != std::string::npos;
    if (!wanted)
      continue;
    ++selected;
    const int before = failures();
    try {
      test.run();
    } catch (const std::exception& e) {
      fail(test.file, 0, std::string(test.name) + " threw: " + e.what());
    } catch (...) {
      fail(test.file, 0, std::string(test.name) + " threw an unknown exception");
    }
    if (failures() != before)
      std::printf("not ok - %s\n", test.name);
  }
  std::printf("%d test%s run, %d failure%s\n", selected, selected == 1 ? "" : "s",
              failures(), failures() == 1 ? "" : "s");
  return failures();
}

}  // namespace testing

// Variadic, so a comma in a template argument list still arrives as one condition.
#define CHECK(...)                                                  \
  do {                                                              \
    if (!(__VA_ARGS__))                                             \
      testing::fail(__FILE__, __LINE__, "CHECK(" #__VA_ARGS__ ")"); \
  } while (0)

// The expression must throw an exception of this type, or one derived from it.
#define CHECK_THROWS_AS(expr, ExceptionType)                                     \
  do {                                                                           \
    bool threw = false;                                                          \
    try {                                                                        \
      (void)(expr);                                                              \
    } catch (const ExceptionType&) {                                             \
      threw = true;                                                              \
    } catch (...) {                                                              \
    }                                                                            \
    if (!threw)                                                                  \
      testing::fail(__FILE__, __LINE__, #expr " did not throw " #ExceptionType); \
  } while (0)

#define TEST_CASE(name)                                                     \
  static void name();                                                       \
  static const testing::Registrar name##_registrar(#name, __FILE__, &name); \
  static void name()
