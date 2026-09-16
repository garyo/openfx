// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <openfx/ofxLog.h>  // openfx::format: "{}" placeholders

#include <iostream>
#include <string_view>

namespace testhost::log {

inline bool verbose = false;

template <typename... Args>
void debug(std::string_view fmt, Args&&... args) {
  if (verbose) std::cerr << "  . " << openfx::format(std::string(fmt), std::forward<Args>(args)...) << "\n";
}

template <typename... Args>
void info(std::string_view fmt, Args&&... args) {
  std::cerr << "  " << openfx::format(std::string(fmt), std::forward<Args>(args)...) << "\n";
}

template <typename... Args>
void warn(std::string_view fmt, Args&&... args) {
  std::cerr << "  ! " << openfx::format(std::string(fmt), std::forward<Args>(args)...) << "\n";
}

template <typename... Args>
void error(std::string_view fmt, Args&&... args) {
  std::cerr << "ERROR: " << openfx::format(std::string(fmt), std::forward<Args>(args)...) << "\n";
}

}  // namespace testhost::log
