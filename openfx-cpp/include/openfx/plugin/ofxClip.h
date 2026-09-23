// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <memory>  // For std::unique_ptr
#include <string>
#include <string_view>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/plugin/ofxImage.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

class Clip {
 private:
  const OfxImageEffectSuiteV1* effectSuite_{};
  const OfxPropertySuiteV1* propertySuite_{};
  OfxImageClipHandle clip_{};
  OfxPropertySetHandle clipPropSet_{};  // The clip property set handle
  std::unique_ptr<PropertyAccessor>
      clipProps_;  // Accessor: use a pointer to defer construction

  const PropertyAccessor& acc() const { return *clipProps_; }

 public:
  // Construct a clip given the raw clip handle.
  // Gets the property set and sets up accessor for it.
  Clip(const OfxImageEffectSuiteV1* effectSuite, const OfxPropertySuiteV1* propSuite,
       OfxImageClipHandle clip)
      : effectSuite_(effectSuite), propertySuite_(propSuite), clip_(clip) {
    if (clip != nullptr) {
      OfxStatus status = effectSuite_->clipGetPropertySet(clip, &clipPropSet_);
      if (status != kOfxStatOK)
        throw ClipNotFoundException(status);
      if (clipPropSet_) {
        clipProps_ = std::make_unique<PropertyAccessor>(clipPropSet_, propSuite);
      }
    }
  }

  // Construct a clip given effect and clip name.
  // Gets the clip with its property set, and sets up accessor for it.
  Clip(const OfxImageEffectSuiteV1* effectSuite, const OfxPropertySuiteV1* propSuite,
       OfxImageEffectHandle effect, std::string_view clipName)
      : effectSuite_(effectSuite), propertySuite_(propSuite) {
    OfxStatus status = effectSuite->clipGetHandle(effect, std::string(clipName).c_str(),
                                                  &clip_, &clipPropSet_);
    if (status != kOfxStatOK || !clip_ || !clipPropSet_)
      throw ClipNotFoundException(status);
    clipProps_ = std::make_unique<PropertyAccessor>(clipPropSet_, propSuite);
  }

  // Construct a clip given effect and clip name, using suite container (simpler)
  Clip(OfxImageEffectHandle effect, std::string_view clipName,
       const SuiteContainer& suites) {
    effectSuite_ = suites.get<OfxImageEffectSuiteV1>();
    propertySuite_ = suites.get<OfxPropertySuiteV1>();
    if (!effectSuite_ || !propertySuite_)
      throw SuiteNotFoundException(
          kOfxStatErrMissingHostFeature,
          effectSuite_ ? kOfxPropertySuite : kOfxImageEffectSuite);
    OfxStatus status = effectSuite_->clipGetHandle(effect, std::string(clipName).c_str(),
                                                   &clip_, &clipPropSet_);
    if (status != kOfxStatOK || !clip_ || !clipPropSet_)
      throw ClipNotFoundException(status);
    clipProps_ = std::make_unique<PropertyAccessor>(clipPropSet_, propertySuite_);
  }

  // Default constructor: empty clip
  Clip() = default;

  // The clip handle and its property-set handle are not owned by Clip, so
  // there is nothing to release here.
  ~Clip() = default;

  // Disable copying
  Clip(const Clip&) = delete;
  Clip& operator=(const Clip&) = delete;

  // Enable moving. The handles are not owned, so a move hands over only the
  // property accessor: a moved-from Clip is empty and must not be read.
  Clip(Clip&&) = default;
  Clip& operator=(Clip&&) = default;

  // Get an image from the clip at this time: an empty Image, to be treated as
  // transparent black, if the clip has none there.
  Image getImage(OfxTime time, const OfxRectD* rect = nullptr) {
    return Image(effectSuite_, propertySuite_, clip_, time, rect);
  }

  // Get an image from the clip at this time, for a specific region
  Image getImage(OfxTime time, const OfxRectD& region) { return getImage(time, &region); }

  // Get the clip handle
  OfxImageClipHandle handle() const { return clip_; }

  // Get the clip's property-set handle
  OfxPropertySetHandle propertySetHandle() const { return clipPropSet_; }

  // Convenience getters for the clip's properties
  const char* name() const { return propsets::ClipInstance(acc()).name(); }
  bool connected() const { return propsets::ClipInstance(acc()).connected(); }
  const char* pixelDepth() const { return propsets::ClipInstance(acc()).pixelDepth(); }
  const char* components() const { return propsets::ClipInstance(acc()).components(); }
  const char* preMultiplication() const {
    return propsets::ClipInstance(acc()).preMultiplication();
  }
  double frameRate() const { return propsets::ClipInstance(acc()).frameRate(); }
  double pixelAspectRatio() const {
    return propsets::ClipInstance(acc()).pixelAspectRatio();
  }
  const char* fieldOrder() const { return propsets::ClipInstance(acc()).fieldOrder(); }
  bool isMask() const { return propsets::ClipInstance(acc()).isMask(); }
  bool optional() const { return propsets::ClipInstance(acc()).optional(); }

  // Typed accessor for this clip's properties.
  propsets::ClipInstance accessor() const { return propsets::ClipInstance(acc()); }

  // Accessor for PropertyAccessor. Must not be called on an empty Clip.
  PropertyAccessor& props() { return *clipProps_; }
  const PropertyAccessor& props() const { return *clipProps_; }

  // Implicit conversions to the handle types
  explicit operator OfxPropertySetHandle() const { return clipPropSet_; }
  explicit operator OfxImageClipHandle() const { return clip_; }

  // A Clip is empty when it has no properties to read through: default
  // constructed, moved from, or built on a clip that has no property set.
  bool empty() const { return clipProps_ == nullptr; }
  explicit operator bool() const { return !empty(); }
};

}  // namespace openfx::plugin
