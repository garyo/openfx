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
  const OfxImageEffectSuiteV1* mEffectSuite;
  const OfxPropertySuiteV1* mPropertySuite;
  OfxImageClipHandle mClip{};
  OfxPropertySetHandle mClipPropSet{};  // The clip property set handle
  std::unique_ptr<PropertyAccessor>
      mClipProps;  // Accessor: use a pointer to defer construction

  // Dereference the accessor; see the equivalent helper in Image for why this
  // can be const.
  PropertyAccessor& acc() const { return *mClipProps; }

 public:
  // Construct a clip given the raw clip handle.
  // Gets the property set and sets up accessor for it.
  Clip(const OfxImageEffectSuiteV1* effect_suite, const OfxPropertySuiteV1* prop_suite,
       OfxImageClipHandle clip)
      : mEffectSuite(effect_suite), mPropertySuite(prop_suite), mClip(clip),
        mClipProps(nullptr) {
    if (clip != nullptr) {
      OfxStatus status = mEffectSuite->clipGetPropertySet(clip, &mClipPropSet);
      if (status != kOfxStatOK)
        throw ClipNotFoundException(status);
      if (mClipPropSet) {
        mClipProps = std::make_unique<PropertyAccessor>(mClipPropSet, prop_suite);
      }
    }
  }

  // Construct a clip given effect and clip name.
  // Gets the clip with its property set, and sets up accessor for it.
  Clip(const OfxImageEffectSuiteV1* effect_suite, const OfxPropertySuiteV1* prop_suite,
       OfxImageEffectHandle effect, std::string_view clip_name)
      : mEffectSuite(effect_suite), mPropertySuite(prop_suite), mClipProps(nullptr) {
    OfxStatus status = effect_suite->clipGetHandle(effect, std::string(clip_name).c_str(),
                                                   &mClip, &mClipPropSet);
    if (status != kOfxStatOK || !mClip || !mClipPropSet)
      throw ClipNotFoundException(status);
    mClipProps = std::make_unique<PropertyAccessor>(mClipPropSet, prop_suite);
  }

  // Construct a clip given effect and clip name, using suite container (simpler)
  Clip(OfxImageEffectHandle effect, std::string_view clip_name,
       const SuiteContainer& suites)
      : mClipProps(nullptr) {
    mEffectSuite = suites.get<OfxImageEffectSuiteV1>();
    mPropertySuite = suites.get<OfxPropertySuiteV1>();
    if (!mEffectSuite || !mPropertySuite)
      throw SuiteNotFoundException(
          kOfxStatErrMissingHostFeature,
          mEffectSuite ? kOfxPropertySuite : kOfxImageEffectSuite);
    OfxStatus status = mEffectSuite->clipGetHandle(effect, std::string(clip_name).c_str(),
                                                   &mClip, &mClipPropSet);
    if (status != kOfxStatOK || !mClip || !mClipPropSet)
      throw ClipNotFoundException(status);
    mClipProps = std::make_unique<PropertyAccessor>(mClipPropSet, mPropertySuite);
  }

  // Default constructor: empty clip
  Clip() : mEffectSuite(nullptr), mClipProps(nullptr) {}

  // The clip handle and its property-set handle are not owned by Clip, so
  // there is nothing to release here.
  ~Clip() = default;

  // Disable copying
  Clip(const Clip&) = delete;
  Clip& operator=(const Clip&) = delete;

  // Enable moving. Since the handles aren't owned, a moved-from Clip simply
  // keeps its raw handles; only mClipProps is left null.
  Clip(Clip&&) = default;
  Clip& operator=(Clip&&) = default;

  // Get an image from the clip at this time
  Image get_image(OfxTime time, const OfxRectD* rect = nullptr) {
    return Image(mEffectSuite, mPropertySuite, mClip, time, rect);
  }

  // Get an image from the clip at this time, for a specific region
  Image get_image(OfxTime time, const OfxRectD& region) {
    return get_image(time, &region);
  }

  // get the clip handle
  OfxImageClipHandle clip() const { return mClip; }

  // get the clip's prop set
  OfxPropertySetHandle get_propset() const { return mClipPropSet; }

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
  PropertyAccessor* props() { return mClipProps.get(); }
  const PropertyAccessor* props() const { return mClipProps.get(); }

  // Implicit conversions to the handle types
  explicit operator OfxPropertySetHandle() const { return mClipPropSet; }
  explicit operator OfxImageClipHandle() const { return mClip; }

  bool empty() const { return mClip == nullptr; }
  explicit operator bool() const { return mClip != nullptr; }
};

}  // namespace openfx::plugin
