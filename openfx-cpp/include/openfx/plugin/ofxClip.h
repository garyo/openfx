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
  const OfxImageEffectSuiteV1* effectSuite_;
  const OfxPropertySuiteV1* propertySuite_;
  OfxImageClipHandle clip_{};
  OfxPropertySetHandle clipPropSet_{};  // The clip property set handle
  std::unique_ptr<PropertyAccessor>
      clipProps_;  // Accessor: use a pointer to defer construction

  // Dereference the accessor; see the equivalent helper in Image for why this
  // can be const.
  PropertyAccessor& acc() const { return *clipProps_; }

 public:
  // Construct a clip given the raw clip handle.
  // Gets the property set and sets up accessor for it.
  Clip(const OfxImageEffectSuiteV1* effect_suite, const OfxPropertySuiteV1* prop_suite,
       OfxImageClipHandle clip)
      : effectSuite_(effect_suite), propertySuite_(prop_suite), clip_(clip),
        clipProps_(nullptr) {
    if (clip != nullptr) {
      OfxStatus status = effectSuite_->clipGetPropertySet(clip, &clipPropSet_);
      if (status != kOfxStatOK)
        throw ClipNotFoundException(status);
      if (clipPropSet_) {
        clipProps_ = std::make_unique<PropertyAccessor>(clipPropSet_, prop_suite);
      }
    }
  }

  // Construct a clip given effect and clip name.
  // Gets the clip with its property set, and sets up accessor for it.
  Clip(const OfxImageEffectSuiteV1* effect_suite, const OfxPropertySuiteV1* prop_suite,
       OfxImageEffectHandle effect, std::string_view clip_name)
      : effectSuite_(effect_suite), propertySuite_(prop_suite), clipProps_(nullptr) {
    OfxStatus status = effect_suite->clipGetHandle(effect, std::string(clip_name).c_str(),
                                                   &clip_, &clipPropSet_);
    if (status != kOfxStatOK || !clip_ || !clipPropSet_)
      throw ClipNotFoundException(status);
    clipProps_ = std::make_unique<PropertyAccessor>(clipPropSet_, prop_suite);
  }

  // Construct a clip given effect and clip name, using suite container (simpler)
  Clip(OfxImageEffectHandle effect, std::string_view clip_name,
       const SuiteContainer& suites)
      : clipProps_(nullptr) {
    effectSuite_ = suites.get<OfxImageEffectSuiteV1>();
    propertySuite_ = suites.get<OfxPropertySuiteV1>();
    if (!effectSuite_ || !propertySuite_)
      throw SuiteNotFoundException(
          kOfxStatErrMissingHostFeature,
          effectSuite_ ? kOfxPropertySuite : kOfxImageEffectSuite);
    OfxStatus status = effectSuite_->clipGetHandle(effect, std::string(clip_name).c_str(),
                                                   &clip_, &clipPropSet_);
    if (status != kOfxStatOK || !clip_ || !clipPropSet_)
      throw ClipNotFoundException(status);
    clipProps_ = std::make_unique<PropertyAccessor>(clipPropSet_, propertySuite_);
  }

  // Default constructor: empty clip
  Clip() : effectSuite_(nullptr), clipProps_(nullptr) {}

  // The clip handle and its property-set handle are not owned by Clip, so
  // there is nothing to release here.
  ~Clip() = default;

  // Disable copying
  Clip(const Clip&) = delete;
  Clip& operator=(const Clip&) = delete;

  // Enable moving. Since the handles aren't owned, a moved-from Clip simply
  // keeps its raw handles; only clipProps_ is left null.
  Clip(Clip&&) = default;
  Clip& operator=(Clip&&) = default;

  // Get an image from the clip at this time
  Image get_image(OfxTime time, const OfxRectD* rect = nullptr) {
    return Image(effectSuite_, propertySuite_, clip_, time, rect);
  }

  // Get an image from the clip at this time, for a specific region
  Image get_image(OfxTime time, const OfxRectD& region) {
    return get_image(time, &region);
  }

  // get the clip handle
  OfxImageClipHandle clip() const { return clip_; }

  // get the clip's prop set
  OfxPropertySetHandle get_propset() const { return clipPropSet_; }

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

  // Typed accessor for this clip's properties. No const overload: the
  // generated propsets::ClipInstance stores a non-const PropertyAccessor&.
  propsets::ClipInstance accessor() { return propsets::ClipInstance(acc()); }

  // Accessor for PropertyAccessor
  PropertyAccessor* props() { return clipProps_.get(); }
  const PropertyAccessor* props() const { return clipProps_.get(); }

  // Implicit conversions to the handle types
  explicit operator OfxPropertySetHandle() const { return clipPropSet_; }
  explicit operator OfxImageClipHandle() const { return clip_; }

  bool empty() const { return clip_ == nullptr; }
  explicit operator bool() const { return clip_ != nullptr; }
};

}  // namespace openfx::plugin
