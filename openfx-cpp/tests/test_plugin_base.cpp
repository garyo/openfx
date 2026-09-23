// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// openfx::plugin::ImageEffectPlugin: which virtual each action reaches, what
// its typed arguments carry, what an exception becomes, and the OfxPlugin
// struct PluginEntry builds.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/plugin/ofxPluginBase.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "fixture.h"
#include "harness.h"

namespace plugin = openfx::plugin;
namespace host = openfx::host;

namespace {

// A plugin that records which virtual ran and what it was handed.
class Recorder : public plugin::ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.tests.recorder";

  std::vector<std::string> ran;
  std::string context;
  std::string changedParam;
  double time = 0;
  OfxRectI renderWindow{0, 0, 0, 0};
  OfxImageEffectHandle lastHandle = nullptr;
  void* dialogUserData = nullptr;
  bool sawOutArgs = false;

  bool didRun(const std::string& action) const {
    return std::find(ran.begin(), ran.end(), action) != ran.end();
  }

 protected:
  OfxStatus load() override { return record("load"); }
  OfxStatus unload() override { return record("unload"); }
  OfxStatus describe(plugin::ImageEffect& effect) override {
    lastHandle = effect.handle();
    return record("describe");
  }
  OfxStatus describeInContext(plugin::ImageEffect&, std::string_view which) override {
    context = which;
    return record("describeInContext");
  }
  OfxStatus createInstance(plugin::ImageEffect&) override {
    return record("createInstance");
  }
  OfxStatus destroyInstance(plugin::ImageEffect&) override {
    return record("destroyInstance");
  }
  OfxStatus instanceChanged(plugin::ImageEffect&, plugin::ActionArgs& in) override {
    const auto args = in.as<plugin::propsets::ActionInstanceChanged_InArgs>();
    changedParam = args.name();
    time = args.time();
    return record("instanceChanged");
  }
  OfxStatus beginInstanceChanged(plugin::ImageEffect&, plugin::ActionArgs&) override {
    return record("beginInstanceChanged");
  }
  OfxStatus endInstanceChanged(plugin::ImageEffect&, plugin::ActionArgs&) override {
    return record("endInstanceChanged");
  }
  OfxStatus purgeCaches(plugin::ImageEffect&) override { return record("purgeCaches"); }
  OfxStatus syncPrivateData(plugin::ImageEffect&) override {
    return record("syncPrivateData");
  }
  OfxStatus beginInstanceEdit(plugin::ImageEffect&) override {
    return record("beginInstanceEdit");
  }
  OfxStatus endInstanceEdit(plugin::ImageEffect&) override {
    return record("endInstanceEdit");
  }
  OfxStatus getRegionOfDefinition(plugin::ImageEffect&, plugin::ActionArgs& in,
                                  plugin::ActionArgs& out) override {
    time =
        in.as<plugin::propsets::ImageEffectActionGetRegionOfDefinition_InArgs>().time();
    sawOutArgs = !out.empty();
    return record("getRegionOfDefinition");
  }
  OfxStatus getRegionsOfInterest(plugin::ImageEffect&, plugin::ActionArgs&,
                                 plugin::ActionArgs&) override {
    return record("getRegionsOfInterest");
  }
  OfxStatus getFramesNeeded(plugin::ImageEffect&, plugin::ActionArgs&,
                            plugin::ActionArgs&) override {
    return record("getFramesNeeded");
  }
  OfxStatus getClipPreferences(plugin::ImageEffect&, plugin::ActionArgs& out) override {
    sawOutArgs = !out.empty();
    return record("getClipPreferences");
  }
  OfxStatus isIdentity(plugin::ImageEffect&, plugin::ActionArgs&,
                       plugin::ActionArgs&) override {
    return record("isIdentity");
  }
  OfxStatus render(plugin::ImageEffect& effect, plugin::ActionArgs& in) override {
    const auto args = in.as<plugin::propsets::ImageEffectActionRender_InArgs>();
    time = args.time();
    renderWindow = openfx::toOfxRectI(args.renderWindow());
    lastHandle = effect.handle();
    return record("render");
  }
  OfxStatus beginSequenceRender(plugin::ImageEffect&, plugin::ActionArgs&) override {
    return record("beginSequenceRender");
  }
  OfxStatus endSequenceRender(plugin::ImageEffect&, plugin::ActionArgs&) override {
    return record("endSequenceRender");
  }
  OfxStatus getTimeDomain(plugin::ImageEffect&, plugin::ActionArgs&) override {
    return record("getTimeDomain");
  }
  OfxStatus getOutputColourspace(plugin::ImageEffect&, plugin::ActionArgs&,
                                 plugin::ActionArgs&) override {
    return record("getOutputColourspace");
  }
  OfxStatus dialog(void* userData) override {
    dialogUserData = userData;
    return record("dialog");
  }

 private:
  OfxStatus record(const char* action) {
    ran.emplace_back(action);
    return kOfxStatOK;
  }
};

// A plugin whose render throws, to show what a thrown exception becomes.
class Thrower : public plugin::ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.tests.thrower";
  enum class What { OfxError, Standard, Other };
  What what = What::OfxError;

 protected:
  OfxStatus render(plugin::ImageEffect&, plugin::ActionArgs&) override {
    switch (what) {
      case What::OfxError:
        throw openfx::OfxException(kOfxStatErrImageFormat, "render");
      case What::Standard:
        throw std::runtime_error("render");
      case What::Other:
        // Deliberately not a std::exception: dispatch has a catch-all too.
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw 42;
    }
    return kOfxStatOK;
  }
};

// A plugin that overrides nothing at all.
class Silent : public plugin::ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.tests.silent";
};

// The plugin's image effect suite with a counter around getPropertySet, which
// is the call that turns an action's handle into an effect. Installing it over
// the real suite in the plugin's own container needs no undoing: it forwards
// every call, and it outlives the plugin it was installed in.
struct PropertySetSpy {
  explicit PropertySetSpy(openfx::SuiteContainer& suites) {
    real = suites.get<OfxImageEffectSuiteV1>();
    spy = *real;
    spy.getPropertySet = count;
    calls = 0;
    suites.add(kOfxImageEffectSuite, 1, &spy);
  }

  static OfxStatus count(OfxImageEffectHandle effect, OfxPropertySetHandle* out) {
    ++calls;
    return real->getPropertySet(effect, out);
  }

  static inline int calls = 0;
  static inline const OfxImageEffectSuiteV1* real = nullptr;
  static inline OfxImageEffectSuiteV1 spy{};
};

// A plugin loaded against the test host, ready to be handed actions.
template <class PluginT>
struct Loaded {
  Loaded() {
    plugin.setHost(effect.host.ofx());
    loadStatus = plugin.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr);
  }

  OfxStatus act(const char* action, host::PropertySet* in = nullptr,
                host::PropertySet* out = nullptr) {
    return plugin.dispatch(action, effect.handle(), in ? in->handle() : nullptr,
                           out ? out->handle() : nullptr);
  }

  tests::Effect effect;
  PluginT plugin;
  OfxStatus loadStatus = kOfxStatFailed;
};

}  // namespace

TEST_CASE(dispatch_fetches_the_suites_on_load) {
  Loaded<Recorder> loaded;
  CHECK(loaded.loadStatus == kOfxStatOK);
  CHECK(loaded.plugin.didRun("load"));
  CHECK(loaded.plugin.host() == loaded.effect.host.ofx());
  CHECK(loaded.plugin.suites.has<OfxPropertySuiteV1>());
  CHECK(loaded.plugin.suites.has<OfxImageEffectSuiteV1>());
  CHECK(loaded.plugin.suites.has<OfxParameterSuiteV1>());
  CHECK(loaded.plugin.suites.has<OfxTimeLineSuiteV1>());
  CHECK(loaded.plugin.dispatch(kOfxActionUnload, nullptr, nullptr, nullptr) ==
        kOfxStatOK);
  CHECK(loaded.plugin.didRun("unload"));
}

TEST_CASE(dispatch_without_the_suites_it_needs_fails_to_load) {
  Recorder plugin;
  CHECK(plugin.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr) ==
        kOfxStatErrMissingHostFeature);  // no host at all
  CHECK(!plugin.didRun("load"));

  host::Host bare;  // a host with no suites to offer
  plugin.setHost(bare.ofx());
  CHECK(plugin.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr) ==
        kOfxStatErrMissingHostFeature);
  CHECK(!plugin.didRun("load"));
}

TEST_CASE(dispatch_reaches_the_describe_actions) {
  Loaded<Recorder> loaded;
  CHECK(loaded.act(kOfxActionDescribe) == kOfxStatOK);
  CHECK(loaded.plugin.didRun("describe"));
  CHECK(loaded.plugin.lastHandle == loaded.effect.handle());

  host::PropertySet in =
      host::PropertySet::forAction(kOfxImageEffectActionDescribeInContext, "inArgs");
  in.set(kOfxImageEffectPropContext, 0, kOfxImageEffectContextGeneral);
  CHECK(loaded.act(kOfxImageEffectActionDescribeInContext, &in) == kOfxStatOK);
  CHECK(loaded.plugin.context == kOfxImageEffectContextGeneral);
}

TEST_CASE(dispatch_reaches_the_instance_lifecycle_actions) {
  Loaded<Recorder> loaded;
  CHECK(loaded.act(kOfxActionCreateInstance) == kOfxStatOK);
  CHECK(loaded.act(kOfxActionDestroyInstance) == kOfxStatOK);
  CHECK(loaded.act(kOfxActionPurgeCaches) == kOfxStatOK);
  CHECK(loaded.act(kOfxActionSyncPrivateData) == kOfxStatOK);
  CHECK(loaded.act(kOfxActionBeginInstanceEdit) == kOfxStatOK);
  CHECK(loaded.act(kOfxActionEndInstanceEdit) == kOfxStatOK);
  CHECK(loaded.plugin.didRun("createInstance"));
  CHECK(loaded.plugin.didRun("destroyInstance"));
  CHECK(loaded.plugin.didRun("purgeCaches"));
  CHECK(loaded.plugin.didRun("syncPrivateData"));
  CHECK(loaded.plugin.didRun("beginInstanceEdit"));
  CHECK(loaded.plugin.didRun("endInstanceEdit"));
}

TEST_CASE(dispatch_reaches_the_instance_changed_actions_with_their_arguments) {
  Loaded<Recorder> loaded;
  host::PropertySet in =
      host::PropertySet::forAction(kOfxActionInstanceChanged, "inArgs");
  host::propsets::ActionInstanceChanged_InArgs args(in.handle(),
                                                    host::PropertySet::suite());
  args.setType(kOfxTypeParameter)
      .setName("scale")
      .setChangeReason(kOfxChangeUserEdited)
      .setTime(5.0)
      .setRenderScale({1.0, 1.0});
  CHECK(loaded.act(kOfxActionInstanceChanged, &in) == kOfxStatOK);
  CHECK(loaded.plugin.changedParam == "scale");
  CHECK(loaded.plugin.time == 5.0);

  host::PropertySet begin =
      host::PropertySet::forAction(kOfxActionBeginInstanceChanged, "inArgs");
  begin.set(kOfxPropChangeReason, 0, kOfxChangeUserEdited);
  CHECK(loaded.act(kOfxActionBeginInstanceChanged, &begin) == kOfxStatOK);
  CHECK(loaded.act(kOfxActionEndInstanceChanged, &begin) == kOfxStatOK);
  CHECK(loaded.plugin.didRun("beginInstanceChanged"));
  CHECK(loaded.plugin.didRun("endInstanceChanged"));
}

TEST_CASE(dispatch_reaches_render_with_its_typed_arguments) {
  Loaded<Recorder> loaded;
  host::PropertySet in =
      host::PropertySet::forAction(kOfxImageEffectActionRender, "inArgs");
  host::propsets::ImageEffectActionRender_InArgs args(in.handle(),
                                                      host::PropertySet::suite());
  args.setTime(7.0).setRenderWindow({0, 0, 16, 8}).setRenderScale({1.0, 1.0});
  CHECK(loaded.act(kOfxImageEffectActionRender, &in) == kOfxStatOK);
  CHECK(loaded.plugin.didRun("render"));
  CHECK(loaded.plugin.time == 7.0);
  CHECK(loaded.plugin.renderWindow.x2 == 16);
  CHECK(loaded.plugin.renderWindow.y2 == 8);
  CHECK(loaded.plugin.lastHandle == loaded.effect.handle());

  CHECK(loaded.act(kOfxImageEffectActionBeginSequenceRender, &in) == kOfxStatOK);
  CHECK(loaded.act(kOfxImageEffectActionEndSequenceRender, &in) == kOfxStatOK);
  CHECK(loaded.plugin.didRun("beginSequenceRender"));
  CHECK(loaded.plugin.didRun("endSequenceRender"));
}

TEST_CASE(dispatch_reaches_the_actions_that_answer_in_out_args) {
  Loaded<Recorder> loaded;
  host::PropertySet in =
      host::PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "inArgs");
  in.set(kOfxPropTime, 0, 2.0);
  host::PropertySet out =
      host::PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "outArgs");
  CHECK(loaded.act(kOfxImageEffectActionGetRegionOfDefinition, &in, &out) == kOfxStatOK);
  CHECK(loaded.plugin.time == 2.0);
  CHECK(loaded.plugin.sawOutArgs);

  CHECK(loaded.act(kOfxImageEffectActionGetRegionsOfInterest, &in, &out) == kOfxStatOK);
  CHECK(loaded.act(kOfxImageEffectActionGetFramesNeeded, &in, &out) == kOfxStatOK);
  CHECK(loaded.act(kOfxImageEffectActionGetClipPreferences, nullptr, &out) == kOfxStatOK);
  CHECK(loaded.act(kOfxImageEffectActionIsIdentity, &in, &out) == kOfxStatOK);
  CHECK(loaded.act(kOfxImageEffectActionGetTimeDomain, nullptr, &out) == kOfxStatOK);
  CHECK(loaded.act(kOfxImageEffectActionGetOutputColourspace, &in, &out) == kOfxStatOK);
  CHECK(loaded.plugin.didRun("getRegionsOfInterest"));
  CHECK(loaded.plugin.didRun("getFramesNeeded"));
  CHECK(loaded.plugin.didRun("getClipPreferences"));
  CHECK(loaded.plugin.didRun("isIdentity"));
  CHECK(loaded.plugin.didRun("getTimeDomain"));
  CHECK(loaded.plugin.didRun("getOutputColourspace"));
}

TEST_CASE(dispatch_declines_an_action_it_does_not_know) {
  Loaded<Recorder> loaded;
  CHECK(loaded.act("OfxActionSomethingNewer") == kOfxStatReplyDefault);
  // An action about no effect at all is not this plugin's business either.
  CHECK(loaded.plugin.dispatch("OfxInteractActionDraw", nullptr, nullptr, nullptr) ==
        kOfxStatReplyDefault);
  CHECK(loaded.plugin.ran.size() == 1);  // only the load
}

TEST_CASE(dispatch_hands_the_dialog_action_its_user_data) {
  Loaded<Recorder> loaded;
  PropertySetSpy spy(loaded.plugin.suites);
  int userData = 0;
  CHECK(loaded.plugin.dispatch(kOfxActionDialog, &userData, nullptr, nullptr) ==
        kOfxStatOK);
  CHECK(loaded.plugin.didRun("dialog"));
  CHECK(loaded.plugin.dialogUserData == &userData);
  // The handle is what the plugin passed to RequestDialog, not an effect, so
  // it must never have been offered to the host as one.
  CHECK(PropertySetSpy::calls == 0);
}

TEST_CASE(dispatch_leaves_an_unknown_actions_handle_alone) {
  Loaded<Recorder> loaded;
  PropertySetSpy spy(loaded.plugin.suites);
  // An action added to OpenFX after this was written may pass anything as its
  // handle, so nothing may be done with it.
  int notAnEffect = 0;
  CHECK(loaded.plugin.dispatch("OfxActionSomethingNewer", &notAnEffect, nullptr,
                               nullptr) == kOfxStatReplyDefault);
  CHECK(loaded.plugin.ran.size() == 1);  // only the load
  CHECK(PropertySetSpy::calls == 0);
}

TEST_CASE(a_plugin_that_implements_nothing_declines_every_action) {
  Loaded<Silent> loaded;
  CHECK(loaded.loadStatus == kOfxStatReplyDefault);
  CHECK(loaded.act(kOfxActionDescribe) == kOfxStatReplyDefault);
  CHECK(loaded.act(kOfxImageEffectActionRender) == kOfxStatReplyDefault);
  CHECK(loaded.plugin.dispatch(kOfxActionUnload, nullptr, nullptr, nullptr) ==
        kOfxStatReplyDefault);
}

TEST_CASE(dispatch_turns_an_exception_into_a_status) {
  Loaded<Thrower> loaded;
  loaded.plugin.what = Thrower::What::OfxError;
  CHECK(loaded.act(kOfxImageEffectActionRender) == kOfxStatErrImageFormat);
  loaded.plugin.what = Thrower::What::Standard;
  CHECK(loaded.act(kOfxImageEffectActionRender) == kOfxStatErrUnknown);
  loaded.plugin.what = Thrower::What::Other;
  CHECK(loaded.act(kOfxImageEffectActionRender) == kOfxStatErrUnknown);
}

TEST_CASE(plugin_entry_builds_the_ofx_plugin_struct) {
  using Entry = plugin::PluginEntry<Recorder>;
  CHECK(Entry::numberOfPlugins() == 1);
  OfxPlugin* descriptor = Entry::get(0);
  CHECK(descriptor != nullptr);
  CHECK(std::string(descriptor->pluginApi) == kOfxImageEffectPluginApi);
  CHECK(descriptor->apiVersion == 1);
  CHECK(std::string(descriptor->pluginIdentifier) == Recorder::kIdentifier);
  CHECK(descriptor->pluginVersionMajor == plugin::ImageEffectPlugin::kVersionMajor);
  CHECK(descriptor->pluginVersionMinor == plugin::ImageEffectPlugin::kVersionMinor);
  CHECK(descriptor->setHost != nullptr);
  CHECK(descriptor->mainEntry != nullptr);
  CHECK(Entry::get(1) == nullptr);
  CHECK(Entry::get(0) == descriptor);  // the same struct every time
  // Each plugin class brings its own identifier.
  CHECK(std::string(plugin::PluginEntry<Silent>::get(0)->pluginIdentifier) ==
        Silent::kIdentifier);
  CHECK(std::string(plugin::PluginEntry<Thrower>::get(0)->pluginIdentifier) ==
        Thrower::kIdentifier);
}

TEST_CASE(plugin_entry_drives_the_one_plugin_it_holds) {
  tests::Effect effect;
  using Entry = plugin::PluginEntry<Recorder>;
  OfxPlugin* descriptor = Entry::get(0);
  descriptor->setHost(effect.host.ofx());
  CHECK(Entry::plugin().host() == effect.host.ofx());
  CHECK(descriptor->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatOK);
  CHECK(descriptor->mainEntry(kOfxActionDescribe, effect.handle(), nullptr, nullptr) ==
        kOfxStatOK);
  CHECK(Entry::plugin().didRun("describe"));
  CHECK(descriptor->mainEntry("OfxActionSomethingNewer", nullptr, nullptr, nullptr) ==
        kOfxStatReplyDefault);
}
