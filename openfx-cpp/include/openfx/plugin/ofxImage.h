// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <memory>  // For std::unique_ptr
#include <utility>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxMisc.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

// An image from a clip, released with clipReleaseImage when the Image goes.
// It can adopt an image C code fetched, and give its handle back to C code
// with release(). A non-owning view of an image handle is simply
// propsets::Image(handle, propertySuite).
class Image {
 private:
  // The image property set handle
  const OfxImageEffectSuiteV1* effectSuite_{};
  OfxPropertySetHandle image_{};
  std::unique_ptr<PropertyAccessor> imageProps_;  // Use a pointer to defer construction

  const PropertyAccessor& acc() const { return *imageProps_; }

  // clipGetImage's image, or null for kOfxStatFailed, which the specification
  // gives as "the image does not exist in the clip at the indicated time
  // and/or region" rather than as a failure.
  static OfxPropertySetHandle fetch(const OfxImageEffectSuiteV1* effectSuite,
                                    OfxImageClipHandle clip, OfxTime time,
                                    const OfxRectD* rect) {
    if (clip == nullptr)
      throw ImageNotFoundException(kOfxStatErrBadHandle, "null clip");
    OfxPropertySetHandle image = nullptr;
    OfxStatus status = effectSuite->clipGetImage(clip, time, rect, &image);
    if (status == kOfxStatFailed)
      return nullptr;
    if (status != kOfxStatOK)
      throw ImageNotFoundException(status, "clipGetImage");
    return image;
  }

 public:
  // Fetch an image from a clip with clipGetImage. The Image is empty if the
  // clip has no image at that time or over that region, in which case the
  // plugin carries on as if it were transparent black; any other failure
  // throws ImageNotFoundException with the host's status, or with
  // kOfxStatErrBadHandle for a null clip.
  Image(const OfxImageEffectSuiteV1* effectSuite, const OfxPropertySuiteV1* propSuite,
        OfxImageClipHandle clip, OfxTime time, const OfxRectD* rect = nullptr)
      : Image(fetch(effectSuite, clip, time, rect), effectSuite, propSuite) {}

  // Adopt an image C code fetched with clipGetImage: the Image releases it from
  // now on, even if this throws for want of a property suite.
  Image(OfxPropertySetHandle image, const OfxImageEffectSuiteV1* effectSuite,
        const OfxPropertySuiteV1* propSuite)
      : effectSuite_(effectSuite), image_(image) {
    try {
      if (image_)
        imageProps_ = std::make_unique<PropertyAccessor>(image_, propSuite);
    } catch (...) {
      reset();
      throw;
    }
  }

  // Default constructor: empty image
  Image() = default;

  ~Image() { reset(); }

  // Disable copying
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  // Enable moving. A moved-from Image has released nothing and holds nothing:
  // it is empty and must not be read.
  Image(Image&& other) noexcept
      : effectSuite_(other.effectSuite_), image_(std::exchange(other.image_, nullptr)),
        imageProps_(std::move(other.imageProps_)) {}

  Image& operator=(Image&& other) noexcept {
    if (this != &other) {
      reset();
      effectSuite_ = other.effectSuite_;
      image_ = std::exchange(other.image_, nullptr);
      imageProps_ = std::move(other.imageProps_);
    }
    return *this;
  }

  // Release the image now, leaving this Image empty.
  void reset() noexcept {
    if (image_)
      effectSuite_->clipReleaseImage(std::exchange(image_, nullptr));
    imageProps_.reset();
  }

  // Hand the image to C code, which must release it with clipReleaseImage,
  // leaving this Image empty.
  [[nodiscard]] OfxPropertySetHandle release() noexcept {
    imageProps_.reset();
    return std::exchange(image_, nullptr);
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

  // Explicit conversion to the handle type
  explicit operator OfxPropertySetHandle() const { return image_; }

  bool empty() const { return image_ == nullptr; }
  explicit operator bool() const { return image_ != nullptr; }
};

}  // namespace openfx::plugin
