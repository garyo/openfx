// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>

#include <exception>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "ofxLog.h"
#include "ofxStatusStrings.h"

namespace openfx {

/**
 * @brief Base exception class for the OpenFX API wrapper
 */
class OfxException : public std::runtime_error {
 public:
  /**
   * @brief Construct a new API exception
   * @param code Error code from the C API
   * @param message Error message
   */
  OfxException(OfxStatus status, const std::string& msg)
      : std::runtime_error(createMessage(msg, status)), error_code_(status) {}

  /**
   * @brief Get the error code
   * @return The error code from the C API
   */
  OfxStatus code() const noexcept { return error_code_; }

 private:
  OfxStatus error_code_;
  static std::string createMessage(const std::string& msg, OfxStatus status) {
    std::ostringstream oss;
    oss << "OpenFX error: " << msg << ": " << ofxStatusToString(status);
    return oss.str();
  }
};

/**
 * @brief Exception thrown when a required property isn't found
 */
class PropertyNotFoundException : public OfxException {
 public:
  explicit PropertyNotFoundException(int code, const std::string& msg = "")
      : OfxException(code, msg) {}
};

/**
 * @brief Exception thrown when an clip isn't found
 */
class ClipNotFoundException : public OfxException {
 public:
  explicit ClipNotFoundException(int code, const std::string& msg = "")
      : OfxException(code, msg) {}
};

/**
 * @brief Exception thrown when an image isn't found
 */
class ImageNotFoundException : public OfxException {
 public:
  explicit ImageNotFoundException(int code, const std::string& msg = "")
      : OfxException(code, msg) {}
};

/**
 * @brief Exception thrown when a suite isn't found
 */
class SuiteNotFoundException : public OfxException {
 public:
  explicit SuiteNotFoundException(int code, const std::string& msg = "")
      : OfxException(code, msg) {}
};

// ---------------------------------------------------------------------------
// The C boundary
// ---------------------------------------------------------------------------
//
// Every function the other side of the API calls through a C function pointer
// -- a suite entry a host provides, a plugin's setHost and main entry, an
// overlay's entry point, a thread function -- must not let a C++ exception
// out: unwinding into a C caller is undefined behaviour. These three turn an
// exception into what a C caller can understand, and never throw themselves.

/**
 * @brief The status for the exception being handled
 *
 * For use inside a catch block: an OfxException becomes its code(),
 * std::bad_alloc becomes kOfxStatErrMemory, and anything else, or no exception
 * at all, becomes @p fallback.
 */
inline OfxStatus statusFromCurrentException(OfxStatus fallback) noexcept {
  const std::exception_ptr error = std::current_exception();
  if (!error)
    return fallback;
  try {
    std::rethrow_exception(error);
  } catch (const OfxException& e) {
    return e.code();
  } catch (const std::bad_alloc&) {
    return kOfxStatErrMemory;
  } catch (...) {
    return fallback;
  }
}

/**
 * @brief Log the exception being handled, as "<context>: <what()>"
 *
 * For use inside a catch block. @p context is a Logger format string, and
 * @p args fill its {} placeholders. Logging is best effort: if formatting the
 * message fails or the log handler throws, that is swallowed too.
 */
template <typename... Args>
void logCurrentException(const char* context, const Args&... args) noexcept {
  try {
    const std::exception_ptr error = std::current_exception();
    if (!error)
      return;
    const std::string where = ::openfx::format(context, args...);
    try {
      std::rethrow_exception(error);
    } catch (const std::exception& e) {
      Logger::error("{}: {}", where, e.what());
    } catch (...) {
      Logger::error("{}: unknown exception", where);
    }
  } catch (...) {  // NOLINT(bugprone-empty-catch): nowhere left to report it
  }
}

/**
 * @brief Run @p f, which returns an OfxStatus, so that no exception escapes
 *
 * The body of a C entry point: an exception out of @p f is logged and becomes
 * a status as statusFromCurrentException() maps it, with @p fallback for
 * anything that is neither an OfxException nor an allocation failure.
 */
template <class F>
OfxStatus callAtCBoundary(F&& f, OfxStatus fallback = kOfxStatFailed) noexcept {
  try {
    return std::forward<F>(f)();
  } catch (...) {
    logCurrentException("exception at a C API boundary");
    return statusFromCurrentException(fallback);
  }
}

}  // namespace openfx
