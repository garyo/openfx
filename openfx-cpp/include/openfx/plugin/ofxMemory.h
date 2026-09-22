// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side RAII wrapper over OfxMemorySuiteV1. For image-sized buffers a
// host may prefer the image memory in openfx/plugin/ofxEffect.h.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>

#include <cstddef>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxSuites.h"

namespace openfx::plugin {

class Memory {
 public:
  // `owner` associates the block with an effect instance, and may be null.
  Memory(const SuiteContainer& suites, size_t bytes, OfxImageEffectHandle owner = nullptr)
      : mMemorySuite(suites.get<OfxMemorySuiteV1>()), mSize(bytes) {
    if (!mMemorySuite)
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxMemorySuite);
    OfxStatus status = mMemorySuite->memoryAlloc(owner, bytes, &mData);
    if (status != kOfxStatOK)
      throw OfxException(status, "memoryAlloc");
  }

  ~Memory() { release(); }

  Memory(const Memory&) = delete;
  Memory& operator=(const Memory&) = delete;

  Memory(Memory&& other) noexcept
      : mMemorySuite(other.mMemorySuite), mData(other.mData), mSize(other.mSize) {
    other.mData = nullptr;
    other.mSize = 0;
  }

  Memory& operator=(Memory&& other) noexcept {
    if (this != &other) {
      release();
      mMemorySuite = other.mMemorySuite;
      mData = other.mData;
      mSize = other.mSize;
      other.mData = nullptr;
      other.mSize = 0;
    }
    return *this;
  }

  void* data() const { return mData; }
  size_t size() const { return mSize; }

  template <class T>
  T* as() const {
    return static_cast<T*>(mData);
  }

 private:
  void release() noexcept {
    if (mData) {
      mMemorySuite->memoryFree(mData);
      mData = nullptr;
      mSize = 0;
    }
  }

  const OfxMemorySuiteV1* mMemorySuite;
  void* mData{};
  size_t mSize;
};

}  // namespace openfx::plugin
