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
      : memorySuite_(suites.get<OfxMemorySuiteV1>()), size_(bytes) {
    if (!memorySuite_)
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxMemorySuite);
    OfxStatus status = memorySuite_->memoryAlloc(owner, bytes, &data_);
    if (status != kOfxStatOK)
      throw OfxException(status, "memoryAlloc");
  }

  ~Memory() { release(); }

  Memory(const Memory&) = delete;
  Memory& operator=(const Memory&) = delete;

  Memory(Memory&& other) noexcept
      : memorySuite_(other.memorySuite_), data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }

  Memory& operator=(Memory&& other) noexcept {
    if (this != &other) {
      release();
      memorySuite_ = other.memorySuite_;
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  void* data() const { return data_; }
  size_t size() const { return size_; }

  template <class T>
  T* as() const {
    return static_cast<T*>(data_);
  }

 private:
  void release() noexcept {
    if (data_) {
      memorySuite_->memoryFree(data_);
      data_ = nullptr;
      size_ = 0;
    }
  }

  const OfxMemorySuiteV1* memorySuite_;
  void* data_{};
  size_t size_;
};

}  // namespace openfx::plugin
