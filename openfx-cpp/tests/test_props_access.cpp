// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// openfx::PropertyAccessor, over a host property set standing in for the one a
// host would hand a plugin.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <openfx/host/ofxPropSetAccessors.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>
#include <openfx/ofxMisc.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/ofxSuites.h>
#include <openfx/plugin/ofxPropSetAccessors.h>

#include <array>
#include <string>
#include <vector>

#include "harness.h"

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
  const std::vector<const char*> contexts =
      props.accessor.getAll<PropId::OfxImageEffectPropSupportedContexts>();
  CHECK(contexts.size() == 2);
  CHECK(std::string(contexts[0]) == kOfxImageEffectContextFilter);
  CHECK(std::string(contexts[1]) == kOfxImageEffectContextGeneral);
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

TEST_CASE(accessor_error_if_missing_false_returns_a_default_instead) {
  Props props("ClipDescriptor");
  CHECK(props.accessor.get<PropId::OfxImageEffectPropFrameRate>(0, false) == 0.0);
  CHECK(props.accessor.getRaw<int>(kMissing, 0, false) == 0);
  CHECK(props.accessor.getRaw<const char*>(kMissing, 0, false) == nullptr);
  CHECK(props.accessor.getRaw<void*>(kMissing, 0, false) == nullptr);
  CHECK(props.accessor.getDimensionRaw(kMissing, false) == -1);
  // Writing is just as forgiving: the wrong type is a warning, not a throw.
  props.accessor.setRaw<const char*>(kOfxImageClipPropOptional, "yes", 0, false);
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

TEST_CASE(accessor_reports_whether_a_property_exists) {
  Props props("ClipDescriptor");
  CHECK(props.accessor.exists(kOfxPropName));
  CHECK(!props.accessor.exists(kMissing));
  props.accessor.setRaw<int>(kMissing, 1);
  CHECK(props.accessor.exists(kMissing));
}

TEST_CASE(accessor_holds_the_property_set_it_reads) {
  Props props("ClipDescriptor");
  CHECK(props.accessor.handle() == props.set.handle());
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
