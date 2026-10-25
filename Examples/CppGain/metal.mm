// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// CppGain's render on Metal: the same gain and offset as on the CPU, for float
// RGBA images, enqueued on the host's command queue.

#include "metal.h"

#import <Metal/Metal.h>

#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cppgain {

namespace {

// Laid out as GainArgs below.
const char* const kKernel = R"(
#include <metal_stdlib>
using namespace metal;

struct GainArgs {
  float4 gain;
  float offset;
  int width, height;
  int srcX1, srcY1, srcX2, srcY2;
  int srcOffset, srcStride, dstOffset, dstStride;
};

kernel void gain(constant GainArgs& a [[buffer(0)]],
                 device const float4* src [[buffer(1)]],
                 device float4* dst [[buffer(2)]],
                 uint2 id [[thread_position_in_grid]]) {
  const int x = int(id.x), y = int(id.y);
  if (x >= a.width || y >= a.height)
    return;
  float4 v = 0.0f;
  if (x >= a.srcX1 && x < a.srcX2 && y >= a.srcY1 && y < a.srcY2)
    v = src[a.srcOffset + y * a.srcStride + x];
  dst[a.dstOffset + y * a.dstStride + x] = v * a.gain + float4(float3(a.offset), 0.0f);
}
)";

// Aligned as the kernel's float4 aligns its GainArgs.
struct alignas(16) GainArgs {
  float gain[4];
  float offset;
  int width, height;
  int srcX1, srcY1, srcX2, srcY2;
  int srcOffset, srcStride, dstOffset, dstStride;
};
static_assert(sizeof(GainArgs) == 64, "the size of the kernel's GainArgs");

// The pipeline for each device, compiled the first time one renders on it.
id<MTLComputePipelineState> pipelineFor(id<MTLDevice> device) {
  static std::mutex mutex;
  static std::map<id<MTLDevice>, id<MTLComputePipelineState>> pipelines;
  const std::lock_guard<std::mutex> lock(mutex);
  __strong id<MTLComputePipelineState>& pipeline = pipelines[device];
  if (!pipeline) {
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:@(kKernel)
                                                  options:[MTLCompileOptions new]
                                                    error:&error];
    if (!library)
      throw std::runtime_error(std::string("Metal kernel: ") +
                               error.localizedDescription.UTF8String);
    pipeline =
        [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"gain"]
                                              error:&error];
    if (!pipeline)
      throw std::runtime_error(std::string("Metal pipeline: ") +
                               error.localizedDescription.UTF8String);
  }
  return pipeline;
}

}  // namespace

void renderOnMetal(void* queue, const MetalImage& src, const MetalImage& dst,
                   const OfxRectI& window, const float gain[4], float offset) {
  constexpr int kPixel = 4 * sizeof(float);
  const int srcStride = src.rowBytes / kPixel, dstStride = dst.rowBytes / kPixel;
  // The source's bounds, and where the window starts in each image's buffer,
  // in pixels from the window's corner.
  const GainArgs args{{gain[0], gain[1], gain[2], gain[3]},
                      offset,
                      window.x2 - window.x1,
                      window.y2 - window.y1,
                      src.bounds.x1 - window.x1,
                      src.bounds.y1 - window.y1,
                      src.bounds.x2 - window.x1,
                      src.bounds.y2 - window.y1,
                      (window.y1 - src.bounds.y1) * srcStride + window.x1 - src.bounds.x1,
                      srcStride,
                      (window.y1 - dst.bounds.y1) * dstStride + window.x1 - dst.bounds.x1,
                      dstStride};
  if (args.width <= 0 || args.height <= 0)
    return;

  id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)queue;
  id<MTLBuffer> out = (__bridge id<MTLBuffer>)dst.buffer;
  id<MTLCommandBuffer> commands = [commandQueue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
  [encoder setComputePipelineState:pipelineFor(commandQueue.device)];
  [encoder setBytes:&args length:sizeof(args) atIndex:0];
  // With no source the kernel reads none, but the slot needs a buffer.
  [encoder setBuffer:src.buffer ? (__bridge id<MTLBuffer>)src.buffer : out
              offset:0
             atIndex:1];
  [encoder setBuffer:out offset:0 atIndex:2];
  [encoder dispatchThreads:MTLSizeMake(NSUInteger(args.width), NSUInteger(args.height), 1)
      threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
  [encoder endEncoding];
  // The host waits for the queue; a render on it doesn't.
  [commands commit];
}

}  // namespace cppgain
