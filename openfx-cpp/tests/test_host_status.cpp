// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// Statuses, both ways across the host side: what the drivers make of the
// status a plugin answers an action with, and what the host's suites answer a
// plugin with, each checked against what the specification says.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxInteract.h>
#include <openfx/host/ofxPlugin.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>

#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "fixture.h"
#include "harness.h"

namespace host = openfx::host;

namespace {

// What the stub plugin answers each action with; kOfxStatReplyDefault for
// whatever the handler leaves alone.
using Handler = std::function<OfxStatus(
    std::string_view action, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)>;

Handler& handler() {
  static Handler current;
  return current;
}

OfxStatus stubMainEntry(const char* action, const void*, OfxPropertySetHandle inArgs,
                        OfxPropertySetHandle outArgs) {
  return openfx::callAtCBoundary([&] {
    return handler() ? handler()(action, inArgs, outArgs) : kOfxStatReplyDefault;
  });
}

OfxPlugin* stubPlugin() {
  static OfxPlugin plugin = {kOfxImageEffectPluginApi,
                             1,
                             "org.openeffects.tests.status",
                             1,
                             0,
                             [](OfxHost*) {},
                             stubMainEntry};
  return &plugin;
}

const OfxPropertySuiteV1* props() { return host::PropertySet::suite(); }

// A filter over the stub plugin, loaded and described, with a source and an
// output clip. The handler is installed after Load and the two describe
// actions, and taken away again with the Filter.
struct Filter {
  explicit Filter(Handler answer = {}) : plugin(stubPlugin(), "/stub/Status.ofx.bundle") {
    handler() = {};
    plugin.load(host);
    global = plugin.describe();
    descriptor = plugin.describeInContext(*global, kOfxImageEffectContextFilter);
    descriptor->defineClip(kOfxImageEffectSimpleSourceClipName);
    descriptor->defineClip(kOfxImageEffectOutputClipName);
    handler() = std::move(answer);
  }
  ~Filter() { handler() = {}; }

  Filter(const Filter&) = delete;
  Filter& operator=(const Filter&) = delete;

  tests::Host host;
  host::Plugin plugin;
  std::unique_ptr<host::EffectDescriptor> global;
  std::unique_ptr<host::EffectDescriptor> descriptor;
};

// A plugin that answers one action with one status and leaves the rest.
Handler answering(std::string_view which, OfxStatus status) {
  return [which, status](std::string_view action, OfxPropertySetHandle,
                         OfxPropertySetHandle) {
    return action == which ? status : kOfxStatReplyDefault;
  };
}

constexpr OfxRectI kWindow{0, 0, tests::Instance::kWidth, tests::Instance::kHeight};

// The code of the openfx::OfxException f throws, or kOfxStatOK if it throws none.
template <class F>
OfxStatus thrownCode(F&& f) {
  try {
    std::forward<F>(f)();
  } catch (const openfx::OfxException& e) {
    return e.code();
  }
  return kOfxStatOK;
}

// Each driver that returns an answer, and the action it sends.
struct AnsweringDriver {
  const char* action;
  void (*drive)(host::EffectInstance&);
};

const AnsweringDriver kAnsweringDrivers[] = {
    {kOfxImageEffectActionGetRegionOfDefinition,
     [](host::EffectInstance& i) { i.regionOfDefinition(0); }},
    {kOfxImageEffectActionGetRegionsOfInterest,
     [](host::EffectInstance& i) { i.getRegionsOfInterest(0, {0, 0, 8, 4}, {1, 1}); }},
    {kOfxImageEffectActionGetFramesNeeded,
     [](host::EffectInstance& i) { i.getFramesNeeded(0); }},
    {kOfxImageEffectActionGetTimeDomain,
     [](host::EffectInstance& i) { i.getTimeDomain(); }},
    {kOfxImageEffectActionGetOutputColourspace,
     [](host::EffectInstance& i) { i.getOutputColourspace({"ofx_scene_linear"}); }},
    {kOfxImageEffectActionGetClipPreferences,
     [](host::EffectInstance& i) { i.queryClipPreferences(); }},
};

}  // namespace

// ---------------------------------------------------------------------------
// IsIdentity
// ---------------------------------------------------------------------------

TEST_CASE(is_identity_returns_the_clip_and_the_time_the_plugin_slipped_to) {
  Filter filter([](std::string_view action, OfxPropertySetHandle inArgs,
                   OfxPropertySetHandle outArgs) {
    if (action != kOfxImageEffectActionIsIdentity)
      return kOfxStatReplyDefault;
    double time = 0;
    props()->propGetDouble(inArgs, kOfxPropTime, 0, &time);
    props()->propSetString(outArgs, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
    props()->propSetDouble(outArgs, kOfxPropTime, 0, time - 1);
    return kOfxStatOK;
  });
  tests::Instance instance(*filter.descriptor);
  instance.create();

  const host::Identity identity =
      instance.isIdentity(5, kWindow, {1.0, 1.0}, kOfxImageFieldNone);
  CHECK(identity.status == kOfxStatOK);
  CHECK(identity.isIdentity());
  CHECK(identity.clip == kOfxImageEffectSimpleSourceClipName);
  CHECK(identity.time == 4.0);
}

TEST_CASE(is_identity_keeps_the_time_asked_about_unless_the_plugin_changes_it) {
  Filter filter([](std::string_view action, OfxPropertySetHandle,
                   OfxPropertySetHandle outArgs) {
    if (action != kOfxImageEffectActionIsIdentity)
      return kOfxStatReplyDefault;
    props()->propSetString(outArgs, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
    return kOfxStatOK;
  });
  tests::Instance instance(*filter.descriptor);
  instance.create();
  const host::Identity identity =
      instance.isIdentity(5, kWindow, {1.0, 1.0}, kOfxImageFieldNone);
  CHECK(identity.isIdentity());
  CHECK(identity.time == 5.0);
}

// An error is not "not identity": the host must not go on to render.
TEST_CASE(is_identity_returns_the_error_a_plugin_reports) {
  Filter filter(answering(kOfxImageEffectActionIsIdentity, kOfxStatErrMemory));
  tests::Instance instance(*filter.descriptor);
  instance.create();
  const host::Identity identity =
      instance.isIdentity(0, kWindow, {1.0, 1.0}, kOfxImageFieldNone);
  CHECK(identity.status == kOfxStatErrMemory);
  CHECK(!identity.isIdentity());
}

// ---------------------------------------------------------------------------
// The drivers' status rule
// ---------------------------------------------------------------------------

// kOfxStatFailed or kOfxStatErrMemory is an error the plugin reported, which
// must not look like kOfxStatReplyDefault, "use the default".
TEST_CASE(every_driver_that_returns_an_answer_throws_the_error_a_plugin_reports) {
  for (const AnsweringDriver& driver : kAnsweringDrivers) {
    for (const OfxStatus status : {kOfxStatFailed, kOfxStatErrMemory, kOfxStatErrFatal}) {
      Filter filter(answering(driver.action, status));
      tests::Instance instance(*filter.descriptor);
      instance.create();
      CHECK(thrownCode([&] { driver.drive(instance); }) == status);
    }
    Filter filter(answering(driver.action, kOfxStatReplyDefault));
    tests::Instance instance(*filter.descriptor);
    instance.create();
    CHECK(thrownCode([&] { driver.drive(instance); }) == kOfxStatOK);
  }
}

TEST_CASE(the_drivers_of_actions_without_an_answer_return_the_status) {
  Filter filter([](std::string_view action, OfxPropertySetHandle, OfxPropertySetHandle) {
    if (action == kOfxActionPurgeCaches)
      return kOfxStatFailed;
    if (action == kOfxActionSyncPrivateData)
      return kOfxStatErrMemory;
    if (action == kOfxActionBeginInstanceEdit)
      return kOfxStatOK;
    if (action == kOfxActionEndInstanceEdit)
      return kOfxStatErrFatal;
    return kOfxStatReplyDefault;
  });
  tests::Instance instance(*filter.descriptor);
  instance.create();
  CHECK(instance.purgeCaches() == kOfxStatFailed);
  CHECK(instance.syncPrivateData() == kOfxStatErrMemory);
  CHECK(instance.beginInstanceEdit() == kOfxStatOK);
  CHECK(instance.endInstanceEdit() == kOfxStatErrFatal);
}

// All three actions go out whatever each answers, so the plugin's brackets
// stay balanced; the host hears the first failure, else InstanceChanged.
TEST_CASE(a_parameter_change_returns_its_first_failure_else_instance_changed) {
  int sent = 0;
  OfxStatus begin = kOfxStatReplyDefault;
  OfxStatus end = kOfxStatReplyDefault;
  Filter filter([&](std::string_view action, OfxPropertySetHandle, OfxPropertySetHandle) {
    if (action == kOfxActionBeginInstanceChanged) {
      ++sent;
      return begin;
    }
    if (action == kOfxActionEndInstanceChanged) {
      ++sent;
      return end;
    }
    if (action == kOfxActionInstanceChanged) {
      ++sent;
      return kOfxStatOK;
    }
    return kOfxStatReplyDefault;
  });
  filter.descriptor->defineParam(kOfxParamTypeDouble, "gain");
  tests::Instance instance(*filter.descriptor);
  instance.create();
  host::Param& gain = *instance.params().find("gain");

  CHECK(instance.paramChanged(gain, kOfxChangeUserEdited, 0, {1, 1}) == kOfxStatOK);
  CHECK(sent == 3);
  end = kOfxStatErrMemory;
  CHECK(instance.paramChanged(gain, kOfxChangeUserEdited, 0, {1, 1}) ==
        kOfxStatErrMemory);
  begin = kOfxStatFailed;
  CHECK(instance.paramChanged(gain, kOfxChangeUserEdited, 0, {1, 1}) == kOfxStatFailed);
  CHECK(sent == 9);
}

TEST_CASE(a_failed_load_describe_or_create_throws_the_plugins_status) {
  {
    tests::Host hostSide;
    host::Plugin plugin(stubPlugin(), "/stub/Status.ofx.bundle");
    handler() = answering(kOfxActionLoad, kOfxStatErrFatal);
    CHECK(thrownCode([&] { plugin.load(hostSide); }) == kOfxStatErrFatal);
    CHECK(!plugin.isLoaded());

    handler() = answering(kOfxActionDescribe, kOfxStatErrMemory);
    plugin.load(hostSide);
    CHECK(thrownCode([&] { plugin.describe(); }) == kOfxStatErrMemory);

    handler() = answering(kOfxImageEffectActionDescribeInContext, kOfxStatFailed);
    const std::unique_ptr<host::EffectDescriptor> global = plugin.describe();
    CHECK(thrownCode([&] {
            plugin.describeInContext(*global, kOfxImageEffectContextFilter);
          }) == kOfxStatFailed);
    handler() = {};
  }
  Filter filter(answering(kOfxActionCreateInstance, kOfxStatErrMemory));
  tests::Instance instance(*filter.descriptor);
  CHECK(thrownCode([&] { instance.create(); }) == kOfxStatErrMemory);
}

namespace {

OfxStatus failingOverlay(const char* action, const void*, OfxPropertySetHandle,
                         OfxPropertySetHandle) {
  return std::string_view(action) == kOfxActionDescribe ? kOfxStatFailed
                                                        : kOfxStatReplyDefault;
}

}  // namespace

// No overlay is null; an overlay that fails to describe is an error.
TEST_CASE(describe_overlay_throws_the_status_of_a_failed_describe) {
  Filter filter;
  CHECK(host::describeOverlay(filter.plugin, *filter.descriptor) == nullptr);
  filter.descriptor->props().set(kOfxImageEffectPluginPropOverlayInteractV2, 0,
                                 reinterpret_cast<void*>(&failingOverlay));
  CHECK(thrownCode([&] { host::describeOverlay(filter.plugin, *filter.descriptor); }) ==
        kOfxStatFailed);
}
