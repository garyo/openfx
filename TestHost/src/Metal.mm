// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Metal.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace testhost::metal {

namespace {

id<MTLCommandQueue> queue() {
  static id<MTLCommandQueue> q = [] {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
      throw std::runtime_error("no Metal device");
    return [device newCommandQueue];
  }();
  return q;
}

}  // namespace

bool available() {
  static const bool any = MTLCreateSystemDefaultDevice() != nil;
  return any;
}

void* commandQueue() { return (__bridge void*)queue(); }

void* upload(const std::byte* bytes, size_t length) {
  // A Metal buffer can't be empty.
  id<MTLBuffer> buffer = [queue().device newBufferWithLength:std::max<size_t>(length, 16)
                                                      options:MTLResourceStorageModeShared];
  if (!buffer)
    throw std::runtime_error("could not allocate a Metal buffer");
  std::memcpy(buffer.contents, bytes, length);
  return (__bridge_retained void*)buffer;
}

void download(void* handle, size_t offset, std::byte* bytes, size_t length) {
  id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)handle;
  std::memcpy(bytes, static_cast<const std::byte*>(buffer.contents) + offset, length);
}

void release(void* handle) { (void)(__bridge_transfer id<MTLBuffer>)handle; }

void finish() {
  // The queue runs its command buffers in order, so an empty one completes
  // after everything before it.
  id<MTLCommandBuffer> last = [queue() commandBuffer];
  [last commit];
  [last waitUntilCompleted];
}

}  // namespace testhost::metal
