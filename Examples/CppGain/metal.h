// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// CppGain's Metal render, kept apart so that only metal.mm is Objective-C++.

#include <ofxCore.h>

namespace cppgain {

// A float RGBA image as Metal render gives it: an id<MTLBuffer> (null for a
// source with no image), its bounds, and its rows' stride.
struct MetalImage {
  void* buffer;
  OfxRectI bounds;
  int rowBytes;
};

// Enqueues dst = src * gain + offset over the render window on the host's
// id<MTLCommandQueue>; the offset lifts the colour channels only. Throws
// std::runtime_error if the kernel won't compile.
void renderOnMetal(void* queue, const MetalImage& src, const MetalImage& dst,
                   const OfxRectI& window, const float gain[4], float offset);

}  // namespace cppgain
