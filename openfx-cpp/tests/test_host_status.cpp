// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// Statuses, both ways across the host side: what the drivers make of the
// status a plugin answers an action with, and what the host's suites answer a
// plugin with, each checked against what the specification says.

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <ofxTimeLine.h>
#include <openfx/host/ofxDefaultSuites.h>
#include <openfx/host/ofxDrawSuiteHost.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxInteract.h>
#include <openfx/host/ofxPlugin.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "fixture.h"
#include "harness.h"
#include "log_capture.h"

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

// kOfxStatOK with nothing written to the out-args breaks the specification,
// which has the plugin answer kOfxStatReplyDefault for that.
TEST_CASE(every_driver_that_returns_an_answer_warns_of_an_ok_with_no_answer) {
  for (const AnsweringDriver& driver : kAnsweringDrivers) {
    Filter filter(answering(driver.action, kOfxStatOK));
    tests::Instance instance(*filter.descriptor);
    instance.create();
    tests::LogCapture log(openfx::Logger::Level::Warning);
    CHECK(thrownCode([&] { driver.drive(instance); }) == kOfxStatOK);
    CHECK(log.messages.find(driver.action) != std::string::npos);
    CHECK(log.messages.find("set nothing in its out-args") != std::string::npos);
  }
}

// The out-args start at the default region, and an OK that leaves them alone
// gives that region, not zeros.
TEST_CASE(region_of_definition_starts_the_plugin_at_the_default_region) {
  OfxRectD seen{0, 0, 0, 0};
  Filter filter([&](std::string_view action, OfxPropertySetHandle,
                    OfxPropertySetHandle outArgs) {
    if (action != kOfxImageEffectActionGetRegionOfDefinition)
      return kOfxStatReplyDefault;
    props()->propGetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, &seen.x1);
    return kOfxStatOK;
  });
  tests::Instance instance(*filter.descriptor);
  instance.create();
  tests::LogCapture log;
  const OfxRectD rod = instance.regionOfDefinition(0);
  CHECK(rod.x2 > rod.x1 && rod.y2 > rod.y1);
  CHECK(seen.x1 == rod.x1 && seen.y1 == rod.y1 && seen.x2 == rod.x2 && seen.y2 == rod.y2);
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

// ---------------------------------------------------------------------------
// The property suite
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kNobodysProperty = "OrgExampleNoSuchProperty";

}  // namespace

// The specification's kOfxStatErrUnknown for a property nobody defined and it
// does not know. What the set defines, here or in its parent, and what the
// metadata knows stay writable, and host code still creates what it likes.
TEST_CASE(the_property_suite_refuses_to_write_a_property_nobody_defined) {
  host::PropertySet set("ClipDescriptor");
  const OfxPropertySetHandle h = set.handle();
  const OfxPropertySuiteV1* suite = props();
  int i = 1;
  double d = 1;
  void* p = &i;
  const char* s = "x";
  CHECK(suite->propSetInt(h, kNobodysProperty, 0, 1) == kOfxStatErrUnknown);
  CHECK(suite->propSetDouble(h, kNobodysProperty, 0, 1.0) == kOfxStatErrUnknown);
  CHECK(suite->propSetString(h, kNobodysProperty, 0, s) == kOfxStatErrUnknown);
  CHECK(suite->propSetPointer(h, kNobodysProperty, 0, p) == kOfxStatErrUnknown);
  CHECK(suite->propSetIntN(h, kNobodysProperty, 1, &i) == kOfxStatErrUnknown);
  CHECK(suite->propSetDoubleN(h, kNobodysProperty, 1, &d) == kOfxStatErrUnknown);
  CHECK(suite->propSetStringN(h, kNobodysProperty, 1, &s) == kOfxStatErrUnknown);
  CHECK(suite->propSetPointerN(h, kNobodysProperty, 1, &p) == kOfxStatErrUnknown);
  CHECK(!set.has(kNobodysProperty));

  // Not in a clip descriptor, but the metadata knows it.
  CHECK(suite->propSetDouble(h, kOfxPropTime, 0, 2.0) == kOfxStatOK);
  // Defined by the host, in the set itself and in its parent.
  set.define(kNobodysProperty, host::PropertySet::Type::Int, 1);
  CHECK(suite->propSetInt(h, kNobodysProperty, 0, 3) == kOfxStatOK);
  host::PropertySet child("ClipInstance", &set);
  CHECK(suite->propSetInt(child.handle(), kNobodysProperty, 0, 4) == kOfxStatOK);
  CHECK(child.getInt(kNobodysProperty) == 4);
  // Host code is not held to the rule.
  CHECK(child.set("OrgExampleHostsOwn", 0, 5) == kOfxStatOK);
}

TEST_CASE(the_property_suite_refuses_a_null_value_pointer) {
  host::PropertySet set("ParamsDouble1D");
  const OfxPropertySetHandle h = set.handle();
  const OfxPropertySuiteV1* suite = props();
  CHECK(suite->propGetInt(h, kOfxParamPropDigits, 0, nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->propGetDouble(h, kOfxParamPropIncrement, 0, nullptr) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propGetString(h, kOfxPropName, 0, nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->propGetPointer(h, kOfxParamPropDataPtr, 0, nullptr) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propGetIntN(h, kOfxParamPropDigits, 1, nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->propGetDoubleN(h, kOfxParamPropIncrement, 1, nullptr) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propGetStringN(h, kOfxPropName, 1, nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->propGetPointerN(h, kOfxParamPropDataPtr, 1, nullptr) ==
        kOfxStatErrBadHandle);
  CHECK(suite->propGetDimension(h, kOfxPropName, nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->propSetIntN(h, kOfxParamPropDigits, 1, nullptr) == kOfxStatErrValue);
  CHECK(suite->propSetDoubleN(h, kOfxParamPropIncrement, 1, nullptr) == kOfxStatErrValue);
  CHECK(suite->propSetStringN(h, kOfxPropName, 1, nullptr) == kOfxStatErrValue);
  CHECK(suite->propSetPointerN(h, kOfxParamPropDataPtr, 1, nullptr) == kOfxStatErrValue);
  // Nothing to read or write, so nothing to read or write through.
  CHECK(suite->propGetIntN(h, kOfxParamPropDigits, 0, nullptr) == kOfxStatOK);
  CHECK(suite->propSetIntN(h, kOfxParamPropDigits, 0, nullptr) == kOfxStatOK);
}

// ---------------------------------------------------------------------------
// The multithread, memory and timeline suites
// ---------------------------------------------------------------------------

TEST_CASE(multi_thread_called_from_a_spawned_thread_is_refused) {
  static std::atomic<OfxStatus> nested{kOfxStatOK};
  const OfxMultiThreadSuiteV1* suite = host::multiThreadSuite();
  CHECK(suite->multiThread(
            [](unsigned, unsigned, void*) {
              nested = host::multiThreadSuite()->multiThread(
                  [](unsigned, unsigned, void*) {}, 1, nullptr);
            },
            2, nullptr) == kOfxStatOK);
  CHECK(nested == kOfxStatErrExists);
}

TEST_CASE(the_default_suites_refuse_a_null_handle_or_out_pointer) {
  const OfxMultiThreadSuiteV1* threads = host::multiThreadSuite();
  CHECK(threads->multiThreadNumCPUs(nullptr) == kOfxStatErrBadHandle);
  CHECK(threads->multiThreadIndex(nullptr) == kOfxStatErrBadHandle);
  CHECK(threads->mutexCreate(nullptr, 0) == kOfxStatErrBadHandle);
  CHECK(threads->mutexDestroy(nullptr) == kOfxStatErrBadHandle);
  CHECK(threads->mutexLock(nullptr) == kOfxStatErrBadHandle);
  CHECK(threads->mutexUnLock(nullptr) == kOfxStatErrBadHandle);
  CHECK(threads->mutexTryLock(nullptr) == kOfxStatErrBadHandle);
  CHECK(host::memorySuite()->memoryAlloc(nullptr, 16, nullptr) == kOfxStatErrBadHandle);
  double first = 0;
  CHECK(host::timeLineSuite()->getTime(nullptr, nullptr) == kOfxStatErrBadHandle);
  CHECK(host::timeLineSuite()->getTimeBounds(nullptr, &first, nullptr) ==
        kOfxStatErrBadHandle);
}

// ---------------------------------------------------------------------------
// The image effect and parameter suites
// ---------------------------------------------------------------------------

TEST_CASE(the_image_effect_suite_refuses_a_null_out_pointer) {
  Filter filter;
  tests::Instance instance(*filter.descriptor);
  instance.create();
  const OfxImageEffectSuiteV1* suite = host::effectSuite();
  const OfxImageClipHandle clip = instance.clip(kOfxImageEffectOutputClipName)->handle();
  CHECK(suite->getPropertySet(instance.handle(), nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->getParamSet(instance.handle(), nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->clipGetPropertySet(clip, nullptr) == kOfxStatErrBadHandle);
  // No image is fetched that could never be released.
  CHECK(suite->clipGetImage(clip, 0, nullptr, nullptr) == kOfxStatErrBadHandle);
  CHECK(instance.fetchCount() == 0);
  CHECK(suite->imageMemoryAlloc(nullptr, 16, nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->imageMemoryLock(nullptr, nullptr) == kOfxStatErrBadHandle);
  int held = 0;
  void* locked = &held;
  CHECK(suite->imageMemoryLock(nullptr, &locked) == kOfxStatErrBadHandle);
  CHECK(locked == nullptr);
}

TEST_CASE(the_parameter_suite_refuses_a_null_out_pointer_and_an_unknown_type) {
  Filter filter;
  filter.descriptor->defineParam(kOfxParamTypeRGB, "tint");
  filter.descriptor->defineParam(kOfxParamTypeString, "caption");
  const OfxParameterSuiteV1* suite = host::paramSuite();
  const OfxParamSetHandle descriptorSet = filter.descriptor->params().handle();
  CHECK(suite->paramDefine(descriptorSet, "OfxParamTypeImaginary", "ghost", nullptr) ==
        kOfxStatErrUnknown);
  // A type the specification has, which this host does not support.
  CHECK(suite->paramDefine(descriptorSet, kOfxParamTypeBytes, "blob", nullptr) ==
        kOfxStatErrUnsupported);

  tests::Instance instance(*filter.descriptor);
  instance.create();
  const OfxParamSetHandle set = instance.params().handle();
  OfxParamHandle tint = nullptr;
  OfxParamHandle caption = nullptr;
  CHECK(suite->paramGetHandle(set, "tint", &tint, nullptr) == kOfxStatOK);
  CHECK(suite->paramGetHandle(set, "caption", &caption, nullptr) == kOfxStatOK);
  CHECK(suite->paramSetGetPropertySet(set, nullptr) == kOfxStatErrBadHandle);
  CHECK(suite->paramGetPropertySet(tint, nullptr) == kOfxStatErrBadHandle);

  // One null among the pointers, and none of them is written through.
  double r = -1;
  double b = -1;
  double* const noDouble = nullptr;
  CHECK(suite->paramGetValue(tint, &r, noDouble, &b) == kOfxStatErrBadHandle);
  CHECK(suite->paramGetValueAtTime(tint, 0.0, &r, noDouble, &b) == kOfxStatErrBadHandle);
  CHECK(r == -1);
  CHECK(b == -1);
  char** const noString = nullptr;
  CHECK(suite->paramGetValue(caption, noString) == kOfxStatErrBadHandle);
}

// ---------------------------------------------------------------------------
// The draw suite
// ---------------------------------------------------------------------------

namespace {

// A draw context that draws nothing: only the validation in front of it matters.
class NullDrawContext : public host::DrawContext {
 public:
  OfxRGBAColourF standardColour(OfxStandardColour) const override { return {}; }

 protected:
  void onSetColour(const OfxRGBAColourF&) override {}
  void onSetLineWidth(float) override {}
  void onSetLineStipple(OfxDrawLineStipplePattern) override {}
  void onDraw(OfxDrawPrimitive, const OfxPointD*, int) override {}
  void onDrawText(const char*, const OfxPointD&, int) override {}
};

}  // namespace

TEST_CASE(the_draw_suite_refuses_a_value_that_is_no_primitive) {
  NullDrawContext context;
  context.open();
  const OfxPointD points[3] = {{0, 0}, {1, 1}, {2, 0}};
  const OfxDrawSuiteV1* suite = host::drawSuite();
  // One past the last primitive, in the range the enumeration can hold.
  const auto notAPrimitive = static_cast<OfxDrawPrimitive>(kOfxDrawPrimitiveEllipse + 1);
  CHECK(suite->draw(context.handle(), notAPrimitive, points, 3) == kOfxStatErrValue);
  CHECK(suite->draw(context.handle(), kOfxDrawPrimitiveLineStrip, points, 3) ==
        kOfxStatOK);
  CHECK(suite->draw(context.handle(), kOfxDrawPrimitivePolygon, points, 2) ==
        kOfxStatErrValue);
  context.close();
}

// ---------------------------------------------------------------------------
// What a plugin throws where it should not
// ---------------------------------------------------------------------------

// A C++ plugin's thread function can throw through the C pointer; out of a
// std::thread that would terminate the host.
TEST_CASE(multi_thread_reports_a_thread_function_that_throws) {
  static std::atomic<unsigned> ran{0};
  const OfxStatus status = host::multiThreadSuite()->multiThread(
      [](unsigned index, unsigned, void*) {
        ++ran;
        if (index == 0)
          throw std::runtime_error("thread 0");
      },
      2, nullptr);
  CHECK(status == kOfxStatFailed);
  CHECK(ran == 2);
}

namespace {

// A plugin whose Unload throws what is not even a std::exception.
OfxStatus throwingUnload(const char* action, const void*, OfxPropertySetHandle,
                         OfxPropertySetHandle) {
  if (std::string_view(action) == kOfxActionUnload)
    throw 42;  // NOLINT(bugprone-std-exception-baseclass): not one, on purpose
  return kOfxStatReplyDefault;
}

}  // namespace

TEST_CASE(a_plugin_that_throws_from_unload_does_not_terminate_its_host) {
  static OfxPlugin plugin = {kOfxImageEffectPluginApi,
                             1,
                             "org.openeffects.tests.unload",
                             1,
                             0,
                             [](OfxHost*) {},
                             throwingUnload};
  tests::Host hostSide;
  {
    host::Plugin loaded(&plugin, "/stub/Unload.ofx.bundle");
    loaded.load(hostSide);
    CHECK(loaded.isLoaded());
  }  // the destructor unloads it
}

// A string parameter read on two threads: each keeps the string it was given,
// though the other thread's is a different value, and longer.
TEST_CASE(a_string_parameter_value_stays_valid_for_the_thread_that_read_it) {
  Filter filter;
  host::Param* described = filter.descriptor->defineParam(kOfxParamTypeString, "caption");
  described->props().set(kOfxParamPropAnimates, 0, 1);
  filter.descriptor->defineParam(kOfxParamTypeString, "title");
  tests::Instance instance(*filter.descriptor);
  instance.create();
  host::Param* caption = instance.params().find("caption");
  host::ParamValue value;
  value.str = "short";
  caption->setValueAtTime(0, value);
  value.str = std::string(256, 'x');
  caption->setValueAtTime(10, value);
  value.str = "the title";
  instance.params().find("title")->setValue(value);

  const OfxParameterSuiteV1* suite = host::paramSuite();
  char* here = nullptr;
  char* title = nullptr;
  CHECK(suite->paramGetValueAtTime(caption->handle(), 0.0, &here) == kOfxStatOK);
  CHECK(suite->paramGetValue(instance.params().find("title")->handle(), &title) ==
        kOfxStatOK);
  std::string there;
  std::thread other([&] {
    char* s = nullptr;
    suite->paramGetValueAtTime(caption->handle(), 10.0, &s);
    there = s ? s : "";
  });
  other.join();
  CHECK(there.size() == 256);
  CHECK(std::string(here) == "short");
  CHECK(std::string(title) == "the title");
}
