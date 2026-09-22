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
      : mEffectSuite(detail::requireEffectSuite(suites)), mSize(bytes) {
    OfxStatus status = mEffectSuite->imageMemoryAlloc(effect, bytes, &mHandle);
    if (status != kOfxStatOK)
      throw OfxException(status, "imageMemoryAlloc");
    status = mEffectSuite->imageMemoryLock(mHandle, &mData);
    if (status != kOfxStatOK) {
      mEffectSuite->imageMemoryFree(mHandle);
      throw OfxException(status, "imageMemoryLock");
    }
  }

  ~ImageMemory() { release(); }

  ImageMemory(const ImageMemory&) = delete;
  ImageMemory& operator=(const ImageMemory&) = delete;

  ImageMemory(ImageMemory&& other) noexcept
      : mEffectSuite(other.mEffectSuite),
        mHandle(other.mHandle),
        mData(other.mData),
        mSize(other.mSize) {
    other.mHandle = nullptr;
    other.mData = nullptr;
    other.mSize = 0;
  }

  ImageMemory& operator=(ImageMemory&& other) noexcept {
    if (this != &other) {
      release();
      mEffectSuite = other.mEffectSuite;
      mHandle = other.mHandle;
      mData = other.mData;
      mSize = other.mSize;
      other.mHandle = nullptr;
      other.mData = nullptr;
      other.mSize = 0;
    }
    return *this;
  }

  void* data() const { return mData; }
  size_t size() const { return mSize; }
  OfxImageMemoryHandle handle() const { return mHandle; }

  template <class T>
  T* as() const {
    return static_cast<T*>(mData);
  }

 private:
  void release() noexcept {
    if (mHandle) {
      mEffectSuite->imageMemoryUnlock(mHandle);
      mEffectSuite->imageMemoryFree(mHandle);
      mHandle = nullptr;
      mData = nullptr;
    }
  }

  const OfxImageEffectSuiteV1* mEffectSuite;
  OfxImageMemoryHandle mHandle{};
  void* mData{};
  size_t mSize;
};

// A typed view of an action's inArgs or outArgs property set:
//   ActionArgs in(inArgs, suites);
//   auto r = in.as<propsets::ImageEffectActionRender_InArgs>();
//   OfxTime t = r.time();
// The views hold a reference into this object, so keep it alive while using
// them.
class ActionArgs {
 public:
  ActionArgs(OfxPropertySetHandle args, const SuiteContainer& suites) : mProps(args, suites) {}

  template <class A>
  A as() {
    return A(mProps);
  }

  PropertyAccessor& props() { return mProps; }
  OfxPropertySetHandle handle() const { return mProps.handle(); }
  bool empty() const { return mProps.handle() == nullptr; }

 private:
  PropertyAccessor mProps;
};

// An image effect descriptor or instance. Which of descriptor() and instance()
// applies is decided by the action the plugin is in.
//
// defineClip returns an accessor over a PropertyAccessor this object owns, so
// keep the ImageEffect alive while the accessor is in use.
class ImageEffect {
 public:
  ImageEffect(OfxImageEffectHandle effect, const SuiteContainer& suites)
      : mSuites(&suites),
        mEffectSuite(detail::requireEffectSuite(suites)),
        mEffect(effect),
        mProps(effect, suites) {}

  OfxImageEffectHandle handle() const { return mEffect; }
  const SuiteContainer& suites() const { return *mSuites; }
  PropertyAccessor& props() { return mProps; }

  propsets::EffectDescriptor descriptor() { return propsets::EffectDescriptor(mProps); }
  propsets::EffectInstance instance() { return propsets::EffectInstance(mProps); }

  // Fetch an existing clip by name.
  Clip clip(std::string_view name) const { return Clip(mEffect, name, *mSuites); }

  // The effect's parameter set.
  ParamSet params() const { return ParamSet(mEffect, *mSuites); }

  // Define a clip in a describe-in-context action.
  propsets::ClipDescriptor defineClip(std::string_view name) {
    OfxPropertySetHandle propSet = nullptr;
    OfxStatus status =
        mEffectSuite->clipDefine(mEffect, std::string(name).c_str(), &propSet);
    if (status != kOfxStatOK)
      throw ClipNotFoundException(status, std::string(name));
    mClipDescriptors.push_back(std::make_unique<PropertyAccessor>(propSet, *mSuites));
    return propsets::ClipDescriptor(*mClipDescriptors.back());
  }

  // True if the host wants this render abandoned.
  bool abort() const { return mEffectSuite->abort(mEffect) != 0; }

  // Allocate image memory owned by this effect.
  ImageMemory imageMemory(size_t bytes) const { return ImageMemory(mEffect, bytes, *mSuites); }

 private:
  const SuiteContainer* mSuites;
  const OfxImageEffectSuiteV1* mEffectSuite;
  OfxImageEffectHandle mEffect;
  PropertyAccessor mProps;
  std::vector<std::unique_ptr<PropertyAccessor>> mClipDescriptors;
};

}  // namespace openfx::plugin
