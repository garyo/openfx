// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace openfx {

// String formatter: forward decls
template <typename... Args>
std::string format(const std::string& fmt, Args&&... args);

inline void format_impl(std::ostringstream& oss, const char* fmt);

template <typename T, typename... Args>
void format_impl(std::ostringstream& oss, const char* fmt, T&& value, Args&&... args);

/**
 * @brief Simple, customizable logging facility for the OpenFX API wrapper
 *
 * A log call never throws: whatever formatting the message or the handler
 * throws is swallowed, so logging is safe anywhere, a C boundary included.
 */
class Logger {
 public:
  /**
   * @brief Log levels
   */
  enum class Level {
    Debug,    ///< Debug messages
    Info,     ///< Informational messages
    Warning,  ///< Warning messages
    Error,    ///< Error messages
    Off       ///< Above every message: setLevel(Level::Off) silences the log
  };

  /**
   * @brief Log handler function type
   *
   * @param level The log level
   * @param timestamp Timestamp when log was created
   * @param message The log message
   */
  using LogHandler =
      std::function<void(Level level, std::chrono::system_clock::time_point timestamp,
                         const std::string& message)>;

  /**
   * @brief Set a custom log handler
   *
   * @param handler The log handler function
   */
  static void setLogHandler(LogHandler handler);

  /**
   * @brief Set a context string (e.g., plugin name) to prepend to all log messages
   *
   * @param context The context string (typically plugin name)
   */
  static void setContext(const std::string& context);

  /**
   * @brief Get the current context string
   *
   * @return The current context string
   */
  static std::string getContext();

  /**
   * @brief Set the minimum log level; messages below this level are dropped
   *
   * @param level The minimum log level
   */
  static void setLevel(Level level) noexcept;

  /**
   * @brief Get the current minimum log level
   *
   * @return The current minimum log level
   */
  static Level getLevel() noexcept;

  /**
   * @brief Log a debug message
   *
   * @param format The message, with a {} placeholder for each of @p args
   * @param args Arguments to format
   */
  template <typename... Args>
  static void debug(std::string_view format, Args&&... args) noexcept {
    log(Level::Debug, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Log an informational message
   *
   * @param format The message, with a {} placeholder for each of @p args
   * @param args Arguments to format
   */
  template <typename... Args>
  static void info(std::string_view format, Args&&... args) noexcept {
    log(Level::Info, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Log a warning message
   *
   * @param format The message, with a {} placeholder for each of @p args
   * @param args Arguments to format
   */
  template <typename... Args>
  static void warn(std::string_view format, Args&&... args) noexcept {
    log(Level::Warning, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Log an error message
   *
   * @param format The message, with a {} placeholder for each of @p args
   * @param args Arguments to format
   */
  template <typename... Args>
  static void error(std::string_view format, Args&&... args) noexcept {
    log(Level::Error, format, std::forward<Args>(args)...);
  }

 private:
  /**
   * @brief Log a message with the specified level
   *
   * A message below the current level is dropped before it is formatted.
   *
   * @param level The log level
   * @param format The message, with a {} placeholder for each of @p args
   * @param args Arguments to format
   */
  template <typename... Args>
  static void log(Level level, std::string_view format, Args&&... args) noexcept {
    if (level < getLevel())
      return;
    try {
      const auto timestamp = std::chrono::system_clock::now();
      emit(level, timestamp, format_message(format, std::forward<Args>(args)...));
    } catch (...) {  // NOLINT(bugprone-empty-catch): a log call never throws
    }
  }

  /**
   * @brief Hand a formatted message, with the context before it, to the handler
   */
  static void emit(Level level, std::chrono::system_clock::time_point timestamp,
                   const std::string& message);

  /**
   * @brief Default log handler implementation
   *
   * @param level The log level
   * @param timestamp Timestamp when log was created
   * @param message The message to log
   */
  static void defaultLogHandler(Level level,
                                std::chrono::system_clock::time_point timestamp,
                                const std::string& message);

  /**
   * @brief Format a message using the simple formatter
   *
   * @param format Format string with {} placeholders
   * @param args Arguments to format
   * @return Formatted string
   */
  template <typename... Args>
  static std::string format_message(std::string_view format, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
      return std::string(format);
    } else {
      return ::openfx::format(std::string(format), std::forward<Args>(args)...);
    }
  }

  // Static member variables (inline in C++17)
  static inline LogHandler g_logHandler = defaultLogHandler;
  static inline std::mutex g_logMutex;
  static inline std::string g_context;
  static inline std::atomic<Level> g_logLevel{Level::Info};
};

// Inline implementations for non-template methods

inline void Logger::setLogHandler(LogHandler handler) {
  if (handler) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logHandler = std::move(handler);
  } else {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logHandler = defaultLogHandler;
  }
}

inline void Logger::setContext(const std::string& context) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  g_context = context;
}

inline std::string Logger::getContext() {
  std::lock_guard<std::mutex> lock(g_logMutex);
  return g_context;
}

inline void Logger::setLevel(Level level) noexcept { g_logLevel.store(level); }

inline Logger::Level Logger::getLevel() noexcept { return g_logLevel.load(); }

inline void Logger::emit(Level level, std::chrono::system_clock::time_point timestamp,
                         const std::string& message) {
  // The context and the handler are taken under the lock, but the handler runs
  // without it: the mutex is not recursive, so a handler that logs or reads the
  // context would otherwise deadlock.
  LogHandler handler;
  std::string finalMessage;
  {
    std::lock_guard<std::mutex> lock(g_logMutex);
    finalMessage = g_context.empty() ? message : "[" + g_context + "] " + message;
    handler = g_logHandler;
  }

  if (handler) {
    handler(level, timestamp, finalMessage);
  }
}

inline void Logger::defaultLogHandler(Level level,
                                      std::chrono::system_clock::time_point timestamp,
                                      const std::string& message) {
  // Convert timestamp to local time
  std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
  std::tm local_time;
#ifdef _WIN32
  // Microsoft’s localtime_s expects arguments in reverse order compared to C11's
  // localtime_s:
  localtime_s(&local_time, &time);
#else
  // POSIX
  localtime_r(&time, &local_time);
#endif

  // Level prefix
  const char* levelStr = "";
  switch (level) {
    case Level::Debug:
      levelStr = "DEBUG";
      break;
    case Level::Info:
      levelStr = "INFO";
      break;
    case Level::Warning:
      levelStr = "⚠️WARN";
      break;
    case Level::Error:
      levelStr = "❌ERROR";
      break;
    case Level::Off:  // no message is logged at Off
      break;
  }

  // Write formatted log message to stdout (not stderr)
  // In plugin contexts, stderr may not be captured by host applications
  std::cout << "[" << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "][" << levelStr
            << "] " << message << '\n'
            << std::flush;
}

/**
 * @brief String formatter with {} placeholders (similar to std::format)
 *
 * @param fmt Format string with {} placeholders
 * @param args Arguments to format
 * @return Formatted string
 */
template <typename... Args>
std::string format(const std::string& fmt, Args&&... args) {
  std::ostringstream oss;
  format_impl(oss, fmt.c_str(), std::forward<Args>(args)...);
  return oss.str();
}

/**
 * @brief Implementation detail for the string formatter
 *
 * @param oss Output string stream
 * @param fmt Format string
 */
inline void format_impl(std::ostringstream& oss, const char* fmt) { oss << fmt; }

/**
 * @brief Implementation detail for the string formatter
 *
 * @param oss Output string stream
 * @param fmt Format string
 * @param value Value to format
 * @param args Remaining arguments
 */
template <typename T, typename... Args>
void format_impl(std::ostringstream& oss, const char* fmt, T&& value, Args&&... args) {
  const char* pos = std::strchr(fmt, '{');

  // Check if we have a valid placeholder with closing }
  if (pos && pos[1] == '}') {
    // Write everything up to the placeholder
    oss.write(fmt, pos - fmt);
    // Write the value
    oss << value;
    // Continue with the rest of the format string
    format_impl(oss, pos + 2, std::forward<Args>(args)...);
  } else {
    // No valid placeholder, just write the rest of the format string
    oss << fmt;
  }
}

}  // namespace openfx
