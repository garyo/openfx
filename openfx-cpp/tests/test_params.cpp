// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// A parameter set's names, as a plugin keeps them, and its values that aren't
// their defaults, as a plugin logs them.

#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <openfx/plugin/ofxEffect.h>
#include <openfx/plugin/ofxParam.h>

#include <string>
#include <vector>

#include "fixture.h"
#include "harness.h"

namespace plugin = openfx::plugin;

TEST_CASE(a_param_set_remembers_the_names_defined_through_it_and_its_copies) {
  tests::Effect effect;
  std::vector<std::string> names;
  plugin::ParamSet params(effect.handle(), effect.suites);
  params.defineDouble("before");
  params.rememberNames(names).defineDouble("scale");
  plugin::ParamSet copy = params;
  copy.defineGroup("group");
  CHECK(names == std::vector<std::string>{"scale", "group"});
}

TEST_CASE(values_changed_gives_each_value_that_is_not_its_default) {
  tests::Effect effect;
  std::vector<std::string> names;
  plugin::ParamSet descriptor(effect.handle(), effect.suites);
  descriptor.rememberNames(names);
  descriptor.defineDouble("scale").setDefaultValue<double>(1.0);
  descriptor.defineDouble2D("centre");
  descriptor.defineInt("count");
  descriptor.defineBoolean("on");
  descriptor.defineRGBA("gain");
  descriptor.defineString("caption");
  descriptor.defineGroup("group");

  tests::Instance instance(*effect.contextDescriptor);
  instance.create();
  plugin::ParamSet params(instance.handle(), effect.suites);
  CHECK(params.valuesChanged(names, 0.0).empty());

  params.get<plugin::DoubleParam>("scale").setValue(2.5);
  params.get<plugin::Double2DParam>("centre").setValue({0.5, 1.0 / 3.0});
  params.get<plugin::BooleanParam>("on").setValue(true);
  params.get<plugin::StringParam>("caption").setValue("two \"words\"");
  const std::string changed =
      "scale=2.5 centre=0.5,0.3333333333 on=1 caption=\"two \\\"words\\\"\"";
  CHECK(params.valuesChanged(names, 0.0) == changed);

  // A name that isn't a parameter's can't be read.
  names.emplace_back("missing");
  CHECK(params.valuesChanged(names, 0.0) == changed + " missing=?");
}
