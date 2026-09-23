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
  const OfxImageEffectSuiteV1* effectSuite_{};
  OfxPropertySetHandle image_{};
  std::unique_ptr<PropertyAccessor> imageProps_;  // Use a pointer to defer construction

  const PropertyAccessor& acc() const { return *imageProps_; }

 public:
  // Constructor acquires the resource
  Image(const OfxImageEffectSuiteV1* effectSuite, const OfxPropertySuiteV1* propSuite,
        OfxImageClipHandle clip, OfxTime time, const OfxRectD* rect = nullptr)
      : effectSuite_(effectSuite) {
    if (clip == nullptr)
      throw ImageNotFoundException(kOfxStatErrBadHandle, "null clip");
    OfxStatus status = effectSuite_->clipGetImage(clip, time, rect, &image_);
    if (status != kOfxStatOK)
      throw ImageNotFoundException(status);
    if (image_) {
      imageProps_ = std::make_unique<PropertyAccessor>(image_, propSuite);
    }
  }

  // Default constructor: empty image
  Image() = default;

  // Destructor releases the resource
  ~Image() {
    if (image_) {
      effectSuite_->clipReleaseImage(image_);
      image_ = nullptr;
    }
  }

  // Disable copying
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  // Enable moving. A moved-from Image has released nothing and holds nothing:
  // it is empty and must not be read.
  Image(Image&& other) noexcept
      : effectSuite_(other.effectSuite_), image_(other.image_),
        imageProps_(std::move(other.imageProps_)) {
    other.image_ = nullptr;
  }

  Image& operator=(Image&& other) noexcept {
    if (this != &other) {
      // Release any existing resource
      if (image_) {
        effectSuite_->clipReleaseImage(image_);
      }

      // Acquire the other's resource
      effectSuite_ = other.effectSuite_;
      image_ = other.image_;
      imageProps_ = std::move(other.imageProps_);
      other.image_ = nullptr;
    }
    return *this;
  }

  // Get the image's data pointer
  void* data() const { return propsets::Image(acc()).data(); }

  // Get the image's bounds
  OfxRectI bounds() const { return openfx::toOfxRectI(propsets::Image(acc()).bounds()); }

  // Get the image's row bytes
  int rowBytes() const { return propsets::Image(acc()).rowBytes(); }

  // Convenience getters for the remaining image properties
  const char* pixelDepth() const { return propsets::Image(acc()).pixelDepth(); }
  const char* components() const { return propsets::Image(acc()).components(); }
  const char* field() const { return propsets::Image(acc()).field(); }
  const char* preMultiplication() const {
    return propsets::Image(acc()).preMultiplication();
  }
  const char* uniqueIdentifier() const {
    return propsets::Image(acc()).uniqueIdentifier();
  }
  double pixelAspectRatio() const { return propsets::Image(acc()).pixelAspectRatio(); }

  OfxRectI regionOfDefinition() const {
    return openfx::toOfxRectI(propsets::Image(acc()).regionOfDefinition());
  }

  OfxPointD renderScale() const {
    return openfx::toOfxPointD(propsets::Image(acc()).renderScale());
  }

  // Typed accessor for this image's properties.
  propsets::Image accessor() const { return propsets::Image(acc()); }

  // Get the PropertyAccessor. Must not be called on an empty Image.
  PropertyAccessor& props() { return *imageProps_; }
  const PropertyAccessor& props() const { return *imageProps_; }

  // Get the underlying handle
  OfxPropertySetHandle handle() const { return image_; }

  // Implicit conversion to base handle type
  explicit operator OfxPropertySetHandle() const { return image_; }

  bool empty() const { return image_ == nullptr; }
  explicit operator bool() const { return image_ != nullptr; }
};

}  // namespace openfx::plugin
