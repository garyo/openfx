// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The plugin side next to plain C code: an action the dispatchers have no
// virtual for, overlays wired up by hand, wrappers built from a container made
// on the spot, a main entry written by hand, and several plugins in a binary.

#include <ofxCore.h>
#include <ofxGPURender.h>
#include <ofxImageEffect.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/plugin/ofxInteract.h>
#include <openfx/plugin/ofxPluginBase.h>

#include <string>
#include <vector>

#include "fixture.h"
#include "harness.h"

namespace plugin = openfx::plugin;
namespace host = openfx::host;

namespace {

// One call to otherAction, as it arrived.
struct RawCall {
  std::string action;
  const void* handle;
  OfxPropertySetHandle inArgs;
  OfxPropertySetHandle outArgs;
};

// An effect that renders, and records every action it has no virtual for.
class Hooked : public plugin::ImageEffectPlugin {
 public:
  std::vector<RawCall> other;
  bool rendered = false;

 protected:
  OfxStatus render(plugin::ImageEffect&, plugin::ActionArgs&) override {
    rendered = true;
    return kOfxStatOK;
  }
  OfxStatus otherAction(const char* action, const void* handle,
                        OfxPropertySetHandle inArgs,
                        OfxPropertySetHandle outArgs) override {
    other.push_back({action, handle, inArgs, outArgs});
    return kOfxStatOK;
  }
};

// An overlay that records the actions it has a virtual for, and every one it
// has none for.
class HookedOverlay : public plugin::InteractPlugin<HookedOverlay> {
 public:
  std::vector<std::string> ran;
  std::vector<RawCall> other;

 protected:
  OfxStatus describe(plugin::Interact&) override {
    ran.emplace_back("describe");
    return kOfxStatOK;
  }
  OfxStatus otherAction(const char* action, const void* handle,
                        OfxPropertySetHandle inArgs,
                        OfxPropertySetHandle outArgs) override {
    other.push_back({action, handle, inArgs, outArgs});
    return kOfxStatOK;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// otherAction
// ---------------------------------------------------------------------------

TEST_CASE(other_action_gets_the_opengl_context_actions_as_the_host_passed_them) {
  tests::Effect effect;
  Hooked hooked;
  hooked.setHost(effect.host.ofx());
  CHECK(hooked.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr) ==
        kOfxStatReplyDefault);

  host::PropertySet in;
  CHECK(hooked.dispatch(kOfxActionOpenGLContextAttached, effect.handle(), in.handle(),
                        nullptr) == kOfxStatOK);
  CHECK(hooked.dispatch(kOfxActionOpenGLContextDetached, effect.handle(), nullptr,
                        nullptr) == kOfxStatOK);
  CHECK(hooked.other.size() == 2);
  CHECK(hooked.other[0].action == kOfxActionOpenGLContextAttached);
  CHECK(hooked.other[0].handle == effect.handle());
  CHECK(hooked.other[0].inArgs == in.handle());
  CHECK(hooked.other[0].outArgs == nullptr);
  CHECK(hooked.other[1].action == kOfxActionOpenGLContextDetached);
  CHECK(hooked.other[1].handle == effect.handle());
  CHECK(hooked.other[1].inArgs == nullptr);
}

TEST_CASE(other_action_gets_an_action_of_the_hosts_own_untouched) {
  tests::Effect effect;
  Hooked hooked;
  hooked.setHost(effect.host.ofx());
  hooked.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr);

  int notAnEffect = 0;
  host::PropertySet in;
  host::PropertySet out;
  CHECK(hooked.dispatch("com.example.HostAction", &notAnEffect, in.handle(),
                        out.handle()) == kOfxStatOK);
  CHECK(hooked.other.size() == 1);
  CHECK(hooked.other[0].action == "com.example.HostAction");
  CHECK(hooked.other[0].handle == &notAnEffect);
  CHECK(hooked.other[0].inArgs == in.handle());
  CHECK(hooked.other[0].outArgs == out.handle());

  // An action with a virtual of its own still goes there.
  CHECK(hooked.dispatch(kOfxImageEffectActionRender, effect.handle(), nullptr, nullptr) ==
        kOfxStatOK);
  CHECK(hooked.rendered);
  CHECK(hooked.other.size() == 1);
}

TEST_CASE(an_overlays_other_action_gets_an_action_of_the_hosts_own) {
  HookedOverlay overlay;
  int handle = 0;
  host::PropertySet in;
  CHECK(overlay.dispatch("com.example.OverlayAction", &handle, in.handle(), nullptr) ==
        kOfxStatOK);
  CHECK(overlay.other.size() == 1);
  CHECK(overlay.other[0].action == "com.example.OverlayAction");
  CHECK(overlay.other[0].handle == &handle);
  CHECK(overlay.other[0].inArgs == in.handle());
  CHECK(overlay.ran.empty());
}
