// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The generated property metadata: the definitions, the per-set and per-action
// tables, and the compile-time traits over them.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxKeySyms.h>
#include <ofxParam.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/ofxPropsBySet.h>
#include <openfx/ofxPropsMetadata.h>
#include <openfx/plugin/ofxPropSetAccessors.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "harness.h"

using openfx::PropId;
using openfx::PropType;

TEST_CASE(metadata_finds_a_property_definition_by_name) {
  const openfx::PropDef* def = openfx::find_prop_def(kOfxPropName);
  CHECK(def != nullptr);
  CHECK(std::string_view(def->name) == kOfxPropName);
  CHECK(def->dimension == 1);
  CHECK(def->supportedTypes.size() == 1);
  CHECK(def->supportedTypes[0] == PropType::String);

  const openfx::PropDef* window = openfx::find_prop_def(kOfxImageEffectPropRenderWindow);
  CHECK(window != nullptr);
  CHECK(window->dimension == 4);
  CHECK(window->supportedTypes[0] == PropType::Int);
}

TEST_CASE(metadata_does_not_find_a_property_it_has_no_definition_for) {
  CHECK(openfx::find_prop_def("OrgExampleHostPropOfOurOwn") == nullptr);
  CHECK(openfx::find_prop_def("") == nullptr);
  // The name search is exact, not a prefix match.
  CHECK(openfx::find_prop_def("OfxPropNam") == nullptr);
  CHECK(openfx::find_prop_def("OfxPropNameExtra") == nullptr);
}

TEST_CASE(metadata_property_definitions_are_sorted_by_name) {
  CHECK(openfx::assertions::prop_defs_sorted);
  bool sorted = true;
  for (size_t i = 1; i < openfx::prop_defs.Size; ++i)
    sorted = sorted && std::string_view(openfx::prop_defs[i - 1].name) <
                           std::string_view(openfx::prop_defs[i].name);
  CHECK(sorted);
  // Every definition is therefore reachable by its own name.
  bool allFound = true;
  for (size_t i = 0; i < openfx::prop_defs.Size; ++i)
    allFound = allFound &&
               openfx::find_prop_def(openfx::prop_defs[i].name) == &openfx::prop_defs[i];
  CHECK(allFound);
}

TEST_CASE(metadata_indexes_the_definitions_by_property_id) {
  CHECK(std::string_view(openfx::prop_defs[PropId::OfxPropLabel].name) == kOfxPropLabel);
  CHECK(std::string_view(openfx::prop_defs[PropId::OfxImageEffectPropRenderScale].name) ==
        kOfxImageEffectPropRenderScale);
  CHECK(static_cast<size_t>(PropId::NProps) == openfx::prop_defs.Size);
  // A handful of names in the OFX headers do carry their "k": the metadata
  // repeats what the header says rather than correcting it.
  CHECK(openfx::find_prop_def(kOfxPropKeySym) != nullptr);
  CHECK(std::string_view(openfx::prop_defs[PropId::OfxPropKeySym].name) ==
        kOfxPropKeySym);
}

TEST_CASE(metadata_lists_the_properties_of_a_property_set) {
  const auto clip = openfx::prop_sets.find("ClipDescriptor");
  CHECK(clip != openfx::prop_sets.end());
  bool foundName = false;
  for (const openfx::Prop& prop : clip->second)
    if (std::string_view(prop.name) == kOfxPropName) {
      foundName = true;
      CHECK(prop.plugin_write);
      CHECK(!prop.host_write);
      CHECK(&prop.def == &openfx::prop_defs[PropId::OfxPropName]);
    }
  CHECK(foundName);

  // The same property is the host's to write on a clip instance.
  const auto instance = openfx::prop_sets.find("ClipInstance");
  CHECK(instance != openfx::prop_sets.end());
  for (const openfx::Prop& prop : instance->second)
    if (std::string_view(prop.name) == kOfxPropName) {
      CHECK(prop.host_write);
      CHECK(!prop.plugin_write);
    }
}

TEST_CASE(metadata_has_no_properties_for_an_unknown_property_set) {
  CHECK(openfx::prop_sets.find("NoSuchPropertySet") == openfx::prop_sets.end());
  CHECK(openfx::prop_sets.find("ParamsDouble1D") != openfx::prop_sets.end());
  CHECK(openfx::prop_sets.find("Image") != openfx::prop_sets.end());
}

TEST_CASE(metadata_lists_the_arguments_of_an_action) {
  using Key = std::array<std::string_view, 2>;
  const auto render =
      openfx::action_props.find(Key{kOfxImageEffectActionRender, "inArgs"});
  CHECK(render != openfx::action_props.end());
  const auto& names = render->second;
  const auto has = [&names](const char* name) {
    return std::find_if(names.begin(), names.end(), [name](const char* candidate) {
             return std::string_view(candidate) == name;
           }) != names.end();
  };
  CHECK(has(kOfxPropTime));
  CHECK(has(kOfxImageEffectPropRenderWindow));
  CHECK(has(kOfxImageEffectPropRenderScale));
  CHECK(!has(kOfxImageEffectPropRegionOfDefinition));

  CHECK(openfx::action_props.find(Key{kOfxImageEffectActionRender, "outArgs"}) ==
        openfx::action_props.end());
  CHECK(openfx::action_props.find(Key{"OfxActionNoSuchThing", "inArgs"}) ==
        openfx::action_props.end());
  CHECK(openfx::action_props.find(Key{kOfxImageEffectActionGetRegionOfDefinition,
                                      "outArgs"}) != openfx::action_props.end());
}

TEST_CASE(metadata_carries_the_spec_defaults) {
  CHECK(std::string_view(
            openfx::prop_defs[PropId::OfxImageEffectPropSupportsTiles].defaults[0]) ==
        "1");
  CHECK(std::string_view(openfx::prop_defs[PropId::OfxParamPropDigits].defaults[0]) ==
        "2");
  // One per dimension where the spec gives one per dimension.
  const openfx::PropDef& size =
      openfx::prop_defs[PropId::OfxParamPropInteractMinimumSize];
  CHECK(size.dimension == 2);
  CHECK(size.defaults.size() == 2);
  CHECK(std::string_view(size.defaults[1]) == "10.0");
  // Most properties have none.
  CHECK(openfx::prop_defs[PropId::OfxPropLabel].defaults.empty());
}

TEST_CASE(metadata_carries_the_enum_values_of_an_enum_property) {
  const openfx::PropDef& field =
      openfx::prop_defs[PropId::OfxImageClipPropFieldExtraction];
  CHECK(field.supportedTypes[0] == PropType::Enum);
  CHECK(field.enumValues.size() == 6);
  CHECK(std::string_view(field.enumValues[0]) == kOfxImageFieldNone);
  CHECK(std::string_view(field.enumValues[1]) == kOfxImageFieldLower);
  // A plain string property has none.
  CHECK(openfx::prop_defs[PropId::OfxPropLabel].enumValues.empty());
}

TEST_CASE(metadata_enum_values_are_reachable_through_EnumValue) {
  using Field = openfx::EnumValue<PropId::OfxImageClipPropFieldExtraction>;
  CHECK(Field::size() == 6);
  CHECK(std::string_view(Field::get(1)) == kOfxImageFieldLower);
  CHECK(Field::isValid(kOfxImageFieldUpper));
  CHECK(!Field::isValid("OfxFieldSideways"));
  static_assert(Field::size() == 6, "the field extraction values are a fixed list");
  static_assert(Field::get(0) != nullptr, "and are known at compile time");

  using Style = openfx::EnumValue<PropId::OfxImageEffectPropColourManagementStyle>;
  CHECK(Style::isValid(kOfxImageEffectColourManagementCore));
  CHECK(Style::size() == 5);
}

TEST_CASE(metadata_traits_give_each_property_its_native_type) {
  static_assert(
      std::is_same_v<openfx::PropTraits_t<PropId::OfxPropName>::type, const char*>);
  static_assert(
      std::is_same_v<openfx::PropTraits_t<PropId::OfxParamPropDigits>::type, int>);
  static_assert(
      std::is_same_v<openfx::PropTraits_t<PropId::OfxParamPropIncrement>::type, double>);
  static_assert(
      std::is_same_v<openfx::PropTraits_t<PropId::OfxParamPropDataPtr>::type, void*>);
  static_assert(
      std::is_same_v<openfx::PropTraits_t<PropId::OfxParamPropSecret>::type, bool>);
  static_assert(!openfx::PropTraits_t<PropId::OfxPropName>::is_multitype);
  static_assert(openfx::PropTraits_t<PropId::OfxParamPropDefault>::is_multitype);
  static_assert(
      std::is_same_v<openfx::PropTypeToNative<PropType::Enum>::type, const char*>);
  static_assert(std::is_same_v<openfx::PropTypeToNative<PropType::Bool>::type, int>);
  CHECK(openfx::PropTraits_t<PropId::OfxPropName>::def.dimension == 1);
}

TEST_CASE(metadata_getter_names_keep_their_acronyms) {
  // Naming each getter is the test: it has to compile, and so has to be
  // spelled with its acronym intact.
  namespace propsets = openfx::plugin::propsets;
  const auto named = [](auto getter) { return getter != nullptr; };
  CHECK(named(&propsets::EffectInstance::ocioConfig));
  CHECK(named(&propsets::EffectInstance::ocioDisplay));
  CHECK(named(&propsets::EffectInstance::ocioView));
  CHECK(named(&propsets::EffectInstance::openGLRenderSupported));
  CHECK(named(&propsets::EffectInstance::cpuRenderSupported));
  CHECK(named(&propsets::EffectInstance::pixelAspectRatio));
  CHECK(named(&propsets::ImageEffectHost::openCLSupported));
  CHECK(named(&propsets::ImageEffectHost::cudaStreamSupported));
  CHECK(named(&propsets::ImageEffectActionRender_InArgs::openCLSupported));
}
