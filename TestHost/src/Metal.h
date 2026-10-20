// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// Metal rendering (OFX 1.5): the host's device and command queue, and the
// Metal buffers it hands a plugin in place of CPU pixel pointers. macOS only;
// elsewhere available() is false and nothing else may be called.

#include <cstddef>

namespace testhost::metal {

// Whether this build and machine can render with Metal.
bool available();

// The host's command queue (an id<MTLCommandQueue>), made on first use.
void* commandQueue();

// A new shared Metal buffer holding a copy of `length` bytes; the handle is
// an id<MTLBuffer> the caller owns until release().
void* upload(const std::byte* bytes, size_t length);
// Copies `length` bytes starting `offset` bytes into the buffer.
void download(void* buffer, size_t offset, std::byte* bytes, size_t length);
void release(void* buffer);

// Waits until everything enqueued on the command queue so far has finished,
// as a host does before it reads a plugin's output; the plugin may return
// from its render action before its work completes.
void finish();

}  // namespace testhost::metal
