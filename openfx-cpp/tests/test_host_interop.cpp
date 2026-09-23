// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The host side used a piece at a time, next to a host's own C code: each test
// is something a host with its own suites, handles or policies must be able to
// do with the framework in a few lines. The plugin is a stub OfxPlugin that
// talks to the host through the C suites, as a plugin written in C would.

#include <ofxCore.h>
#include <ofxGPURender.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxProperty.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxInteract.h>
#include <openfx/host/ofxPlugin.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fixture.h"
#include "harness.h"

namespace host = openfx::host;

namespace {

// What the stub plugin does with an action; it answers kOfxStatReplyDefault
// to whatever the handler leaves alone.
using Handler =
    std::function<OfxStatus(std::string_view action, const void* handle,
                            OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)>;

// Everything the stub plugin was sent, and the handler a test gave it.
struct Stub {
  Handler handler;
  std::map<std::string, int, std::less<>> received;
  OfxHost* host = nullptr;

  int count(std::string_view action) const {
    auto it = received.find(action);
    return it == received.end() ? 0 : it->second;
  }
};

Stub& stub() {
  static Stub state;
  return state;
}

void stubSetHost(OfxHost* ofxHost) { stub().host = ofxHost; }

OfxStatus stubMainEntry(const char* action, const void* handle,
                        OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
  return openfx::callAtCBoundary([&] {
    ++stub().received[action];
    return stub().handler ? stub().handler(action, handle, inArgs, outArgs)
                          : kOfxStatReplyDefault;
  });
}

OfxPlugin* stubPlugin() {
  static OfxPlugin plugin = {kOfxImageEffectPluginApi,
                             1,
                             "org.openeffects.tests.interop",
                             1,
                             0,
                             stubSetHost,
                             stubMainEntry};
  return &plugin;
}

// The property suite, as a plugin fetches it.
const OfxPropertySuiteV1* props() { return host::PropertySet::suite(); }

// A filter over the stub plugin, loaded and described, with the two clips its
// DescribeInContext would have defined. Making one resets what the stub saw
// and hands it the test's handler, so anything the handler captures must be
// declared before the Filter, to outlive the Unload it is sent last.
struct Filter {
  explicit Filter(Handler handler = {})
      : plugin(stubPlugin(), "/stub/Interop.ofx.bundle") {
    stub() = Stub{std::move(handler), {}, nullptr};
    plugin.load(host);
    global = plugin.describe();
    descriptor = plugin.describeInContext(*global, kOfxImageEffectContextFilter);
    descriptor->defineClip(kOfxImageEffectSimpleSourceClipName);
    descriptor->defineClip(kOfxImageEffectOutputClipName);
  }

  tests::Host host;
  host::Plugin plugin;
  std::unique_ptr<host::EffectDescriptor> global;
  std::unique_ptr<host::EffectDescriptor> descriptor;
};

// Properties of the host's own, which no driver knows about.
constexpr const char* kRegionIsExact = "org.openeffects.tests.RegionIsExact";
constexpr const char* kTool = "org.openeffects.tests.Tool";

}  // namespace

// ---------------------------------------------------------------------------
// The arguments of every action
// ---------------------------------------------------------------------------

namespace {

// A host that gives the plugin a CUDA stream on Render, which the driver does
// not write, and reads back what the plugin said about its region of
// definition beyond the region itself, which the driver does not read.
class ExtendingInstance : public tests::Instance {
 public:
  using tests::Instance::Instance;

  int stream = 0;  // stands for the host's CUDA stream
  int regionIsExact = -1;
  std::vector<std::string> sent;

 protected:
  void beforeAction(const char* action, host::PropertySet* inArgs,
                    host::PropertySet*) override {
    sent.emplace_back(action);
    if (std::string_view(action) == kOfxImageEffectActionRender)
      inArgs->set(kOfxImageEffectPropCudaStream, 0, static_cast<void*>(&stream));
  }

  void afterAction(const char* action, host::PropertySet*, host::PropertySet* outArgs,
                   OfxStatus status) override {
    if (std::string_view(action) == kOfxImageEffectActionGetRegionOfDefinition &&
        status == kOfxStatOK)
      regionIsExact = outArgs->getInt(kRegionIsExact, 0, -1);
  }
};

}  // namespace

TEST_CASE(a_host_adds_to_and_reads_from_the_arguments_of_any_action) {
  void* streamSeen = nullptr;
  Filter filter([&](std::string_view action, const void*, OfxPropertySetHandle inArgs,
                    OfxPropertySetHandle outArgs) {
    if (action == kOfxImageEffectActionRender) {
      props()->propGetPointer(inArgs, kOfxImageEffectPropCudaStream, 0, &streamSeen);
      return kOfxStatOK;
    }
    if (action == kOfxImageEffectActionGetRegionOfDefinition) {
      const double rod[4] = {0, 0, 16, 8};
      props()->propSetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, rod);
      props()->propSetInt(outArgs, kRegionIsExact, 0, 1);
      return kOfxStatOK;
    }
    return kOfxStatReplyDefault;
  });
  ExtendingInstance instance(*filter.descriptor);
  instance.create();

  host::RenderArgs args;
  args.renderWindow = {0, 0, tests::Instance::kWidth, tests::Instance::kHeight};
  CHECK(instance.render(args) == kOfxStatOK);
  CHECK(streamSeen == &instance.stream);

  const OfxRectD rod = instance.regionOfDefinition(0);
  CHECK(rod.x2 == 16.0);
  CHECK(instance.regionIsExact == 1);

  // The drivers that send no arguments go through the hooks too.
  instance.purgeCaches();
  const std::vector<std::string> expected{
      kOfxActionCreateInstance, kOfxImageEffectActionRender,
      kOfxImageEffectActionGetRegionOfDefinition, kOfxActionPurgeCaches};
  CHECK(instance.sent == expected);
}

namespace {

// What the stub overlay was told about the tool in use.
std::string& toolSeen() {
  static std::string tool;
  return tool;
}

OfxStatus overlayMainEntry(const char* action, const void*, OfxPropertySetHandle inArgs,
                           OfxPropertySetHandle) {
  return openfx::callAtCBoundary([&] {
    if (std::string_view(action) != kOfxInteractActionPenDown)
      return kOfxStatReplyDefault;
    char* tool = nullptr;
    if (props()->propGetString(inArgs, kTool, 0, &tool) == kOfxStatOK)
      toolSeen() = tool;
    return kOfxStatOK;
  });
}

// A host that tells the overlay which of its tools the pen is.
class ToolInstance : public host::InteractInstance {
 public:
  using host::InteractInstance::InteractInstance;

  std::vector<std::pair<std::string, OfxStatus>> answered;

 protected:
  void beforeAction(const char* action, host::PropertySet* inArgs) override {
    if (std::string_view(action) == kOfxInteractActionPenDown)
      inArgs->set(kTool, 0, "brush");
  }
  void afterAction(const char* action, host::PropertySet*, OfxStatus status) override {
    answered.emplace_back(action, status);
  }
};

}  // namespace

TEST_CASE(a_host_adds_to_the_arguments_of_any_interact_action) {
  Filter filter;
  tests::Instance effect(*filter.descriptor);
  effect.create();
  host::InteractDescriptor overlay(filter.plugin, overlayMainEntry, true);
  toolSeen().clear();
  {
    ToolInstance instance(overlay, effect);
    instance.create();
    CHECK(instance.penDown({1, 1}, {1, 1}, 1.0) == kOfxStatOK);
    CHECK(toolSeen() == "brush");
    CHECK(instance.gainFocus() == kOfxStatReplyDefault);
    CHECK(instance.answered.size() == 3);
    CHECK(instance.answered[1].first == kOfxInteractActionPenDown);
    CHECK(instance.answered[1].second == kOfxStatOK);
  }
}
