// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrapper over an OfxImageEffectHandle: its properties, clips,
// parameters and image memory, plus a typed view of an action's argument
// property sets.
//
// Each wrapper takes its suites either from a SuiteContainer or as raw suite
// pointers, and copies the pointers it needs either way, so a container built
// on the spot may go as soon as the wrapper is made.

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/ofxSuites.h"
#include "openfx/plugin/ofxClip.h"
#include "openfx/plugin/ofxParam.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

namespace detail {

inline const OfxImageEffectSuiteV1* requireEffectSuite(
    const OfxImageEffectSuiteV1* suite) {
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxImageEffectSuite);
  return suite;
}

}  // namespace detail

// A block of image memory from the host, freed with imageMemoryFree when the
// ImageMemory goes. The host may move the block while it is unlocked, so
// data() is valid only while it is locked: lock() and unlock() bracket its use
// across actions, and the destructor unlocks it first if it is locked. It can
// adopt a block C code allocated, and give its handle back with release().
class ImageMemory {
 public:
  // Allocate a block with imageMemoryAlloc, and lock it.
  ImageMemory(OfxImageEffectHandle effect, size_t bytes, const SuiteContainer& suites)
      : ImageMemory(effect, bytes, suites.get<OfxImageEffectSuiteV1>()) {}
  ImageMemory(OfxImageEffectHandle effect, size_t bytes,
              const OfxImageEffectSuiteV1* effectSuite)
      : effectSuite_(detail::requireEffectSuite(effectSuite)), size_(bytes) {
    OfxStatus status = effectSuite_->imageMemoryAlloc(effect, bytes, &handle_);
    if (status != kOfxStatOK)
      throw OfxException(status, "imageMemoryAlloc");
    try {
      lock();
    } catch (...) {
      reset();
      throw;
    }
  }

  // Adopt a block C code allocated with imageMemoryAlloc: the ImageMemory
  // frees it from now on, even if this throws. `locked` says C code holds a
  // lock on it, which passes to this ImageMemory; data() then gives the
  // address, found with a lock nested in C code's and undone at once. `bytes`
  // is only what size() reports.
  ImageMemory(OfxImageMemoryHandle memory, const OfxImageEffectSuiteV1* effectSuite,
              bool locked, size_t bytes = 0)
      : effectSuite_(effectSuite), handle_(memory), size_(bytes) {
    if (!locked)
      return;
    try {
      lock();
      OfxStatus status = effectSuite_->imageMemoryUnlock(handle_);
      if (status != kOfxStatOK)
        throw OfxException(status, "imageMemoryUnlock");
    } catch (...) {
      reset();
      throw;
    }
  }

  ~ImageMemory() { reset(); }

  ImageMemory(const ImageMemory&) = delete;
  ImageMemory& operator=(const ImageMemory&) = delete;

  ImageMemory(ImageMemory&& other) noexcept
      : effectSuite_(other.effectSuite_), handle_(std::exchange(other.handle_, nullptr)),
        data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
        locked_(std::exchange(other.locked_, false)) {}

  ImageMemory& operator=(ImageMemory&& other) noexcept {
    if (this != &other) {
      reset();
      effectSuite_ = other.effectSuite_;
      handle_ = std::exchange(other.handle_, nullptr);
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
      locked_ = std::exchange(other.locked_, false);
    }
    return *this;
  }

  // Lock the block, if this has not already, and return its address.
  void* lock() {
    if (!locked_) {
      void* data = nullptr;
      OfxStatus status = effectSuite_->imageMemoryLock(handle_, &data);
      if (status != kOfxStatOK)
        throw OfxException(status, "imageMemoryLock");
      data_ = data;
      locked_ = true;
    }
    return data_;
  }

  // Unlock the block, if this has it locked, so the host may move it.
  void unlock() {
    if (!locked_)
      return;
    OfxStatus status = effectSuite_->imageMemoryUnlock(handle_);
    if (status != kOfxStatOK)
      throw OfxException(status, "imageMemoryUnlock");
    data_ = nullptr;
    locked_ = false;
  }

  bool isLocked() const noexcept { return locked_; }

  // The block's address, valid only while it is locked; null while it is not.
  void* data() const { return data_; }
  size_t size() const { return size_; }
  OfxImageMemoryHandle handle() const { return handle_; }

  template <class T>
  T* as() const {
    return static_cast<T*>(data_);
  }

  // Unlock and free the block now, leaving this ImageMemory empty.
  void reset() noexcept {
    if (handle_) {
      if (locked_)
        effectSuite_->imageMemoryUnlock(handle_);
      effectSuite_->imageMemoryFree(handle_);
    }
    handle_ = nullptr;
    data_ = nullptr;
    size_ = 0;
    locked_ = false;
  }

  // Hand the block to C code, which must free it with imageMemoryFree, leaving
  // this ImageMemory empty. A lock this held (see isLocked()) passes to C code
  // with it.
  [[nodiscard]] OfxImageMemoryHandle release() noexcept {
    data_ = nullptr;
    size_ = 0;
    locked_ = false;
    return std::exchange(handle_, nullptr);
  }

 private:
  const OfxImageEffectSuiteV1* effectSuite_;
  OfxImageMemoryHandle handle_{};
  void* data_{};
  size_t size_;
  bool locked_{false};
};

// A typed view of an action's inArgs or outArgs property set:
//   ActionArgs in(inArgs, suites);
//   auto r = in.as<propsets::ImageEffectActionRender_InArgs>();
//   OfxTime t = r.time();
class ActionArgs {
 public:
  ActionArgs(OfxPropertySetHandle args, const SuiteContainer& suites)
      : props_(args, suites) {}
  ActionArgs(OfxPropertySetHandle args, const OfxPropertySuiteV1* propSuite)
      : props_(args, propSuite) {}

  template <class A>
  A as() const {
    return A(props_);
  }

  PropertyAccessor& props() { return props_; }
  OfxPropertySetHandle handle() const { return props_.handle(); }
  bool empty() const { return props_.handle() == nullptr; }

 private:
  PropertyAccessor props_;
};

// An image effect descriptor or instance. Which of descriptor() and instance()
// applies is decided by the action the plugin is in.
class ImageEffect {
 public:
  ImageEffect(OfxImageEffectHandle effect, const SuiteContainer& suites)
      : ImageEffect(effect, suites.get<OfxImageEffectSuiteV1>(),
                    suites.get<OfxPropertySuiteV1>(), suites.get<OfxParameterSuiteV1>()) {
  }

  // The parameter suite is needed only by params().
  ImageEffect(OfxImageEffectHandle effect, const OfxImageEffectSuiteV1* effectSuite,
              const OfxPropertySuiteV1* propSuite,
              const OfxParameterSuiteV1* paramSuite = nullptr)
      : effectSuite_(detail::requireEffectSuite(effectSuite)), propSuite_(propSuite),
        paramSuite_(paramSuite), effect_(effect),
        props_(effect, effectSuite_, propSuite) {}

  OfxImageEffectHandle handle() const { return effect_; }
  PropertyAccessor& props() { return props_; }

  // The suites this wrapper calls, for calling them directly.
  const OfxImageEffectSuiteV1* effectSuite() const { return effectSuite_; }
  const OfxPropertySuiteV1* propertySuite() const { return propSuite_; }
  const OfxParameterSuiteV1* paramSuite() const { return paramSuite_; }

  propsets::EffectDescriptor descriptor() const {
    return propsets::EffectDescriptor(props_);
  }
  propsets::EffectInstance instance() const { return propsets::EffectInstance(props_); }

  // Fetch an existing clip by name, throwing ClipNotFoundException if the
  // lookup fails, as Clip's by-name constructor does.
  Clip clip(std::string_view name) const {
    return Clip(effectSuite_, propSuite_, effect_, name);
  }

  // The effect's parameter set.
  ParamSet params() const {
    return ParamSet(effect_, effectSuite_, paramSuite_, propSuite_);
  }

  // Define a clip in a describe-in-context action. A host that refuses
  // throws OfxException with its status, or kOfxStatErrBadHandle if it
  // answered kOfxStatOK without the clip's property set.
  propsets::ClipDescriptor defineClip(std::string_view name) {
    const std::string clipName(name);
    OfxPropertySetHandle propSet = nullptr;
    OfxStatus status = effectSuite_->clipDefine(effect_, clipName.c_str(), &propSet);
    if (status == kOfxStatOK && !propSet)
      status = kOfxStatErrBadHandle;
    if (status != kOfxStatOK)
      throw OfxException(status, "clipDefine " + clipName);
    return propsets::ClipDescriptor(propSet, propSuite_);
  }

  // True if the host wants this render abandoned.
  bool abort() const { return effectSuite_->abort(effect_) != 0; }

  // Allocate image memory owned by this effect.
  ImageMemory imageMemory(size_t bytes) const {
    return ImageMemory(effect_, bytes, effectSuite_);
  }

 private:
  const OfxImageEffectSuiteV1* effectSuite_;
  const OfxPropertySuiteV1* propSuite_;
  const OfxParameterSuiteV1* paramSuite_;
  OfxImageEffectHandle effect_;
  PropertyAccessor props_;
};

}  // namespace openfx::plugin
