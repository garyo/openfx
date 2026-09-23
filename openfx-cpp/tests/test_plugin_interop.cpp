// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The plugin side next to plain C code: an action the dispatchers have no
// virtual for, overlays wired up by hand, wrappers built from a container made
// on the spot, a main entry written by hand, and several plugins in a binary.

#include <ofxCore.h>
#include <ofxDialog.h>
#include <ofxGPURender.h>
#include <ofxImageEffect.h>
#include <ofxParametricParam.h>
#include <openfx/host/ofxDrawSuiteHost.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxInteract.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxLog.h>
#include <openfx/ofxSuites.h>
#include <openfx/plugin/ofxInteract.h>
#include <openfx/plugin/ofxPluginBase.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
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

// An overlay reached only through its main entry, as a host reaches one: it
// dispatches to the class's instance().
class EntryOverlay : public plugin::InteractPlugin<EntryOverlay> {
 public:
  int described = 0;

 protected:
  OfxStatus describe(plugin::Interact&) override {
    ++described;
    return kOfxStatOK;
  }
};

// What a plugin that keeps its suites in globals builds to hand a wrapper or an
// overlay: a container made on the spot, gone by the time either is used.
openfx::SuiteContainer suitesOnTheSpot() {
  openfx::SuiteContainer suites;
  suites.add(kOfxPropertySuite, 1, host::PropertySet::suite());
  suites.add(kOfxImageEffectSuite, 1, host::effectSuite());
  suites.add(kOfxParameterSuite, 1, host::paramSuite());
  suites.add(kOfxInteractSuite, 1, host::interactSuite());
  return suites;
}

// Collects what is logged while it lives, then puts back the handler the test
// run uses.
struct LogCapture {
  LogCapture() {
    openfx::Logger::setLogHandler(
        [this](openfx::Logger::Level, std::chrono::system_clock::time_point,
               const std::string& message) { messages += message + "\n"; });
  }
  ~LogCapture() {
    openfx::Logger::setLogHandler(
        std::getenv("OPENFX_TEST_LOG")
            ? openfx::Logger::LogHandler()
            : openfx::Logger::LogHandler([](openfx::Logger::Level,
                                            std::chrono::system_clock::time_point,
                                            const std::string&) {}));
  }
  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;

  std::string messages;
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

// ---------------------------------------------------------------------------
// Overlay wiring
// ---------------------------------------------------------------------------

TEST_CASE(an_overlay_without_suites_says_the_host_feature_is_missing) {
  tests::Effect effect;
  host::InteractDescriptor descriptor(effect.plugin, &HookedOverlay::mainEntry, true);
  HookedOverlay overlay;
  LogCapture log;
  CHECK(overlay.dispatch(kOfxActionDescribe, descriptor.handle(), nullptr, nullptr) ==
        kOfxStatErrMissingHostFeature);
  CHECK(overlay.ran.empty());
  CHECK(log.messages.find("setSuites") != std::string::npos);
}

TEST_CASE(an_overlay_object_of_its_own_works_once_given_suites) {
  tests::Effect effect;
  host::InteractDescriptor descriptor(effect.plugin, &HookedOverlay::mainEntry, true);
  HookedOverlay overlay;
  overlay.setSuites(suitesOnTheSpot());
  CHECK(overlay.dispatch(kOfxActionDescribe, descriptor.handle(), nullptr, nullptr) ==
        kOfxStatOK);
  CHECK(overlay.ran == std::vector<std::string>{"describe"});
}

// A plugin that puts mainEntry on its descriptor itself, rather than what
// entryPoint() returns, gives instance() the suites.
TEST_CASE(an_overlay_registered_by_its_main_entry_takes_suites_on_its_instance) {
  tests::Effect effect;
  OfxPluginEntryPoint* entry = &EntryOverlay::mainEntry;
  host::InteractDescriptor descriptor(effect.plugin, entry, true);
  CHECK(descriptor.describe() == kOfxStatErrMissingHostFeature);
  EntryOverlay::instance().setSuites(suitesOnTheSpot());
  CHECK(descriptor.describe() == kOfxStatOK);
  CHECK(EntryOverlay::instance().described == 1);
}

// ---------------------------------------------------------------------------
// Wrappers built from a container made on the spot
// ---------------------------------------------------------------------------
//
// Each wrapper here is built from a temporary container, which is gone by the
// next line, so whatever the wrapper does after that must go through suite
// pointers of its own.

TEST_CASE(an_effect_wrapper_outlives_the_container_it_was_built_from) {
  tests::Effect effect;
  plugin::ImageEffect descriptor(effect.handle(), suitesOnTheSpot());
  descriptor.defineClip(kOfxImageEffectOutputClipName);
  descriptor.params().defineDouble("scale").setDefaultValue<double>(2.0);
  CHECK(effect.contextDescriptor->clips().size() == 1);

  tests::Instance instance(*effect.contextDescriptor);
  plugin::ImageEffect wrapped(instance.handle(), suitesOnTheSpot());
  CHECK(wrapped.clip(kOfxImageEffectOutputClipName).handle() != nullptr);
  CHECK(wrapped.params().get<plugin::DoubleParam>("scale").getValue() == 2.0);
  CHECK(wrapped.imageMemory(64).data() != nullptr);
}

TEST_CASE(a_param_set_outlives_the_container_it_was_built_from) {
  tests::Effect effect;
  plugin::ParamSet params(effect.handle(), suitesOnTheSpot());
  params.defineDouble("scale").setDefaultValue<double>(3.0);

  tests::Instance instance(*effect.contextDescriptor);
  plugin::ParamSet onInstance(instance.handle(), suitesOnTheSpot());
  CHECK(onInstance.get<plugin::DoubleParam>("scale").getValue() == 3.0);
}

TEST_CASE(an_interact_outlives_the_container_it_was_built_from) {
  tests::Effect effect;
  tests::Instance instance(*effect.contextDescriptor);
  host::InteractDescriptor descriptor(effect.plugin, &HookedOverlay::mainEntry, true);
  host::InteractInstance overlay(descriptor, instance);
  plugin::Interact interact(overlay.handle(), suitesOnTheSpot());
  CHECK(interact.effectHandle() == instance.handle());
  CHECK(interact.effect().handle() == instance.handle());
}

// A plugin with its suites in globals needs no container at all.
TEST_CASE(the_wrappers_take_raw_suite_pointers_in_place_of_a_container) {
  tests::Effect effect;
  plugin::ImageEffect descriptor(effect.handle(), host::effectSuite(),
                                 host::PropertySet::suite(), host::paramSuite());
  descriptor.params().defineDouble("scale").setDefaultValue<double>(4.0);

  tests::Instance instance(*effect.contextDescriptor);
  plugin::ParamSet params(instance.handle(), host::effectSuite(), host::paramSuite(),
                          host::PropertySet::suite());
  plugin::DoubleParam scale(params.handle(), "scale", host::paramSuite(),
                            host::PropertySet::suite());
  CHECK(scale.getValue() == 4.0);

  // Without the parameter suite the effect is still wrapped; only params() fails.
  plugin::ImageEffect noParams(instance.handle(), host::effectSuite(),
                               host::PropertySet::suite());
  CHECK_THROWS_AS(noParams.params(), openfx::SuiteNotFoundException);
}

// ---------------------------------------------------------------------------
// fetchSuites
// ---------------------------------------------------------------------------

namespace {

// Stand-ins for the optional suites the test host lacks: fetching a suite only
// stores its pointer.
const OfxImageEffectOpenGLRenderSuiteV1 kOpenGLRenderSuite{};
const OfxOpenCLProgramSuiteV1 kOpenCLProgramSuite{};
const OfxParametricParameterSuiteV1 kParametricParameterSuite{};
const OfxDialogSuiteV1 kDialogSuite{};

// The test host with every suite the bindings know about.
struct FullHost : tests::Host {
  FullHost() {
    suites().add(kOfxInteractSuite, 1, host::interactSuite());
    suites().add(kOfxDrawSuite, 1, host::drawSuite());
    suites().add(kOfxOpenGLRenderSuite, 1, &kOpenGLRenderSuite);
    suites().add(kOfxOpenCLProgramSuite, 1, &kOpenCLProgramSuite);
    suites().add(kOfxParametricParameterSuite, 1, &kParametricParameterSuite);
    suites().add(kOfxDialogSuite, 1, &kDialogSuite);
  }
};

// A plugin binary with a main entry of its own, as a C plugin moving to the
// bindings one action at a time has: it fetches the suites itself in Load and
// hands only Describe to dispatch().
namespace handwritten {

class Labeller : public plugin::ImageEffectPlugin {
 protected:
  OfxStatus describe(plugin::ImageEffect& effect) override {
    effect.descriptor().setLabel("Hand-written");
    return kOfxStatOK;
  }
};

OfxHost* gHost = nullptr;
Labeller gPlugin;

void setHost(OfxHost* host) { gHost = host; }

OfxStatus mainEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                    OfxPropertySetHandle outArgs) {
  if (std::strcmp(action, kOfxActionLoad) == 0)
    return plugin::ImageEffectPlugin::fetchSuites(gHost, gPlugin.suites);
  if (std::strcmp(action, kOfxActionDescribe) == 0)
    return gPlugin.dispatch(action, handle, inArgs, outArgs);
  return kOfxStatReplyDefault;
}

}  // namespace handwritten

}  // namespace

TEST_CASE(fetch_suites_serves_a_hand_written_main_entry) {
  tests::Effect effect;
  OfxPlugin entry = {kOfxImageEffectPluginApi,
                     1,
                     "org.openeffects.tests.handwritten",
                     1,
                     0,
                     handwritten::setHost,
                     handwritten::mainEntry};
  entry.setHost(effect.host.ofx());
  CHECK(entry.mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatOK);
  CHECK(entry.mainEntry(kOfxActionDescribe, effect.handle(), nullptr, nullptr) ==
        kOfxStatOK);
  CHECK(effect.contextDescriptor->props().getString(kOfxPropLabel) == "Hand-written");
}

TEST_CASE(fetch_suites_requires_the_property_effect_and_parameter_suites) {
  openfx::SuiteContainer suites;
  CHECK(plugin::ImageEffectPlugin::fetchSuites(nullptr, suites) ==
        kOfxStatErrMissingHostFeature);

  host::Host partial;
  partial.suites().add(kOfxPropertySuite, 1, host::PropertySet::suite());
  partial.suites().add(kOfxDialogSuite, 1, &kDialogSuite);
  CHECK(plugin::ImageEffectPlugin::fetchSuites(partial.ofx(), suites) ==
        kOfxStatErrMissingHostFeature);
  // What the host does have is fetched all the same.
  CHECK(suites.get<OfxPropertySuiteV1>() == host::PropertySet::suite());
  CHECK(suites.get<OfxDialogSuiteV1>() == &kDialogSuite);
  CHECK(!suites.has<OfxImageEffectSuiteV1>());
}

TEST_CASE(load_fetches_every_suite_the_host_offers) {
  FullHost full;
  Hooked hooked;
  hooked.setHost(full.ofx());
  CHECK(hooked.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr) ==
        kOfxStatReplyDefault);
  const openfx::SuiteContainer& suites = hooked.suites;
  CHECK(suites.has<OfxPropertySuiteV1>());
  CHECK(suites.has<OfxImageEffectSuiteV1>());
  CHECK(suites.has<OfxParameterSuiteV1>());
  CHECK(suites.has<OfxMemorySuiteV1>());
  CHECK(suites.has<OfxMultiThreadSuiteV1>());
  CHECK(suites.has<OfxMessageSuiteV1>());
  CHECK(suites.has<OfxMessageSuiteV2>());
  CHECK(suites.has<OfxProgressSuiteV1>());
  CHECK(suites.has<OfxProgressSuiteV2>());
  CHECK(suites.has<OfxTimeLineSuiteV1>());
  CHECK(suites.has<OfxInteractSuiteV1>());
  CHECK(suites.has<OfxDrawSuiteV1>());
  CHECK(suites.get<OfxImageEffectOpenGLRenderSuiteV1>() == &kOpenGLRenderSuite);
  CHECK(suites.get<OfxOpenCLProgramSuiteV1>() == &kOpenCLProgramSuite);
  CHECK(suites.get<OfxParametricParameterSuiteV1>() == &kParametricParameterSuite);
  CHECK(suites.get<OfxDialogSuiteV1>() == &kDialogSuite);
}
