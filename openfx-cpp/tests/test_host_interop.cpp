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
#include <openfx/ofxMisc.h>
#include <openfx/ofxPixels.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
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

// ---------------------------------------------------------------------------
// A host's own timeline
// ---------------------------------------------------------------------------

namespace {

// An effect that keeps its own time, as one per viewer would.
class TimedInstance : public tests::Instance {
 public:
  using tests::Instance::Instance;

  OfxTime now = 0;
  OfxTime currentTime() const override { return now; }
};

host::ParamValue doubleValue(double d) {
  host::ParamValue value;
  value.doubles = {d};
  return value;
}

}  // namespace

TEST_CASE(a_parameter_is_read_and_set_at_its_effects_own_current_time) {
  Filter filter;
  filter.descriptor->defineParam(kOfxParamTypeDouble, "scale");
  TimedInstance instance(*filter.descriptor);
  instance.create();
  host::Param* scale = instance.params().find("scale");
  scale->setValueAtTime(0, doubleValue(1.0));
  scale->setValueAtTime(10, doubleValue(3.0));
  host::timeline().current = 0;  // the default timeline is somewhere else
  instance.now = 5;

  // The plugin's way to the parameter: the effect's set, then the handle.
  OfxParamSetHandle paramSet = nullptr;
  OfxParamHandle param = nullptr;
  CHECK(host::effectSuite()->getParamSet(instance.handle(), &paramSet) == kOfxStatOK);
  const OfxParameterSuiteV1* suite = host::paramSuite();
  CHECK(suite->paramGetHandle(paramSet, "scale", &param, nullptr) == kOfxStatOK);

  double value = 0;
  CHECK(suite->paramGetValue(param, &value) == kOfxStatOK);
  CHECK(value == 2.0);
  // Setting a keyed parameter's value keys it at the effect's time too.
  CHECK(suite->paramSetValue(param, 7.0) == kOfxStatOK);
  CHECK(scale->numKeys() == 3);
  CHECK(scale->value(5).doubles[0] == 7.0);
  CHECK(scale->value(0).doubles[0] == 1.0);
}

// ---------------------------------------------------------------------------
// A host's own parameters
// ---------------------------------------------------------------------------

namespace {

// Stands for a host's own parameter store, which its own parameter suite
// understands and the framework's never sees.
struct HostParameters {
  double gain = 1.0;
};

class OwnParametersInstance : public tests::Instance {
 public:
  using tests::Instance::Instance;

  HostParameters own;
  OfxParamSetHandle paramSetHandle() override {
    return reinterpret_cast<OfxParamSetHandle>(&own);
  }
};

}  // namespace

TEST_CASE(a_host_hands_the_plugin_its_own_parameter_set) {
  Filter filter;
  OwnParametersInstance instance(*filter.descriptor);
  instance.create();
  OfxParamSetHandle paramSet = nullptr;
  CHECK(host::effectSuite()->getParamSet(instance.handle(), &paramSet) == kOfxStatOK);
  CHECK(paramSet == reinterpret_cast<OfxParamSetHandle>(&instance.own));
  // Describing stays the framework's.
  CHECK(host::effectSuite()->getParamSet(filter.descriptor->handle(), &paramSet) ==
        kOfxStatOK);
  CHECK(paramSet == filter.descriptor->params().handle());
}

// ---------------------------------------------------------------------------
// A host's own OfxHost, and actions sent without the drivers
// ---------------------------------------------------------------------------

namespace {

// The fetchSuite of a host that built its OfxHost itself, with the one suite
// it offers.
const void* ownFetchSuite(OfxPropertySetHandle, const char* name, int version) {
  return std::string_view(name) == kOfxPropertySuite && version == 1
             ? host::PropertySet::suite()
             : nullptr;
}

}  // namespace

TEST_CASE(a_plugin_loads_against_a_hosts_own_ofxhost) {
  host::PropertySet hostProps("ImageEffectHost");
  OfxHost ofxHost{hostProps.handle(), ownFetchSuite};
  stub() = Stub{};
  {
    host::Plugin plugin(stubPlugin(), "/stub/Interop.ofx.bundle");
    plugin.load(&ofxHost);
    CHECK(plugin.isLoaded());
    CHECK(stub().host == &ofxHost);
    CHECK(stub().count(kOfxActionLoad) == 1);
    plugin.load(&ofxHost);  // once
    CHECK(stub().count(kOfxActionLoad) == 1);
  }
  CHECK(stub().count(kOfxActionUnload) == 1);
}

TEST_CASE(an_instance_is_destroyed_once_whichever_way_the_host_sends_it) {
  Filter filter;
  {
    tests::Instance instance(*filter.descriptor);
    instance.create();
    CHECK(instance.action(kOfxActionDestroyInstance, nullptr, nullptr) ==
          kOfxStatReplyDefault);
  }
  CHECK(stub().count(kOfxActionDestroyInstance) ==
        1);  // not a second from the destructor

  // Created without the driver, it is still destroyed with the instance.
  stub().received.clear();
  {
    tests::Instance instance(*filter.descriptor);
    CHECK(instance.action(kOfxActionCreateInstance, nullptr, nullptr) ==
          kOfxStatReplyDefault);
  }
  CHECK(stub().count(kOfxActionDestroyInstance) == 1);
}

// ---------------------------------------------------------------------------
// Clip preferences the host can refuse
// ---------------------------------------------------------------------------

TEST_CASE(a_host_refuses_a_clip_preference_before_it_is_applied) {
  const std::string outputDepth =
      openfx::clipPrefDepthProp(kOfxImageEffectOutputClipName);
  const std::string outputPAR = openfx::clipPrefPARProp(kOfxImageEffectOutputClipName);
  // The plugin wants a byte output at a pixel aspect ratio of 2 from a float
  // input, varies per frame, and says one thing more, which only the host knows.
  Filter filter([&](std::string_view action, const void*, OfxPropertySetHandle,
                    OfxPropertySetHandle outArgs) {
    if (action != kOfxImageEffectActionGetClipPreferences)
      return kOfxStatReplyDefault;
    props()->propSetString(outArgs, outputDepth.c_str(), 0, kOfxBitDepthByte);
    props()->propSetDouble(outArgs, outputPAR.c_str(), 0, 2.0);
    props()->propSetInt(outArgs, kOfxImageEffectFrameVarying, 0, 1);
    props()->propSetInt(outArgs, kRegionIsExact, 0, 1);
    return kOfxStatOK;
  });
  tests::Instance instance(*filter.descriptor);
  instance.create();
  host::Clip* output = instance.clip(kOfxImageEffectOutputClipName);

  std::optional<host::ClipPreferences> answer = instance.queryClipPreferences();
  CHECK(answer.has_value());
  if (!answer)
    return;
  // Asking applies nothing.
  CHECK(output->depth() == openfx::PixelDepth::Float);
  CHECK(output->props().getDouble(kOfxImagePropPixelAspectRatio) == 1.0);
  CHECK(!instance.frameVarying());
  CHECK(answer->clips.at(kOfxImageEffectOutputClipName).depth ==
        openfx::PixelDepth::Byte);
  CHECK(answer->clips.at(kOfxImageEffectOutputClipName).pixelAspectRatio == 2.0);
  CHECK(answer->clips.at(kOfxImageEffectSimpleSourceClipName).depth ==
        openfx::PixelDepth::Float);
  CHECK(!answer->premultiplication.has_value());
  CHECK(answer->frameVarying);
  CHECK(answer->outArgs.getInt(kRegionIsExact) == 1);

  // A host that cannot mix depths keeps the output at the input's, and takes
  // the rest of the answer.
  answer->clips.at(kOfxImageEffectOutputClipName).depth = openfx::PixelDepth::Float;
  CHECK(instance.applyClipPreferences(*answer));
  CHECK(output->depth() == openfx::PixelDepth::Float);
  CHECK(output->props().getDouble(kOfxImagePropPixelAspectRatio) == 2.0);
  CHECK(instance.frameVarying());
  CHECK(!instance.applyClipPreferences(*answer));  // nothing left to change
}

// ---------------------------------------------------------------------------
// The region of definition
// ---------------------------------------------------------------------------

TEST_CASE(the_region_of_definition_carries_the_render_scale_and_does_not_recurse) {
  double scaleSeen[2] = {0, 0};
  OfxStatus outputAnswer = kOfxStatOK;
  int nesting = 0;  // how deep inside its own GetRegionOfDefinition the plugin is
  // A plugin that asks for its output clip's region from inside the action
  // that defines it. It stops after a few, so that a host that asks it again
  // each time fails the test rather than overflowing the stack.
  Filter filter([&](std::string_view action, const void* handle,
                    OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    if (action != kOfxImageEffectActionGetRegionOfDefinition)
      return kOfxStatReplyDefault;
    props()->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, scaleSeen);
    if (++nesting < 4) {
      const OfxImageEffectSuiteV1* effects = host::effectSuite();
      OfxImageClipHandle output = nullptr;
      effects->clipGetHandle(static_cast<OfxImageEffectHandle>(const_cast<void*>(handle)),
                             kOfxImageEffectOutputClipName, &output, nullptr);
      OfxRectD bounds{0, 0, 0, 0};
      outputAnswer = effects->clipGetRegionOfDefinition(output, 0, &bounds);
    }
    --nesting;
    const double rod[4] = {0, 0, 16, 8};
    props()->propSetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, rod);
    return kOfxStatOK;
  });
  tests::Instance instance(*filter.descriptor);
  instance.create();

  const OfxRectD rod = instance.regionOfDefinition(0, {0.5, 0.25});
  CHECK(rod.x2 == 16.0);
  CHECK(scaleSeen[0] == 0.5);
  CHECK(scaleSeen[1] == 0.25);
  CHECK(stub().count(kOfxImageEffectActionGetRegionOfDefinition) == 1);
  CHECK(outputAnswer == kOfxStatFailed);
  CHECK(!instance.regionOfDefinitionInFlight());

  // Asked from outside the action, the output's region is the effect's.
  OfxRectD bounds{0, 0, 0, 0};
  CHECK(host::effectSuite()->clipGetRegionOfDefinition(
            instance.clip(kOfxImageEffectOutputClipName)->handle(), 0, &bounds) ==
        kOfxStatOK);
  CHECK(bounds.y2 == 8.0);
}
