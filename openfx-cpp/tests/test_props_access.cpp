// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// openfx::PropertyAccessor, over a host property set standing in for the one a
// host would hand a plugin.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxParam.h>
#include <openfx/host/ofxPropSetAccessors.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>
#include <openfx/ofxMisc.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/ofxSuites.h>
#include <openfx/plugin/ofxPropSetAccessors.h>

#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "harness.h"
#include "log_capture.h"

using openfx::PropertyAccessor;
using openfx::PropId;
using openfx::host::PropertySet;

namespace {

// An accessor over a property set of the named kind, kept alive together.
struct Props {
  explicit Props(const char* setName) : set(setName) {}
  PropertySet set;
  PropertyAccessor accessor{set.handle(), PropertySet::suite()};
};

const char* kMissing = "OrgExampleHostPropNeverWritten";

// An interact suite that hands out a real property set, so an accessor built
// over an interact only fails for want of a suite.
OfxStatus stubInteractGetPropertySet(OfxInteractHandle, OfxPropertySetHandle* out) {
  static PropertySet interact("InteractInstance");
  *out = interact.handle();
  return kOfxStatOK;
}

const OfxInteractSuiteV1* stubInteractSuite() {
  static const OfxInteractSuiteV1 suite = {nullptr, nullptr, stubInteractGetPropertySet};
  return &suite;
}

// A property suite whose every call answers with one status and does nothing
// else: no value, and no log of its own, as a C host's need not have.
OfxStatus stubStatus = kOfxStatOK;

template <typename... Args>
OfxStatus answerStubStatus(Args...) {
  return stubStatus;
}

// An accessor over that suite, answering with this status. Its property set
// handle is never dereferenced.
PropertyAccessor stubAccessor(OfxStatus status) {
  static const OfxPropertySuiteV1 suite = {
      answerStubStatus, answerStubStatus, answerStubStatus, answerStubStatus,
      answerStubStatus, answerStubStatus, answerStubStatus, answerStubStatus,
      answerStubStatus, answerStubStatus, answerStubStatus, answerStubStatus,
      answerStubStatus, answerStubStatus, answerStubStatus, answerStubStatus,
      answerStubStatus, answerStubStatus};
  static int set = 0;
  stubStatus = status;
  return PropertyAccessor(reinterpret_cast<OfxPropertySetHandle>(&set), &suite);
}

// The status of the OfxException f throws, or kOfxStatOK if it throws none.
template <class F>
OfxStatus thrownStatus(F&& f) {
  try {
    std::forward<F>(f)();
  } catch (const openfx::OfxException& e) {
    return e.code();
  }
  return kOfxStatOK;
}

}  // namespace

TEST_CASE(accessor_gets_and_sets_one_value_of_each_type) {
  Props props("ParamsDouble1D");
  PropertyAccessor& a = props.accessor;
  int owned = 0;

  a.set<PropId::OfxPropName>("gain");
  a.set<PropId::OfxParamPropDigits>(4);
  a.set<PropId::OfxParamPropIncrement>(0.25);
  a.set<PropId::OfxParamPropSecret>(true);
  a.set<PropId::OfxParamPropDataPtr>(&owned);

  CHECK(std::string(a.get<PropId::OfxPropName>()) == "gain");
  CHECK(a.get<PropId::OfxParamPropDigits>() == 4);
  CHECK(a.get<PropId::OfxParamPropIncrement>() == 0.25);
  CHECK(a.get<PropId::OfxParamPropSecret>() == 1);
  CHECK(a.get<PropId::OfxParamPropDataPtr>() == &owned);
}

TEST_CASE(accessor_sets_one_index_of_a_multi_dimensional_property) {
  Props props("ParamsDouble1D");
  props.accessor.set<PropId::OfxParamPropInteractMinimumSize>(3.0, 0)
      .set<PropId::OfxParamPropInteractMinimumSize>(4.0, 1);
  CHECK(props.accessor.get<PropId::OfxParamPropInteractMinimumSize>(0) == 3.0);
  CHECK(props.accessor.get<PropId::OfxParamPropInteractMinimumSize>(1) == 4.0);
}

TEST_CASE(accessor_gets_and_sets_a_multi_type_property) {
  Props props("ParamsDouble1D");
  PropertyAccessor& a = props.accessor;
  // The first write fixes the type the host stores it as.
  a.set<PropId::OfxParamPropDefault, double>(0.75);
  CHECK(a.get<PropId::OfxParamPropDefault, double>() == 0.75);
  CHECK(a.get<PropId::OfxParamPropDefault, int>() == 0);  // the same value as an int

  Props ints("ParamsByte");
  ints.accessor.set<PropId::OfxParamPropDefault, int>(3);
  CHECK(ints.accessor.get<PropId::OfxParamPropDefault, int>() == 3);
  CHECK(ints.accessor.get<PropId::OfxParamPropDefault, double>() == 3.0);

  Props strings("ParamsString");
  strings.accessor.set<PropId::OfxParamPropDefault, const char*>("text");
  CHECK(std::string(strings.accessor.get<PropId::OfxParamPropDefault, const char*>()) ==
        "text");
}

TEST_CASE(accessor_gets_all_values_of_a_fixed_dimension_property) {
  Props props("Image");
  props.accessor.set<PropId::OfxImageEffectPropRenderScale>(0.5, 0)
      .set<PropId::OfxImageEffectPropRenderScale>(0.25, 1);
  const std::array<double, 2> scale =
      props.accessor.getAll<PropId::OfxImageEffectPropRenderScale>();
  CHECK(scale.size() == 2);
  CHECK(scale[0] == 0.5);
  CHECK(scale[1] == 0.25);
}

TEST_CASE(accessor_gets_all_values_of_a_variable_dimension_property) {
  Props props("EffectDescriptor");
  props.accessor.setAll<PropId::OfxImageEffectPropSupportedContexts>(
      {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral});
  const std::vector<openfx::CStringView> contexts =
      props.accessor.getAll<PropId::OfxImageEffectPropSupportedContexts>();
  CHECK(contexts.size() == 2);
  CHECK(contexts[0] == kOfxImageEffectContextFilter);
  CHECK(contexts[1] == kOfxImageEffectContextGeneral);
}

TEST_CASE(accessor_gets_all_of_a_missing_variable_dimension_property_softly) {
  Props props("Image");  // declares neither of the properties read below
  const std::vector<openfx::CStringView> colourspaces =
      props.accessor.getAll<PropId::OfxImageClipPropPreferredColourspaces>(false);
  CHECK(colourspaces.empty());
  const std::vector<double> defaults =
      props.accessor.getAllTyped<PropId::OfxParamPropDefault, double>(false);
  CHECK(defaults.empty());
  // Read without the soft flag, the same properties are an error.
  CHECK_THROWS_AS(props.accessor.getAll<PropId::OfxImageClipPropPreferredColourspaces>(),
                  openfx::PropertyNotFoundException);
  CHECK_THROWS_AS((props.accessor.getAllTyped<PropId::OfxParamPropDefault, double>()),
                  openfx::PropertyNotFoundException);
}

TEST_CASE(accessor_sets_all_values_from_a_container) {
  Props props("Image");
  const std::array<int, 4> bounds{1, 2, 3, 4};
  props.accessor.setAll<PropId::OfxImagePropBounds>(bounds);
  CHECK(props.accessor.get<PropId::OfxImagePropBounds>(2) == 3);
  const std::vector<double> scale{0.5, 0.5};
  props.accessor.setAll<PropId::OfxImageEffectPropRenderScale>(scale);
  CHECK(props.accessor.get<PropId::OfxImageEffectPropRenderScale>(1) == 0.5);
}

TEST_CASE(accessor_gets_and_sets_all_values_of_a_multi_type_property) {
  Props props("ParamsDouble2D3D");
  props.accessor.setAllTyped<PropId::OfxParamPropDefault, double>({1.5, 2.5});
  const std::vector<double> defaults =
      props.accessor.getAllTyped<PropId::OfxParamPropDefault, double>();
  CHECK(defaults.size() == 2);
  CHECK(defaults[1] == 2.5);

  const std::vector<double> minima{-1.0, -2.0};
  props.accessor.setAllTyped<PropId::OfxParamPropMin, double>(minima);
  CHECK(props.accessor.get<PropId::OfxParamPropMin, double>(1) == -2.0);
}

TEST_CASE(accessor_sets_and_gets_a_rect_of_ints) {
  Props props("Image");
  props.accessor.set<PropId::OfxImagePropBounds>(OfxRectI{1, 2, 3, 4});
  const OfxRectI bounds = props.accessor.getRectI<PropId::OfxImagePropBounds>();
  CHECK(bounds.x1 == 1);
  CHECK(bounds.y1 == 2);
  CHECK(bounds.x2 == 3);
  CHECK(bounds.y2 == 4);
}

TEST_CASE(accessor_sets_and_gets_a_rect_of_doubles) {
  PropertySet set =
      PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "outArgs");
  PropertyAccessor accessor(set.handle(), PropertySet::suite());
  accessor.set<PropId::OfxImageEffectPropRegionOfDefinition>(
      OfxRectD{-1.0, -2.0, 3.0, 4.0});
  const OfxRectD rod = accessor.getRectD<PropId::OfxImageEffectPropRegionOfDefinition>();
  CHECK(rod.x1 == -1.0);
  CHECK(rod.y2 == 4.0);
}

TEST_CASE(accessor_sets_and_gets_a_point) {
  Props image("Image");
  image.accessor.set<PropId::OfxImageEffectPropRenderScale>(OfxPointD{0.5, 0.25});
  const OfxPointD scale =
      image.accessor.getPointD<PropId::OfxImageEffectPropRenderScale>();
  CHECK(scale.x == 0.5);
  CHECK(scale.y == 0.25);

  Props param("ParamsDouble1D");
  param.accessor.set<PropId::OfxParamPropInteractPreferedSize>(OfxPointI{20, 30});
  const OfxPointI size =
      param.accessor.getPointI<PropId::OfxParamPropInteractPreferedSize>();
  CHECK(size.x == 20);
  CHECK(size.y == 30);
}

TEST_CASE(accessor_sets_an_enum_value_the_metadata_does_not_list) {
  using Components = openfx::EnumValue<PropId::OfxImageEffectPropComponents>;
  // YUVA is in the specification (ofxOld.h) but not in the metadata's list,
  // and a host may use a value of its own; the C API takes either.
  const char* hostComponents = "com.example.host.ComponentsXYZ";
  static_assert(Components::isValid(kOfxImageComponentRGBA));
  static_assert(!Components::isValid(kOfxImageComponentYUVA));
  CHECK(!Components::isValid(hostComponents));

  Props props("Image");
  props.accessor.set<PropId::OfxImageEffectPropComponents>(kOfxImageComponentYUVA);
  CHECK(std::string(props.accessor.get<PropId::OfxImageEffectPropComponents>()) ==
        kOfxImageComponentYUVA);
  openfx::host::propsets::Image(props.accessor).setComponents(hostComponents);
  CHECK(std::string(props.accessor.get<PropId::OfxImageEffectPropComponents>()) ==
        hostComponents);
}

// The typed string getters return a CStringView, so == compares the text; the
// host stores its own copy, at another address than the literal compared with.
// A type the caller asks for by name comes back as that type.
TEST_CASE(accessor_string_getters_compare_by_content) {
  Props props("Image");
  props.accessor.set<PropId::OfxImageEffectPropPixelDepth>(kOfxBitDepthFloat)
      .set<PropId::OfxImageEffectPropComponents>(kOfxImageComponentRGBA);
  const auto depth = props.accessor.get<PropId::OfxImageEffectPropPixelDepth>();
  static_assert(std::is_same_v<decltype(depth), const openfx::CStringView>);
  const char* literal = kOfxBitDepthFloat;
  CHECK(depth.c_str() != literal);
  CHECK(depth == literal);
  CHECK(depth == kOfxBitDepthFloat);
  CHECK(depth != kOfxBitDepthByte);

  const openfx::plugin::propsets::Image image(props.accessor);
  static_assert(std::is_same_v<decltype(image.pixelDepth()), openfx::CStringView>);
  CHECK(image.pixelDepth() == kOfxBitDepthFloat);
  CHECK(image.components() == kOfxImageComponentRGBA);

  // What a getter gives goes back to a setter as it is.
  Props copy("Image");
  copy.accessor.set<PropId::OfxImageEffectPropPixelDepth>(depth).setRaw(
      kOfxImageEffectPropComponents, image.components());
  CHECK(copy.accessor.get<PropId::OfxImageEffectPropPixelDepth>() == kOfxBitDepthFloat);
  CHECK(copy.accessor.get<PropId::OfxImageEffectPropComponents>() ==
        kOfxImageComponentRGBA);

  static_assert(
      std::is_same_v<decltype(props.accessor.getRaw<const char*>(kOfxImagePropField)),
                     const char*>);
  static_assert(std::is_same_v<
                decltype(props.accessor.get<PropId::OfxParamPropDefault, const char*>()),
                const char*>);

  Props param("ParamsString");
  const openfx::host::propsets::ParamsString paramDesc(param.accessor);
  static_assert(
      std::is_same_v<decltype(paramDesc.icon()), std::array<openfx::CStringView, 2>>);
}

TEST_CASE(accessor_reports_a_propertys_dimension) {
  Props props("EffectDescriptor");
  // Known from the metadata, without asking the host.
  CHECK(props.accessor.getDimension<PropId::OfxImageEffectPropSupportsTiles>() == 1);
  // Variable: asked of the host, and so what has been written so far.
  CHECK(props.accessor.getDimension<PropId::OfxImageEffectPropSupportedContexts>() == 0);
  props.accessor.setAll<PropId::OfxImageEffectPropSupportedContexts>(
      {kOfxImageEffectContextFilter});
  CHECK(props.accessor.getDimension<PropId::OfxImageEffectPropSupportedContexts>() == 1);
}

TEST_CASE(accessor_throws_property_not_found_for_a_missing_property) {
  Props props("ClipDescriptor");
  CHECK_THROWS_AS(props.accessor.get<PropId::OfxImageEffectPropFrameRate>(),
                  openfx::PropertyNotFoundException);
  CHECK_THROWS_AS(props.accessor.getRaw<int>(kMissing),
                  openfx::PropertyNotFoundException);
  try {
    props.accessor.getRaw<double>(kMissing);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrUnknown);
    CHECK(std::string(e.what()).find(kMissing) != std::string::npos);
  }
}

// A soft read of a property the set does not have gives the one fallback for
// its type, whichever getter asks, and logs nothing.
TEST_CASE(accessor_soft_read_of_a_missing_property_gives_one_fallback_per_type) {
  Props props("ClipDescriptor");  // declares none of the properties read below
  const PropertyAccessor& a = props.accessor;
  const tests::LogCapture log(openfx::Logger::Level::Debug);

  CHECK(a.get<PropId::OfxParamPropDigits>(0, false) == 0);
  CHECK(a.get<PropId::OfxParamPropDefault, int>(0, false) == 0);
  CHECK(a.getRaw<int>(kMissing, 0, false) == 0);
  CHECK(a.get<PropId::OfxParamPropSecret>(0, false) == false);
  CHECK(a.get<PropId::OfxParamPropDefault, bool>(0, false) == false);
  CHECK(a.getRaw<bool>(kMissing, 0, false) == false);

  CHECK(a.get<PropId::OfxImageEffectPropFrameRate>(0, false) == 0.0);
  CHECK(a.get<PropId::OfxParamPropDefault, double>(0, false) == 0.0);
  CHECK(a.getRaw<double>(kMissing, 0, false) == 0.0);
  const OfxPointD size = a.getPointD<PropId::OfxParamPropInteractMinimumSize>(false);
  CHECK(size.x == 0.0);
  CHECK(size.y == 0.0);

  // Empty, never null, so a string can be used as it comes.
  CHECK(a.get<PropId::OfxParamPropHint>(0, false).empty());
  for (const char* text : {a.get<PropId::OfxParamPropDefault, const char*>(0, false),
                           a.getRaw<const char*>(kMissing, 0, false)}) {
    CHECK(text != nullptr);
    CHECK(text && *text == '\0');
  }

  CHECK(a.get<PropId::OfxParamPropDataPtr>(0, false) == nullptr);
  CHECK(a.getRaw<void*>(kMissing, 0, false) == nullptr);

  CHECK(a.getDimensionRaw(kMissing, false) == 0);
  CHECK(a.getDimension<PropId::OfxParamPropChoiceOption>(false) == 0);
  CHECK(a.getAll<PropId::OfxParamPropChoiceOption>(false).empty());

  CHECK(log.messages.empty());
}

// A soft write of a property the set refuses as unknown does nothing.
TEST_CASE(accessor_soft_write_of_a_missing_property_does_nothing) {
  Props props("ClipDescriptor");
  props.accessor.setRaw<int>(kMissing, 1, 0, false);
  CHECK(!props.accessor.exists(kMissing));
  CHECK_THROWS_AS(props.accessor.setRaw<int>(kMissing, 1),
                  openfx::PropertyNotFoundException);
}

// A soft call is soft about a missing property alone: any other status the
// suite answers throws, from the getters, the setters and the raw calls alike.
TEST_CASE(accessor_soft_call_throws_every_failure_but_a_missing_property) {
  for (const OfxStatus status :
       {kOfxStatFailed, kOfxStatErrBadHandle, kOfxStatErrBadIndex, kOfxStatErrValue,
        kOfxStatErrMemory, kOfxStatErrUnsupported}) {
    PropertyAccessor a = stubAccessor(status);
    CHECK(thrownStatus([&] { a.get<PropId::OfxPropLabel>(0, false); }) == status);
    CHECK(thrownStatus([&] { a.get<PropId::OfxParamPropDefault, double>(0, false); }) ==
          status);
    CHECK(thrownStatus([&] { a.getRaw<void*>(kMissing, 0, false); }) == status);
    CHECK(thrownStatus([&] { a.getDimensionRaw(kMissing, false); }) == status);
    CHECK(thrownStatus([&] { a.getAll<PropId::OfxParamPropChoiceOption>(false); }) ==
          status);
    CHECK(thrownStatus([&] { a.set<PropId::OfxPropLabel>("x", 0, false); }) == status);
    CHECK(thrownStatus([&] { a.setRaw<double>(kMissing, 1.0, 0, false); }) == status);
  }
  PropertyAccessor missing = stubAccessor(kOfxStatErrUnknown);
  CHECK(thrownStatus([&] { missing.get<PropId::OfxPropLabel>(0, false); }) == kOfxStatOK);
  CHECK(thrownStatus([&] { missing.set<PropId::OfxPropLabel>("x", 0, false); }) ==
        kOfxStatOK);

  // The same from a real property set: an index past the end, the wrong type.
  Props props("ClipDescriptor");
  CHECK(thrownStatus([&] { props.accessor.get<PropId::OfxPropName>(4, false); }) ==
        kOfxStatErrBadIndex);
  CHECK(thrownStatus([&] {
          props.accessor.getRaw<const char*>(kOfxImageClipPropOptional, 0, false);
        }) == kOfxStatErrValue);
  CHECK(thrownStatus([&] {
          props.accessor.setRaw<const char*>(kOfxImageClipPropOptional, "yes", 0, false);
        }) == kOfxStatErrValue);
}

// soft() gives a copy of the accessor that forgives a property the set does
// not have, in every call; the accessor it was made from stays strict.
TEST_CASE(accessor_soft_copy_forgives_a_missing_property) {
  Props props("ClipDescriptor");  // declares none of the properties used below
  const PropertyAccessor& strict = props.accessor;
  const PropertyAccessor soft = strict.soft();
  const tests::LogCapture log(openfx::Logger::Level::Debug);

  CHECK(soft.get<PropId::OfxParamPropDigits>() == 0);
  CHECK(soft.get<PropId::OfxImageEffectPropFrameRate>() == 0.0);
  CHECK(soft.get<PropId::OfxParamPropHint>().empty());
  CHECK(soft.get<PropId::OfxParamPropDataPtr>() == nullptr);
  CHECK(soft.get<PropId::OfxParamPropDefault, double>() == 0.0);
  CHECK(soft.getRaw<const char*>(kMissing) != nullptr);
  CHECK(soft.getPointD<PropId::OfxParamPropInteractMinimumSize>().x == 0.0);
  CHECK(soft.getDimension<PropId::OfxParamPropChoiceOption>() == 0);
  CHECK(soft.getAll<PropId::OfxParamPropChoiceOption>().empty());
  CHECK(log.messages.empty());

  PropertyAccessor writer = strict.soft();
  writer.setRaw(kMissing, 1).reset(kMissing);
  CHECK(!strict.exists(kMissing));

  CHECK_THROWS_AS(strict.get<PropId::OfxParamPropDigits>(),
                  openfx::PropertyNotFoundException);
  CHECK_THROWS_AS(props.accessor.setRaw(kMissing, 1), openfx::PropertyNotFoundException);
}

// A soft copy chains as the original does, each call on it soft.
TEST_CASE(accessor_soft_copy_chains) {
  Props props("ClipDescriptor");
  props.accessor.soft()
      .setRaw(kMissing, 1)
      .set<PropId::OfxPropLabel>("Source")
      .setRaw(kMissing, 2);
  CHECK(props.accessor.get<PropId::OfxPropLabel>() == "Source");
}

// Soft about a missing property alone: every other status throws from a soft
// copy as from the accessor it was made from.
TEST_CASE(accessor_soft_copy_throws_every_failure_but_a_missing_property) {
  for (const OfxStatus status :
       {kOfxStatFailed, kOfxStatErrBadHandle, kOfxStatErrBadIndex, kOfxStatErrValue,
        kOfxStatErrMemory, kOfxStatErrUnsupported}) {
    const PropertyAccessor soft = stubAccessor(status).soft();
    PropertyAccessor writer = soft;
    CHECK(thrownStatus([&] { soft.get<PropId::OfxPropLabel>(); }) == status);
    CHECK(thrownStatus([&] { soft.get<PropId::OfxParamPropDefault, double>(); }) ==
          status);
    CHECK(thrownStatus([&] { soft.getRaw<void*>(kMissing); }) == status);
    CHECK(thrownStatus([&] { soft.getDimensionRaw(kMissing); }) == status);
    CHECK(thrownStatus([&] { soft.getAll<PropId::OfxParamPropChoiceOption>(); }) ==
          status);
    CHECK(thrownStatus([&] { writer.set<PropId::OfxPropLabel>("x"); }) == status);
    CHECK(thrownStatus([&] { writer.setRaw<double>(kMissing, 1.0); }) == status);
    CHECK(thrownStatus([&] { writer.reset(kMissing); }) == status);
  }
  PropertyAccessor missing = stubAccessor(kOfxStatErrUnknown).soft();
  CHECK(thrownStatus([&] { missing.get<PropId::OfxPropLabel>(); }) == kOfxStatOK);
  CHECK(thrownStatus([&] { missing.set<PropId::OfxPropLabel>("x"); }) == kOfxStatOK);

  Props props("ClipDescriptor");
  const PropertyAccessor soft = props.accessor.soft();
  CHECK(thrownStatus([&] { soft.get<PropId::OfxPropName>(4); }) == kOfxStatErrBadIndex);
  CHECK(thrownStatus([&] { soft.getRaw<const char*>(kOfxImageClipPropOptional); }) ==
        kOfxStatErrValue);
}

// find() tells a property the set does not have, std::nullopt, from one that
// holds the fallback's value; any other failure throws.
TEST_CASE(accessor_find_tells_a_missing_property_from_the_fallback) {
  Props props("ParamsDouble1D");
  PropertyAccessor& a = props.accessor;
  a.set<PropId::OfxParamPropDigits>(0).set<PropId::OfxParamPropHint>("");
  const std::optional<int> digits = a.find<PropId::OfxParamPropDigits>();
  CHECK(digits.has_value() && *digits == 0);
  const std::optional<openfx::CStringView> hint = a.find<PropId::OfxParamPropHint>();
  CHECK(hint.has_value() && hint->empty());
  CHECK(!a.find<PropId::OfxImageEffectPropFrameRate>().has_value());
  CHECK(!a.find<PropId::OfxImagePropBounds>(2).has_value());

  a.set<PropId::OfxParamPropDefault, double>(0.5);
  CHECK((a.find<PropId::OfxParamPropDefault, double>() == 0.5));
  Props image("Image");
  CHECK(!(image.accessor.find<PropId::OfxParamPropDefault, double>().has_value()));

  CHECK(a.findRaw<int>(kOfxParamPropDigits) == 0);
  CHECK(!a.findRaw<double>(kMissing).has_value());
  static_assert(std::is_same_v<decltype(a.findRaw<const char*>(kMissing)),
                               std::optional<const char*>>);

  CHECK(thrownStatus([&] { a.find<PropId::OfxPropName>(4); }) == kOfxStatErrBadIndex);
  CHECK(thrownStatus([&] {
          stubAccessor(kOfxStatErrBadHandle).findRaw<int>(kMissing);
        }) == kOfxStatErrBadHandle);
  CHECK(thrownStatus([&] { a.soft().find<PropId::OfxPropName>(4); }) ==
        kOfxStatErrBadIndex);
}

TEST_CASE(accessor_throws_the_status_the_suite_returned) {
  Props props("ClipDescriptor");
  // The wrong type for a property that does exist: not "not found".
  try {
    props.accessor.getRaw<const char*>(kOfxImageClipPropOptional);
    CHECK(false);
  } catch (const openfx::PropertyNotFoundException&) {
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrValue);
  }
  // An index past the end of a property that does exist.
  try {
    props.accessor.get<PropId::OfxPropName>(4);
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrBadIndex);
  }
}

// A failed call is reported once, by what it throws, which carries the status
// and the property; whoever catches it decides whether to log it.
TEST_CASE(accessor_throws_without_logging_first) {
  const tests::LogCapture log(openfx::Logger::Level::Debug);
  try {
    stubAccessor(kOfxStatErrBadIndex).get<PropId::OfxPropLabel>();
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrBadIndex);
    const std::string what = e.what();
    CHECK(what.find(kOfxPropLabel) != std::string::npos);
    CHECK(what.find("kOfxStatErrBadIndex") != std::string::npos);
  }
  try {
    stubAccessor(kOfxStatErrValue).set<PropId::OfxPropLabel>("gain");
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrValue);
    CHECK(std::string(e.what()).find("OfxPropLabel=gain") != std::string::npos);
  }
  CHECK_THROWS_AS(stubAccessor(kOfxStatErrUnknown).getRaw<double>(kMissing),
                  openfx::PropertyNotFoundException);
  CHECK_THROWS_AS(stubAccessor(kOfxStatErrMemory).getDimensionRaw(kMissing),
                  openfx::OfxException);
  CHECK(log.messages.empty());
}

TEST_CASE(accessor_reads_and_writes_by_name) {
  Props props("ParamsDouble1D");
  PropertyAccessor& a = props.accessor;
  int owned = 0;
  a.setRaw<int>(kOfxParamPropDigits, 5)
      .setRaw<double>(kOfxParamPropIncrement, 0.5)
      .setRaw<const char*>(kOfxPropName, "gain")
      .setRaw<void*>(kOfxParamPropDataPtr, &owned);
  CHECK(a.getRaw<int>(kOfxParamPropDigits) == 5);
  CHECK(a.getRaw<double>(kOfxParamPropIncrement) == 0.5);
  CHECK(std::string(a.getRaw<const char*>(kOfxPropName)) == "gain");
  CHECK(a.getRaw<void*>(kOfxParamPropDataPtr) == &owned);
  CHECK(a.getDimensionRaw(kOfxParamPropInteractMinimumSize) == 2);
}

// setRaw takes a value of any type the C API has a call for, converted.
TEST_CASE(accessor_writes_by_name_from_a_convertible_value) {
  Props props("ParamsDouble1D");
  PropertyAccessor& a = props.accessor;
  int owned = 0;
  a.setRaw(kOfxParamPropSecret, true)
      .setRaw(kOfxParamPropDigits, 5L)
      .setRaw(kOfxParamPropIncrement, 0.5F)
      .setRaw(kOfxPropName, std::string("gain"))
      .setRaw(kOfxPropLabel, "Gain")
      .setRaw(kOfxParamPropDataPtr, &owned);
  CHECK(a.getRaw<bool>(kOfxParamPropSecret));
  CHECK(a.getRaw<int>(kOfxParamPropDigits) == 5);
  CHECK(a.getRaw<double>(kOfxParamPropIncrement) == 0.5);
  CHECK(std::string(a.getRaw<const char*>(kOfxPropName)) == "gain");
  CHECK(std::string(a.getRaw<const char*>(kOfxPropLabel)) == "Gain");
  CHECK(a.getRaw<void*>(kOfxParamPropDataPtr) == &owned);
  a.setRaw(kOfxParamPropDigits, 7U).setRaw(kOfxParamPropSecret, false);
  CHECK(a.getRaw<int>(kOfxParamPropDigits) == 7);
  CHECK(!a.getRaw<bool>(kOfxParamPropSecret));
}

// getRawN and setRawN are propGet*N and propSet*N, soft about a missing
// property alone as every other call is.
TEST_CASE(accessor_reads_and_writes_several_values_by_name) {
  Props descriptor("EffectDescriptor");
  const std::array<const char*, 2> contexts{kOfxImageEffectContextFilter,
                                            kOfxImageEffectContextGeneral};
  descriptor.accessor.setRawN(kOfxImageEffectPropSupportedContexts, 2, contexts.data());
  std::array<const char*, 2> gotContexts{};
  descriptor.accessor.getRawN(kOfxImageEffectPropSupportedContexts, 2,
                              gotContexts.data());
  CHECK(std::string(gotContexts[0]) == kOfxImageEffectContextFilter);
  CHECK(std::string(gotContexts[1]) == kOfxImageEffectContextGeneral);

  Props image("Image");
  const std::array<int, 4> bounds{1, 2, 3, 4};
  image.accessor.setRawN(kOfxImagePropBounds, 4, bounds.data());
  std::array<int, 4> gotBounds{};
  image.accessor.getRawN(kOfxImagePropBounds, 4, gotBounds.data());
  CHECK(gotBounds == bounds);
  const std::array<double, 2> scale{0.5, 0.25};
  image.accessor.setRawN(kOfxImageEffectPropRenderScale, 2, scale.data());
  std::array<double, 2> gotScale{};
  image.accessor.getRawN(kOfxImageEffectPropRenderScale, 2, gotScale.data());
  CHECK(gotScale == scale);

  Props param("ParamsDouble1D");
  int owned = 0;
  const std::array<void*, 1> pointers{&owned};
  param.accessor.setRawN(kOfxParamPropDataPtr, 1, pointers.data());
  void* gotPointer = nullptr;
  param.accessor.getRawN(kOfxParamPropDataPtr, 1, &gotPointer);
  CHECK(gotPointer == &owned);

  std::array<double, 2> missing{7.0, 7.0};
  image.accessor.getRawN(kMissing, 2, missing.data(), false);
  CHECK(missing[0] == 0.0);
  CHECK(missing[1] == 0.0);
  std::array<const char*, 2> missingText{};
  image.accessor.getRawN(kMissing, 2, missingText.data(), false);
  CHECK(missingText[1] != nullptr && *missingText[1] == '\0');
  CHECK_THROWS_AS(image.accessor.getRawN(kMissing, 2, missing.data()),
                  openfx::PropertyNotFoundException);
  image.accessor.setRawN(kMissing, 2, scale.data(), false);
  CHECK_THROWS_AS(image.accessor.setRawN(kMissing, 2, scale.data()),
                  openfx::PropertyNotFoundException);
  CHECK(thrownStatus([&] {
          stubAccessor(kOfxStatErrBadIndex).getRawN(kMissing, 2, missing.data(), false);
        }) == kOfxStatErrBadIndex);
  CHECK(thrownStatus([&] {
          stubAccessor(kOfxStatErrValue).setRawN(kMissing, 2, scale.data(), false);
        }) == kOfxStatErrValue);
}

TEST_CASE(accessor_resets_a_property_to_its_default) {
  const double increment =
      Props("ParamsDouble1D").accessor.get<PropId::OfxParamPropIncrement>();
  const int digits = Props("ParamsDouble1D").accessor.get<PropId::OfxParamPropDigits>();
  Props props("ParamsDouble1D");
  PropertyAccessor& a = props.accessor;
  a.set<PropId::OfxParamPropIncrement>(increment + 1)
      .reset<PropId::OfxParamPropIncrement>();
  CHECK(a.get<PropId::OfxParamPropIncrement>() == increment);
  a.setRaw(kOfxParamPropDigits, digits + 1).reset(kOfxParamPropDigits);
  CHECK(a.get<PropId::OfxParamPropDigits>() == digits);

  CHECK_THROWS_AS(a.reset(kMissing), openfx::PropertyNotFoundException);
  a.reset(kMissing, false);
  CHECK(thrownStatus([&] {
          stubAccessor(kOfxStatErrBadHandle).reset(kMissing, false);
        }) == kOfxStatErrBadHandle);
}

TEST_CASE(accessor_reports_whether_a_property_exists) {
  Props props("ClipDescriptor");
  static_assert(std::is_same_v<decltype(props.accessor.exists(kOfxPropName)), bool>);
  CHECK(props.accessor.exists(kOfxPropName));
  CHECK(props.accessor.exists<PropId::OfxPropName>());
  // Not a clip descriptor property, but one a plugin may write all the same.
  CHECK(!props.accessor.exists(kOfxPropTime));
  CHECK(!props.accessor.exists<PropId::OfxPropTime>());
  props.accessor.setRaw<double>(kOfxPropTime, 1);
  CHECK(props.accessor.exists(kOfxPropTime));
  CHECK(props.accessor.exists<PropId::OfxPropTime>());
  // Absence is the one failure that means no.
  CHECK(thrownStatus([&] { stubAccessor(kOfxStatErrBadHandle).exists(kMissing); }) ==
        kOfxStatErrBadHandle);
}

// The handle and the suite are there for any call the accessor does not wrap.
TEST_CASE(accessor_hands_out_its_property_set_and_suite) {
  Props props("ClipDescriptor");
  CHECK(props.accessor.handle() == props.set.handle());
  CHECK(props.accessor.suite() == PropertySet::suite());
  CHECK(props.accessor.suite()->propSetString(props.accessor.handle(), kOfxPropName, 0,
                                              "Source") == kOfxStatOK);
  CHECK(std::string(props.accessor.get<PropId::OfxPropName>()) == "Source");
}

TEST_CASE(accessor_takes_its_suite_from_a_container) {
  PropertySet set("ClipDescriptor");
  openfx::SuiteContainer suites;
  suites.add(kOfxPropertySuite, 1, PropertySet::suite());
  PropertyAccessor accessor(set.handle(), suites);
  accessor.set<PropId::OfxPropName>("Source");
  CHECK(std::string(accessor.get<PropId::OfxPropName>()) == "Source");
}

TEST_CASE(accessor_without_a_property_suite_throws) {
  PropertySet set("ClipDescriptor");
  const openfx::SuiteContainer empty;
  CHECK_THROWS_AS(PropertyAccessor(set.handle(), empty), openfx::SuiteNotFoundException);
  CHECK_THROWS_AS(PropertyAccessor(set.handle(), nullptr),
                  openfx::SuiteNotFoundException);
}

TEST_CASE(interact_accessor_without_a_property_suite_throws) {
  OfxInteractHandle interact = nullptr;  // never dereferenced: the suites go first
  openfx::SuiteContainer suites;
  suites.add(kOfxInteractSuite, 1, stubInteractSuite());
  CHECK_THROWS_AS(PropertyAccessor(interact, suites), openfx::SuiteNotFoundException);
  CHECK_THROWS_AS(PropertyAccessor(interact, stubInteractSuite(), nullptr),
                  openfx::SuiteNotFoundException);
  // With both suites it is built, as the other constructors are.
  suites.add(kOfxPropertySuite, 1, PropertySet::suite());
  PropertyAccessor accessor(interact, suites);
  accessor.set<PropId::OfxPropName>("overlay");
  CHECK(std::string(accessor.get<PropId::OfxPropName>()) == "overlay");
}

TEST_CASE(accessor_reports_which_types_a_property_supports) {
  CHECK(openfx::prop::exists<PropId::OfxPropName>());
  CHECK(openfx::prop::supportsType<PropId::OfxPropName, const char*>());
  CHECK(!openfx::prop::supportsType<PropId::OfxPropName, double>());
  CHECK(openfx::prop::supportsType<PropId::OfxParamPropDefault, double>());
  CHECK(openfx::prop::supportsType<PropId::OfxParamPropDefault, const char*>());
  CHECK(openfx::prop::supportsType<PropId::OfxParamPropDigits, int>());
}

TEST_CASE(generated_multi_type_list_getters_read_every_value) {
  // The generated <name>All<T>() getters of a multi-type property go through
  // getAllTyped; they had never been instantiated before this test.
  Props props("ParamsDouble1D");
  openfx::plugin::propsets::ParamsDouble1D(props.accessor)
      .setDefaultValue<double>({1.5, 2.5});
  std::vector<double> all =
      openfx::host::propsets::ParamsDouble1D(props.accessor).defaultValueAll<double>();
  CHECK(all.size() == 2);
  CHECK(all[0] == 1.5);
  CHECK(all[1] == 2.5);
}

TEST_CASE(generated_list_getters_read_a_missing_property_softly) {
  Props props("Image");  // declares neither of the properties read below
  const openfx::plugin::propsets::EffectInstance instance(props.accessor);
  const std::array<double, 2> size = instance.projectSize(false);
  CHECK(size[0] == 0.0);
  CHECK(size[1] == 0.0);
  CHECK_THROWS_AS(instance.projectSize(), openfx::PropertyNotFoundException);

  const openfx::host::propsets::ParamsDouble1D param(props.accessor);
  CHECK(param.defaultValueAll<double>(false).empty());
  CHECK_THROWS_AS(param.defaultValueAll<double>(), openfx::PropertyNotFoundException);
}

// A generated class's soft() is a copy of the same class, so its calls chain,
// each soft about a property the set does not have.
TEST_CASE(generated_soft_copy_is_the_same_class) {
  // A host whose every call answers that it has no such property.
  const openfx::plugin::propsets::EffectDescriptor desc(stubAccessor(kOfxStatErrUnknown));
  static_assert(
      std::is_same_v<decltype(desc.soft()), openfx::plugin::propsets::EffectDescriptor>);
  CHECK(thrownStatus([&] {
          desc.soft().setLabel("Gain").setVersionLabel("1.0").setSupportsTiles(true);
        }) == kOfxStatOK);
  CHECK_THROWS_AS(openfx::plugin::propsets::EffectDescriptor(desc).setLabel("Gain"),
                  openfx::PropertyNotFoundException);

  Props props("Image");  // no descriptor properties at all
  const openfx::host::propsets::EffectDescriptor host(props.accessor);
  CHECK(host.soft().label().empty());
  CHECK_THROWS_AS(host.label(), openfx::PropertyNotFoundException);
}
