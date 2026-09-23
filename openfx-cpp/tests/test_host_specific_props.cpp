// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// Host-defined properties, as `gen-props.py host-metadata` generates them for
// a host's own namespace: the MyHost example's, read and written through
// openfx::PropertyAccessor like any property of the specification.

#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>
#include <openfx/ofxPropsAccess.h>

#include <array>
#include <string>
#include <string_view>

#include "../examples/host-specific-props/myhost/myhostPropsMetadata.h"
#include "harness.h"

using openfx::PropertyAccessor;
using openfx::host::PropertySet;

TEST_CASE(host_metadata_lists_an_enum_propertys_values) {
  using Quality = openfx::EnumValue<myhost::PropId::MyHostRenderQuality>;
  static_assert(Quality::size() == 2);
  static_assert(Quality::isValid("com.example.myhost.RenderQualityDraft"));
  static_assert(!Quality::isValid("com.example.myhost.RenderQualityBest"));
  CHECK(std::string(Quality::get(1)) == "com.example.myhost.RenderQualityFinal");
}

TEST_CASE(host_properties_are_read_and_written_through_an_accessor) {
  PropertySet set("EffectInstance");
  // The host defines its own properties; the metadata only describes them.
  set.define(kMyHostRenderQuality, PropertySet::Type::String, 1);
  set.define(kMyHostNodeColor, PropertySet::Type::Int, 3);
  PropertyAccessor props(set.handle(), PropertySet::suite());

  props.set<myhost::PropId::MyHostRenderQuality>("com.example.myhost.RenderQualityDraft");
  CHECK(std::string(props.get<myhost::PropId::MyHostRenderQuality>()) ==
        "com.example.myhost.RenderQualityDraft");

  props.setAll<myhost::PropId::MyHostNodeColor>({255, 128, 64});
  const std::array<int, 3> colour = props.getAll<myhost::PropId::MyHostNodeColor>();
  CHECK(colour[0] == 255);
  CHECK(colour[2] == 64);
}

TEST_CASE(host_metadata_gives_each_property_a_c_name) {
  static_assert(std::string_view(kMyHostViewerProcess) ==
                "com.example.myhost.ViewerProcess");
  CHECK(
      std::string_view(openfx::PropTraits_t<myhost::PropId::MyHostNodeColor>::def.name) ==
      kMyHostNodeColor);

  // So C code reads what C++ code wrote, by the same constant.
  PropertySet set("EffectInstance");
  set.define(kMyHostNodeName, PropertySet::Type::String, 1);
  PropertyAccessor(set.handle(), PropertySet::suite())
      .set<myhost::PropId::MyHostNodeName>("Grade1");
  char* name = nullptr;
  CHECK(PropertySet::suite()->propGetString(set.handle(), kMyHostNodeName, 0, &name) ==
        kOfxStatOK);
  CHECK(std::string(name) == "Grade1");
}

TEST_CASE(host_properties_another_host_lacks_are_read_softly) {
  // A plugin cannot know which host it is in, so it asks for MyHost's
  // properties without treating their absence as an error.
  PropertySet set("EffectInstance");
  PropertyAccessor props(set.handle(), PropertySet::suite());
  CHECK(props.soft().get<myhost::PropId::MyHostViewerProcess>().empty());
  CHECK(!props.find<myhost::PropId::MyHostViewerProcess>().has_value());
  const std::array<int, 3> colour =
      props.soft().getAll<myhost::PropId::MyHostNodeColor>();
  CHECK(colour[0] == 0);
  CHECK_THROWS_AS(props.getAll<myhost::PropId::MyHostNodeColor>(),
                  openfx::PropertyNotFoundException);
}
