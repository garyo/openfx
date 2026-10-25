# OFX API Wrapper Logging System


## Overview

The OFX API Wrapper provides a lightweight, thread-safe customizable logging system. It supports very simple `fmt`-style formatting.

## Basic Usage

```cpp
#include "openfx/ofxLog.h"

// Log simple messages
openfx::Logger::debug("Entering initialization routine");
openfx::Logger::info("Application started");
openfx::Logger::warn("Resource usage is high");
openfx::Logger::error("Operation failed");

// Log with formatting (similar to std::format)
openfx::Logger::debug("Loaded config from {}", config_path);
openfx::Logger::info("Processing resource: {}", resource_id);
openfx::Logger::warn("Memory usage: {}MB", memory_usage);
openfx::Logger::error("Failed to process item {} in category {}", item_id, category);
```

## Log Levels

The logging system supports four log levels, from lowest to highest severity:

- **Debug**: Detailed diagnostic messages, useful during development
- **Info**: General information messages
- **Warning**: Warning messages that don't prevent operation
- **Error**: Error messages indicating failures

A fifth level, **Off**, is above them all: no message is logged at it, so setting it silences
the log.

By default, `Logger::debug` messages are filtered out. Use `setLevel`/`getLevel` to control the
minimum level that gets logged:

```cpp
// Only log warnings and errors
openfx::Logger::setLevel(openfx::Logger::Level::Warning);

// Enable debug messages
openfx::Logger::setLevel(openfx::Logger::Level::Debug);

// Log nothing at all
openfx::Logger::setLevel(openfx::Logger::Level::Off);

openfx::Logger::Level current = openfx::Logger::getLevel();
```

Messages below the current level are dropped before they are formatted or passed to the log
handler, so filtered-out `debug` calls are cheap.

## Logging to a File

Most hosts send a plugin's standard output nowhere, so `logToFile` sends the log to the end of
a file instead. Each message is a line with its local time to the millisecond, its thread and
its level, and is flushed as it is written, so a host that crashes leaves the last one in the
file:

```cpp
openfx::Logger::logToFile("/tmp/myplugin.log");  // false if it can't be opened
```

```
2026-10-25 06:00:13.482 0x1d2c3 debug OfxImageEffectActionRender ...
2026-10-25 06:00:13.519 0x1d2c3 debug OfxImageEffectActionRender -> kOfxStatOK
```

To leave that to whoever is debugging, `logToFileFromEnvironment` does it only when an
environment variable of the plugin's choosing gives a file, and then sets the level to Debug.
Call it once, early, such as from `OfxGetPlugin`:

```cpp
openfx::Logger::logToFileFromEnvironment("MYPLUGIN_LOG");
```

## Tracing a Plugin's Actions

At debug level, `ImageEffectPlugin` and `InteractPlugin` log each action they are sent before
they handle it, and the status they return after, a thrown exception's included (an overlay's
are marked `interact`). In a host that crashes or misbehaves, the log then says which action was
in progress, and what each before it returned.

## Thread IDs

With several renders at once, or an action the host sends while the plugin is still in another,
which thread logged a line matters. The default and file handlers put the thread's ID on every
line, as the system's debugger shows it: lldb's thread ID on macOS (in hex, as lldb writes it),
the kernel thread ID (gdb's LWP) on Linux, and `GetCurrentThreadId()` on Windows. A line in the
log can then be matched with a thread in the debugger. `Logger::threadId()` gives the calling
thread's ID; a handler of your own runs on the thread that logged, so it can call it too.

## Custom Log Handlers

You can customize where and how log messages are processed by providing your own log handler:

```cpp
// A log handler of your own, here writing to a file in a format of its own
std::shared_ptr<std::ofstream> logfile = 
    std::make_shared<std::ofstream>("application.log", std::ios::app);
    
openfx::Logger::setLogHandler(
    [logfile](openfx::Logger::Level level, 
             std::chrono::system_clock::time_point timestamp,
             const std::string& message) {
        // Convert timestamp to local time
        std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
        std::tm local_tm = *std::localtime(&time);
        
        // Level to string
        const char* levelStr = "";
        switch (level) {
            case openfx::Logger::Level::Debug:   levelStr = "DEBUG"; break;
            case openfx::Logger::Level::Info:    levelStr = "INFO"; break;
            case openfx::Logger::Level::Warning: levelStr = "WARN"; break;
            case openfx::Logger::Level::Error:   levelStr = "ERROR"; break;
            case openfx::Logger::Level::Off:     break;  // never passed to a handler
        }
        
        // Write to file
        *logfile << "["
                << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
                << "]["
                << levelStr
                << "] "
                << message
                << std::endl;
    }
);
```

## String Formatting

The logging system includes a simple string formatting utility that uses `{}` placeholders, similar to `std::format`:

```cpp
// Single placeholder
openfx::Logger::info("Processing file: {}", filename);

// Multiple placeholders 
openfx::Logger::info("Transfer completed: {} bytes in {} seconds", bytes, seconds);

// You can also use the formatter directly
std::string msg = openfx::format("User {} logged in from {}", username, ip_address);
```

## Default Log Format

The default log handler formats messages as:

```
[YYYY-MM-DD HH:MM:SS][LEVEL][THREAD] Message
```

For example, on macOS:
```
[2025-02-28 14:30:44][DEBUG][0x1d2c3] Loaded config from /etc/openfx/config.json
[2025-02-28 14:30:45][INFO][0x1d2c3] Application started
[2025-02-28 14:30:46][WARN][0x1d2c9] Memory usage above threshold: 85%
[2025-02-28 14:30:47][ERROR][0x1d2c3] Failed to connect to database
```

## Thread Safety

The logging system is thread-safe and can be used from multiple threads simultaneously. A mutex
guards the handler and the context while a message is prepared, but the handler runs outside it,
so a handler may itself log or read the context. A handler called from several threads at once
serializes its own output if it needs to.

## Performance Considerations

- The logging system is designed to be lightweight, but frequent logging can impact performance
- When using custom log handlers, consider implementing buffering for high-volume logging
- A message is formatted only if its level passes, but its arguments are evaluated either way,
  so consider a level check before computing an expensive one:

```cpp
if (openfx::Logger::getLevel() <= openfx::Logger::Level::Debug) {
    openfx::Logger::debug("Detailed debug info: {}", expensive_to_compute_string());
}
```

## Integration with Error Handling

A log call never throws. Whatever formatting the message or the log handler throws is
swallowed, and the message is lost, so logging is safe anywhere, even in a function called
through a C function pointer, where an exception must not escape.

The logging system is designed to work well with the OFX API Wrapper's exception system:

```cpp
try {
    // Some operation
} catch (const openfx::OfxException& e) {
    openfx::Logger::error("API error occurred: {} (code: {})", e.what(), e.code());
    // Handle the exception
}
```

-------------
Copyright OpenFX and contributors to the OpenFX project.

`SPDX-License-Identifier: BSD-3-Clause`

