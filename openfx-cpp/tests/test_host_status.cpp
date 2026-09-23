// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// Statuses, both ways across the host side: what the drivers make of the
// status a plugin answers an action with, and what the host's suites answer a
// plugin with, each checked against what the specification says.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxProperty.h>
#include <openfx/host/ofxEffect.h>
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
