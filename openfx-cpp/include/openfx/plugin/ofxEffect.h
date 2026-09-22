// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrapper over an OfxImageEffectHandle: its properties, clips,
// parameters and image memory, plus a typed view of an action's argument
// property sets.

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/ofxSuites.h"
#include "openfx/plugin/ofxClip.h"
#include "openfx/plugin/ofxParam.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

namespace detail {

inline const OfxImageEffectSuiteV1* requireEffectSuite(const SuiteContainer& suites) {
  const auto* suite = suites.get<OfxImageEffectSuiteV1>();
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxImageEffectSuite);
  return suite;
}

}  // namespace detail

// A block of image memory from the host, allocated and locked by the
// constructor and unlocked and freed by the destructor.
class ImageMemory {
 public:
  ImageMemory(OfxImageEffectHandle effect, size_t bytes, const SuiteContainer& suites)
      : effectSuite_(detail::requireEffectSuite(suites)), size_(bytes) {
    OfxStatus status = effectSuite_->imageMemoryAlloc(effect, bytes, &handle_);
    if (status != kOfxStatOK)
      throw OfxException(status, "imageMemoryAlloc");
    status = effectSuite_->imageMemoryLock(handle_, &data_);
    if (status != kOfxStatOK) {
      effectSuite_->imageMemoryFree(handle_);
      throw OfxException(status, "imageMemoryLock");
    }
  }

  ~ImageMemory() { release(); }

  ImageMemory(const ImageMemory&) = delete;
  ImageMemory& operator=(const ImageMemory&) = delete;

  ImageMemory(ImageMemory&& other) noexcept
      : effectSuite_(other.effectSuite_), handle_(other.handle_), data_(other.data_),
        size_(other.size_) {
    other.handle_ = nullptr;
    other.data_ = nullptr;
    other.size_ = 0;
  }

  ImageMemory& operator=(ImageMemory&& other) noexcept {
    if (this != &other) {
      release();
      effectSuite_ = other.effectSuite_;
      handle_ = other.handle_;
      data_ = other.data_;
      size_ = other.size_;
      other.handle_ = nullptr;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  void* data() const { return data_; }
  size_t size() const { return size_; }
  OfxImageMemoryHandle handle() const { return handle_; }

  template <class T>
  T* as() const {
    return static_cast<T*>(data_);
  }

 private:
  void release() noexcept {
    if (handle_) {
      effectSuite_->imageMemoryUnlock(handle_);
      effectSuite_->imageMemoryFree(handle_);
      handle_ = nullptr;
      data_ = nullptr;
    }
  }

  const OfxImageEffectSuiteV1* effectSuite_;
  OfxImageMemoryHandle handle_{};
  void* data_{};
  size_t size_;
};

// A typed view of an action's inArgs or outArgs property set:
//   ActionArgs in(inArgs, suites);
//   auto r = in.as<propsets::ImageEffectActionRender_InArgs>();
//   OfxTime t = r.time();
// The views hold a reference into this object, so keep it alive while using
// them.
class ActionArgs {
 public:
  ActionArgs(OfxPropertySetHandle args, const SuiteContainer& suites)
      : props_(args, suites) {}

  template <class A>
  A as() {
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
//
// defineClip returns an accessor over a PropertyAccessor this object owns, so
// keep the ImageEffect alive while the accessor is in use.
class ImageEffect {
 public:
  ImageEffect(OfxImageEffectHandle effect, const SuiteContainer& suites)
      : suites_(&suites), effectSuite_(detail::requireEffectSuite(suites)),
        effect_(effect), props_(effect, suites) {}

  OfxImageEffectHandle handle() const { return effect_; }
  const SuiteContainer& suites() const { return *suites_; }
  PropertyAccessor& props() { return props_; }

  propsets::EffectDescriptor descriptor() { return propsets::EffectDescriptor(props_); }
  propsets::EffectInstance instance() { return propsets::EffectInstance(props_); }

  // Fetch an existing clip by name.
  Clip clip(std::string_view name) const { return Clip(effect_, name, *suites_); }

  // The effect's parameter set.
  ParamSet params() const { return ParamSet(effect_, *suites_); }

  // Define a clip in a describe-in-context action.
  propsets::ClipDescriptor defineClip(std::string_view name) {
    OfxPropertySetHandle propSet = nullptr;
    OfxStatus status =
        effectSuite_->clipDefine(effect_, std::string(name).c_str(), &propSet);
    if (status != kOfxStatOK)
      throw ClipNotFoundException(status, std::string(name));
    clipDescriptors_.push_back(std::make_unique<PropertyAccessor>(propSet, *suites_));
    return propsets::ClipDescriptor(*clipDescriptors_.back());
  }

  // True if the host wants this render abandoned.
  bool abort() const { return effectSuite_->abort(effect_) != 0; }

  // Allocate image memory owned by this effect.
  ImageMemory imageMemory(size_t bytes) const {
    return ImageMemory(effect_, bytes, *suites_);
  }

 private:
  const SuiteContainer* suites_;
  const OfxImageEffectSuiteV1* effectSuite_;
  OfxImageEffectHandle effect_;
  PropertyAccessor props_;
  std::vector<std::unique_ptr<PropertyAccessor>> clipDescriptors_;
};

}  // namespace openfx::plugin
