// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// openfx::host::PropertySet: the metadata-driven property store and the
// OfxPropertySuiteV1 over it.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <openfx/host/ofxPropertySet.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "harness.h"

using openfx::host::PropertySet;

namespace {

// The property a test writes that the metadata says nothing about.
constexpr const char* kUndeclared = "OrgExampleHostPropSomethingOfOurOwn";

}  // namespace

TEST_CASE(propertyset_defines_the_named_set_from_the_metadata) {
  PropertySet clip("ClipDescriptor");
  CHECK(clip.has(kOfxPropName));
  CHECK(clip.has(kOfxImageClipPropOptional));
  CHECK(!clip.has(kOfxImageEffectPropFrameRate));  // that is a clip instance property
  CHECK_THROWS_AS(PropertySet("NoSuchPropertySet"), std::runtime_error);
}

TEST_CASE(propertyset_takes_each_propertys_type_from_the_metadata) {
  PropertySet param("ParamsDouble1D");
  CHECK(param.find(kOfxPropName)->type == PropertySet::Type::String);
  CHECK(param.find(kOfxParamPropIncrement)->type == PropertySet::Type::Double);
  CHECK(param.find(kOfxParamPropDigits)->type == PropertySet::Type::Int);
  CHECK(param.find(kOfxParamPropCanUndo)->type == PropertySet::Type::Int);  // bool
  CHECK(param.find(kOfxParamPropDataPtr)->type == PropertySet::Type::Pointer);
  // A multi-typed property takes its type from the first write instead.
  CHECK(!param.has(kOfxParamPropDefault));
}

TEST_CASE(propertyset_takes_each_propertys_dimension_from_the_metadata) {
  PropertySet param("ParamsDouble1D");
  CHECK(param.find(kOfxParamPropIncrement)->dimension == 1);
  CHECK(param.find(kOfxParamPropInteractMinimumSize)->dimension == 2);
  PropertySet effect("EffectDescriptor");
  CHECK(effect.find(kOfxImageEffectPropSupportedContexts)->dimension == 0);
  int n = -1;
  CHECK(effect.dimension(kOfxImageEffectPropSupportedContexts, &n) == kOfxStatOK);
  CHECK(n == 0);  // a variable-dimension property starts empty
}

TEST_CASE(propertyset_seeds_the_spec_defaults_from_the_metadata) {
  PropertySet effect("EffectDescriptor");
  CHECK(effect.getInt(kOfxImageEffectPropSupportsTiles) == 1);
  CHECK(effect.getString(kOfxImageEffectPluginRenderThreadSafety) ==
        kOfxImageEffectRenderInstanceSafe);
  PropertySet param("ParamsDouble1D");
  CHECK(param.getInt(kOfxParamPropDigits) == 2);
  CHECK(param.getDouble(kOfxParamPropIncrement) == 1.0);
  CHECK(param.getString(kOfxParamPropDoubleType) == kOfxParamDoubleTypePlain);
  // One default per dimension where the spec gives one.
  CHECK(param.getDouble(kOfxParamPropInteractMinimumSize, 0) == 10.0);
  CHECK(param.getDouble(kOfxParamPropInteractMinimumSize, 1) == 10.0);
  // A property the spec gives no default for starts at zero.
  CHECK(param.getInt(kOfxParamPropSecret) == 0);
}

TEST_CASE(propertyset_reads_fall_through_to_the_parent) {
  PropertySet parent("ClipDescriptor");
  parent.set(kUndeclared, 0, 7);
  PropertySet child("ClipInstance", &parent);
  CHECK(child.getInt(kUndeclared) == 7);
  CHECK(child.find(kUndeclared) != nullptr);
  // Writing shadows the parent's definition and leaves the parent alone.
  CHECK(child.set(kUndeclared, 0, 9) == kOfxStatOK);
  CHECK(child.getInt(kUndeclared) == 9);
  CHECK(parent.getInt(kUndeclared) == 7);
}

TEST_CASE(propertyset_seeds_shared_properties_from_the_parent) {
  PropertySet parent("ClipDescriptor");
  parent.set(kOfxPropLabel, 0, "Source");
  parent.set(kOfxImageClipPropOptional, 0, 1);
  PropertySet child("ClipInstance", &parent);
  CHECK(child.getString(kOfxPropLabel) == "Source");
  CHECK(child.getInt(kOfxImageClipPropOptional) == 1);
  // The copy is taken once: a later write to the parent does not reach it.
  parent.set(kOfxPropLabel, 0, "Renamed");
  CHECK(child.getString(kOfxPropLabel) == "Source");
}

TEST_CASE(propertyset_creates_an_undeclared_property_from_the_value) {
  PropertySet set("ClipDescriptor");
  CHECK(set.set(kUndeclared, 0, 1.5) == kOfxStatOK);
  CHECK(set.find(kUndeclared)->type == PropertySet::Type::Double);
  CHECK(set.getDouble(kUndeclared) == 1.5);
  // It has no declared dimension, so it grows to the index written.
  CHECK(set.set(kUndeclared, 3, 2.5) == kOfxStatOK);
  int n = 0;
  CHECK(set.dimension(kUndeclared, &n) == kOfxStatOK);
  CHECK(n == 4);
  CHECK(set.getDouble(kUndeclared, 3) == 2.5);
}

TEST_CASE(propertyset_creates_a_declared_property_with_its_metadata_type) {
  // A property the metadata knows but this set does not list keeps its type,
  // whatever type the value written has.
  PropertySet set("ClipDescriptor");
  CHECK(set.set(kOfxPropTime, 0, 3) == kOfxStatOK);
  CHECK(set.find(kOfxPropTime)->type == PropertySet::Type::Double);
  CHECK(set.getDouble(kOfxPropTime) == 3.0);
}

TEST_CASE(propertyset_reports_a_type_mismatch) {
  PropertySet set("ClipDescriptor");
  CHECK(set.set(kOfxImageClipPropOptional, 0, "yes") == kOfxStatErrValue);
  char* text = nullptr;
  CHECK(set.get(kOfxImageClipPropOptional, 0, &text) == kOfxStatErrValue);
  void* pointer = nullptr;
  CHECK(set.get(kOfxPropName, 0, &pointer) == kOfxStatErrValue);
  int number = 0;
  CHECK(set.get(kOfxPropName, 0, &number) == kOfxStatErrValue);
}

TEST_CASE(propertyset_reports_a_bad_index) {
  PropertySet set("ClipDescriptor");
  CHECK(set.set(kOfxPropName, -1, "x") == kOfxStatErrBadIndex);
  CHECK(set.set(kOfxPropName, 1, "x") == kOfxStatErrBadIndex);  // dimension is 1
  char* text = nullptr;
  CHECK(set.get(kOfxPropName, -1, &text) == kOfxStatErrBadIndex);
  CHECK(set.get(kOfxPropName, 1, &text) == kOfxStatErrBadIndex);
}

TEST_CASE(propertyset_reports_an_unknown_property) {
  PropertySet set("ClipDescriptor");
  int value = 0;
  CHECK(set.get("OrgExampleHostPropNeverWritten", 0, &value) == kOfxStatErrUnknown);
  int n = 0;
  CHECK(set.dimension("OrgExampleHostPropNeverWritten", &n) == kOfxStatErrUnknown);
  CHECK(set.reset("OrgExampleHostPropNeverWritten") == kOfxStatErrUnknown);
}

TEST_CASE(propertyset_coerces_between_int_and_double) {
  PropertySet set("ParamsDouble1D");
  CHECK(set.set(kOfxParamPropIncrement, 0, 3) == kOfxStatOK);  // int into a double
  CHECK(set.getDouble(kOfxParamPropIncrement) == 3.0);
  int asInt = 0;
  CHECK(set.get(kOfxParamPropIncrement, 0, &asInt) == kOfxStatOK);
  CHECK(asInt == 3);
  CHECK(set.set(kOfxParamPropDigits, 0, 2.7) == kOfxStatOK);  // double into an int
  CHECK(set.getInt(kOfxParamPropDigits) == 2);
  double asDouble = 0;
  CHECK(set.get(kOfxParamPropDigits, 0, &asDouble) == kOfxStatOK);
  CHECK(asDouble == 2.0);
}

TEST_CASE(propertyset_resets_a_property_to_its_declared_default) {
  PropertySet set("ParamsDouble1D");
  set.set(kOfxParamPropInteractMinimumSize, 0, 20.0);
  set.set(kOfxParamPropInteractMinimumSize, 1, 30.0);
  CHECK(set.reset(kOfxParamPropInteractMinimumSize) == kOfxStatOK);
  CHECK(set.getDouble(kOfxParamPropInteractMinimumSize, 0) == 10.0);
  CHECK(set.getDouble(kOfxParamPropInteractMinimumSize, 1) == 10.0);
  int n = 0;
  CHECK(set.dimension(kOfxParamPropInteractMinimumSize, &n) == kOfxStatOK);
  CHECK(n == 2);

  // A property whose default is not zero comes back to that default too.
  PropertySet clip("ClipDescriptor");
  clip.set(kOfxImageEffectPropSupportsTiles, 0, 0);
  CHECK(clip.getInt(kOfxImageEffectPropSupportsTiles) == 0);
  CHECK(clip.reset(kOfxImageEffectPropSupportsTiles) == kOfxStatOK);
  CHECK(clip.getInt(kOfxImageEffectPropSupportsTiles) == 1);

  set.set(kUndeclared, 2, "third");  // variable dimension: reset empties it
  CHECK(set.reset(kUndeclared) == kOfxStatOK);
  CHECK(set.dimension(kUndeclared, &n) == kOfxStatOK);
  CHECK(n == 0);
}

TEST_CASE(propertyset_defines_a_property_of_its_own) {
  PropertySet set("ClipDescriptor");
  set.define("OrgExampleHostPropRegion", PropertySet::Type::Double, 4);
  CHECK(set.find("OrgExampleHostPropRegion")->dimension == 4);
  int n = 0;
  CHECK(set.dimension("OrgExampleHostPropRegion", &n) == kOfxStatOK);
  CHECK(n == 4);
  CHECK(set.set("OrgExampleHostPropRegion", 3, 1.0) == kOfxStatOK);
  CHECK(set.set("OrgExampleHostPropRegion", 4, 1.0) == kOfxStatErrBadIndex);
}

TEST_CASE(propertyset_for_an_action_defines_that_actions_arguments) {
  PropertySet in = PropertySet::forAction(kOfxImageEffectActionRender, "inArgs");
  CHECK(in.has(kOfxPropTime));
  CHECK(in.find(kOfxImageEffectPropRenderWindow)->dimension == 4);
  CHECK(in.find(kOfxImageEffectPropRenderScale)->dimension == 2);
  CHECK(in.find(kOfxImageEffectPropRenderWindow)->type == PropertySet::Type::Int);

  PropertySet out =
      PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "outArgs");
  CHECK(out.has(kOfxImageEffectPropRegionOfDefinition));
  CHECK(!out.has(kOfxPropTime));  // that one is an in-arg
}

TEST_CASE(propertyset_for_an_unknown_action_is_empty) {
  PropertySet none = PropertySet::forAction("OfxActionNoSuchThing", "inArgs");
  CHECK(!none.has(kOfxPropTime));
  PropertySet neither = PropertySet::forAction(kOfxImageEffectActionRender, "outArgs");
  CHECK(!neither.has(kOfxPropTime));  // Render has no out-args at all
}

TEST_CASE(propertyset_seeds_action_arguments_with_their_defaults) {
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionGetRegionsOfInterest, "inArgs");
  CHECK(in.getString(kOfxImageEffectPropThumbnailRender).empty() ||
        in.getString(kOfxImageEffectPropThumbnailRender) == "false");
}

TEST_CASE(propertyset_reads_convenience_values_with_a_fallback) {
  PropertySet set("ClipDescriptor");
  CHECK(set.getInt("OrgExampleHostPropMissing", 0, 42) == 42);
  CHECK(set.getDouble("OrgExampleHostPropMissing", 0, 2.5) == 2.5);
  CHECK(set.getString("OrgExampleHostPropMissing", 0, "fallback") == "fallback");
  set.set(kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
  set.set(kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentAlpha);
  const std::vector<std::string> components =
      set.getStrings(kOfxImageEffectPropSupportedComponents);
  CHECK(components.size() == 2);
  CHECK(components[1] == kOfxImageComponentAlpha);
  CHECK(set.getStrings("OrgExampleHostPropMissing").empty());
}

TEST_CASE(propertyset_dumps_its_properties) {
  PropertySet set("ClipDescriptor");
  set.set(kOfxPropName, 0, "Source");
  const std::string dump = set.dump("  ");
  CHECK(dump.find("OfxPropName = \"Source\"") != std::string::npos);
  CHECK(dump.find("  ") == 0);
}

TEST_CASE(propertyset_handle_round_trips) {
  PropertySet set("ClipDescriptor");
  CHECK(PropertySet::from(set.handle()) == &set);
}

TEST_CASE(property_suite_round_trips_every_type) {
  PropertySet set("ParamsDouble1D");
  OfxPropertySetHandle handle = set.handle();
  const OfxPropertySuiteV1* suite = PropertySet::suite();

  CHECK(suite->propSetString(handle, kOfxPropName, 0, "gain") == kOfxStatOK);
  char* name = nullptr;
  CHECK(suite->propGetString(handle, kOfxPropName, 0, &name) == kOfxStatOK);
  CHECK(std::string(name) == "gain");

  CHECK(suite->propSetDouble(handle, kOfxParamPropIncrement, 0, 0.25) == kOfxStatOK);
  double increment = 0;
  CHECK(suite->propGetDouble(handle, kOfxParamPropIncrement, 0, &increment) ==
        kOfxStatOK);
  CHECK(increment == 0.25);

  CHECK(suite->propSetInt(handle, kOfxParamPropDigits, 0, 4) == kOfxStatOK);
  int digits = 0;
  CHECK(suite->propGetInt(handle, kOfxParamPropDigits, 0, &digits) == kOfxStatOK);
  CHECK(digits == 4);

  int owned = 0;
  CHECK(suite->propSetPointer(handle, kOfxParamPropDataPtr, 0, &owned) == kOfxStatOK);
  void* pointer = nullptr;
  CHECK(suite->propGetPointer(handle, kOfxParamPropDataPtr, 0, &pointer) == kOfxStatOK);
  CHECK(pointer == &owned);
}

TEST_CASE(property_suite_round_trips_whole_properties) {
  PropertySet set("ParamsDouble1D");
  OfxPropertySetHandle handle = set.handle();
  const OfxPropertySuiteV1* suite = PropertySet::suite();

  const double sizes[2] = {3.0, 4.0};
  CHECK(suite->propSetDoubleN(handle, kOfxParamPropInteractMinimumSize, 2, sizes) ==
        kOfxStatOK);
  double back[2] = {0, 0};
  CHECK(suite->propGetDoubleN(handle, kOfxParamPropInteractMinimumSize, 2, back) ==
        kOfxStatOK);
  CHECK(back[0] == 3.0);
  CHECK(back[1] == 4.0);

  const int flags[1] = {1};
  CHECK(suite->propSetIntN(handle, kOfxParamPropSecret, 1, flags) == kOfxStatOK);
  int flagsBack[1] = {0};
  CHECK(suite->propGetIntN(handle, kOfxParamPropSecret, 1, flagsBack) == kOfxStatOK);
  CHECK(flagsBack[0] == 1);

  const char* labels[1] = {"Gain"};
  CHECK(suite->propSetStringN(handle, kOfxPropLabel, 1, labels) == kOfxStatOK);
  char* labelsBack[1] = {nullptr};
  CHECK(suite->propGetStringN(handle, kOfxPropLabel, 1, labelsBack) == kOfxStatOK);
  CHECK(std::string(labelsBack[0]) == "Gain");

  int owned = 0;
  void* pointers[1] = {&owned};
  CHECK(suite->propSetPointerN(handle, kOfxParamPropDataPtr, 1, pointers) == kOfxStatOK);
  void* pointersBack[1] = {nullptr};
  CHECK(suite->propGetPointerN(handle, kOfxParamPropDataPtr, 1, pointersBack) ==
        kOfxStatOK);
  CHECK(pointersBack[0] == &owned);
}

TEST_CASE(property_suite_resets_and_reports_dimensions) {
  PropertySet set("ParamsDouble1D");
  OfxPropertySetHandle handle = set.handle();
  const OfxPropertySuiteV1* suite = PropertySet::suite();

  int dimension = 0;
  CHECK(suite->propGetDimension(handle, kOfxParamPropInteractMinimumSize, &dimension) ==
        kOfxStatOK);
  CHECK(dimension == 2);
  CHECK(suite->propSetDouble(handle, kOfxParamPropInteractMinimumSize, 1, 8.0) ==
        kOfxStatOK);
  CHECK(suite->propReset(handle, kOfxParamPropInteractMinimumSize) == kOfxStatOK);
  CHECK(set.getDouble(kOfxParamPropInteractMinimumSize, 1) == 10.0);  // the default
  CHECK(suite->propGetDimension(handle, "OrgExampleHostPropMissing", &dimension) ==
        kOfxStatErrUnknown);
}

TEST_CASE(property_suite_rejects_a_null_handle) {
  const OfxPropertySuiteV1* suite = PropertySet::suite();
  int value = 0;
  CHECK(suite->propSetInt(nullptr, kOfxParamPropDigits, 0, 1) == kOfxStatErrBadHandle);
  CHECK(suite->propGetInt(nullptr, kOfxParamPropDigits, 0, &value) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propSetString(nullptr, kOfxPropName, 0, "x") == kOfxStatErrBadHandle);
  CHECK(suite->propSetDouble(nullptr, kOfxParamPropIncrement, 0, 1.0) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propSetPointer(nullptr, kOfxParamPropDataPtr, 0, nullptr) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propReset(nullptr, kOfxPropName) == kOfxStatErrBadHandle);
  CHECK(suite->propGetDimension(nullptr, kOfxPropName, &value) == kOfxStatErrBadHandle);
  const int one = 1;
  CHECK(suite->propSetIntN(nullptr, kOfxParamPropDigits, 1, &one) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propGetIntN(nullptr, kOfxParamPropDigits, 1, &value) ==
        kOfxStatErrBadHandle);
}
