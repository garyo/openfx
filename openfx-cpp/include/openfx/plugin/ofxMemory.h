// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side RAII wrapper over OfxMemorySuiteV1. For image-sized buffers a
// host may prefer the image memory in openfx/plugin/ofxEffect.h.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>

#include <cstddef>
#include <utility>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxSuites.h"

namespace openfx::plugin {

// A block from the host's memory suite, freed with memoryFree when the Memory
// goes. It can adopt a block C code allocated, and give it back with release().
class Memory {
 public:
  // Allocate a block with memoryAlloc. `owner` associates the block with an
  // effect instance, and may be null.
  Memory(const SuiteContainer& suites, size_t bytes, OfxImageEffectHandle owner = nullptr)
      : memorySuite_(suites.get<OfxMemorySuiteV1>()), size_(bytes) {
    if (!memorySuite_)
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxMemorySuite);
    OfxStatus status = memorySuite_->memoryAlloc(owner, bytes, &data_);
    if (status != kOfxStatOK)
      throw OfxException(status, "memoryAlloc");
  }

  // Adopt a block C code allocated with memoryAlloc: the Memory frees it from
  // now on. `bytes` is only what size() reports.
  Memory(void* data, size_t bytes, const OfxMemorySuiteV1* memorySuite) noexcept
      : memorySuite_(memorySuite), data_(data), size_(bytes) {}

  ~Memory() { reset(); }

  Memory(const Memory&) = delete;
  Memory& operator=(const Memory&) = delete;

  Memory(Memory&& other) noexcept
      : memorySuite_(other.memorySuite_), data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)) {}

  Memory& operator=(Memory&& other) noexcept {
    if (this != &other) {
      reset();
      memorySuite_ = other.memorySuite_;
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  void* data() const { return data_; }
  size_t size() const { return size_; }

  template <class T>
  T* as() const {
    return static_cast<T*>(data_);
  }

  // Free the block now, leaving this Memory empty.
  void reset() noexcept {
    if (data_)
      memorySuite_->memoryFree(data_);
    data_ = nullptr;
    size_ = 0;
  }

  // Hand the block to C code, which must free it with memoryFree, leaving this
  // Memory empty.
  [[nodiscard]] void* release() noexcept {
    size_ = 0;
    return std::exchange(data_, nullptr);
  }

 private:
  const OfxMemorySuiteV1* memorySuite_;
  void* data_{};
  size_t size_;
};

}  // namespace openfx::plugin
