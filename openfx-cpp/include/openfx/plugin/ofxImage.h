// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <memory>  // For std::unique_ptr

#include "openfx/ofxExceptions.h"
#include "openfx/ofxMisc.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

class Image {
 private:
  // The image property set handle
  const OfxImageEffectSuiteV1* mEffectSuite;
  OfxPropertySetHandle mImg{};
  std::unique_ptr<PropertyAccessor> mImgProps;  // Use a pointer to defer construction

  // Dereference the accessor. unique_ptr's constness is shallow (like a raw
  // pointer), so this can be const and still hand back a non-const reference
  // for the generated propsets accessor classes.
  PropertyAccessor& acc() const { return *mImgProps; }

 public:
  // Constructor acquires the resource
  Image(const OfxImageEffectSuiteV1* effect_suite, const OfxPropertySuiteV1* prop_suite,
        OfxImageClipHandle clip, OfxTime time, const OfxRectD* rect = nullptr)
      : mEffectSuite(effect_suite), mImgProps(nullptr) {
    if (clip == nullptr)
      throw ImageNotFoundException(kOfxStatErrBadHandle, "null clip");
    OfxStatus status = mEffectSuite->clipGetImage(clip, time, rect, &mImg);
    if (status != kOfxStatOK)
      throw ImageNotFoundException(status);
    if (mImg) {
      mImgProps = std::make_unique<PropertyAccessor>(mImg, prop_suite);
    }
  }

  // Default constructor: empty image
  Image() : mEffectSuite(nullptr), mImgProps(nullptr) {}

  // Destructor releases the resource
  ~Image() {
    if (mImg) {
      mEffectSuite->clipReleaseImage(mImg);
      mImg = nullptr;
    }
  }

  // Disable copying
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  // Enable moving
  Image(Image&& other) noexcept
      : mEffectSuite(other.mEffectSuite), mImg(other.mImg), mImgProps(std::move(other.mImgProps)) {
    other.mImg = nullptr;
  }

  Image& operator=(Image&& other) noexcept {
    if (this != &other) {
      // Release any existing resource
      if (mImg) {
        mEffectSuite->clipReleaseImage(mImg);
      }

      // Acquire the other's resource
      mEffectSuite = other.mEffectSuite;
      mImg = other.mImg;
      mImgProps = std::move(other.mImgProps);
      other.mImg = nullptr;
    }
    return *this;
  }

  // Get the image's data pointer
  void* data() const { return propsets::Image(acc()).data(); }

  // Get the image's bounds
  OfxRectI bounds() const { return openfx::toOfxRectI(propsets::Image(acc()).bounds()); }

  // Get the image's rowbytes
  int rowbytes() const { return propsets::Image(acc()).rowBytes(); }

  // Convenience getters for the remaining image properties
  const char* pixelDepth() const { return propsets::Image(acc()).pixelDepth(); }
  const char* components() const { return propsets::Image(acc()).components(); }
  const char* field() const { return propsets::Image(acc()).field(); }
  const char* preMultiplication() const { return propsets::Image(acc()).preMultiplication(); }
  const char* uniqueIdentifier() const { return propsets::Image(acc()).uniqueIdentifier(); }
  double pixelAspectRatio() const { return propsets::Image(acc()).pixelAspectRatio(); }

  OfxRectI regionOfDefinition() const {
    return openfx::toOfxRectI(propsets::Image(acc()).regionOfDefinition());
  }

  OfxPointD renderScale() const {
    return openfx::toOfxPointD(propsets::Image(acc()).renderScale());
  }

  // Typed accessor for this image's properties. No const overload: the
  // generated propsets::Image stores a non-const PropertyAccessor&.
  propsets::Image accessor() { return propsets::Image(acc()); }

  // Get the PropertyAccessor
  PropertyAccessor* props() { return mImgProps.get(); }
  const PropertyAccessor* props() const { return mImgProps.get(); }

  // Get the underlying handle
  OfxPropertySetHandle get() const { return mImg; }

  // Implicit conversion to base handle type
  explicit operator OfxPropertySetHandle() const { return mImg; }

  bool empty() const { return mImg == nullptr; }
  explicit operator bool() const { return mImg != nullptr; }
};

}  // namespace openfx::plugin
