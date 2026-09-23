// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// A base class that turns the image effect actions into virtual functions with
// typed arguments, and the boilerplate a plugin binary needs. An action with no
// virtual of its own -- the OpenGL context actions, a host's own -- reaches
// otherAction() with its arguments as the host passed them.
//
//   class MyPlugin : public openfx::plugin::ImageEffectPlugin {
//    public:
//     static constexpr const char* kIdentifier = "com.example.MyPlugin";
//    protected:
//     OfxStatus describe(ImageEffect& effect) override { ... }
//     OfxStatus render(ImageEffect& effect, ActionArgs& in) override { ... }
//   };
//
// The binary exports the two functions every OFX binary does, answered by
// PluginEntry for one plugin:
//
//   using Entry = openfx::plugin::PluginEntry<MyPlugin>;
//   int OfxGetNumberOfPlugins(void) { return Entry::numberOfPlugins(); }
//   OfxPlugin* OfxGetPlugin(int nth) { return Entry::get(nth); }
//
// or by PluginEntries for several, which the host finds in the order listed:
//
//   using Entries = openfx::plugin::PluginEntries<Blur, Sharpen, Gain>;
//   int OfxGetNumberOfPlugins(void) { return Entries::numberOfPlugins(); }
//   OfxPlugin* OfxGetPlugin(int nth) { return Entries::get(nth); }

#include <ofxColour.h>
#include <ofxCore.h>
#include <ofxDialog.h>
#include <ofxDrawSuite.h>
#include <ofxGPURender.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxParametricParam.h>
#include <ofxProgress.h>
#include <ofxProperty.h>
#include <ofxTimeLine.h>

#include <cstring>
#include <string_view>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxSuites.h"
#include "openfx/plugin/ofxEffect.h"

namespace openfx::plugin {

class ImageEffectPlugin {
 public:
  // The version reported in the OfxPlugin struct; a derived class redefines
  // these along with the mandatory kIdentifier.
  static constexpr unsigned kVersionMajor = 1;
  static constexpr unsigned kVersionMinor = 0;

  ImageEffectPlugin() = default;
  virtual ~ImageEffectPlugin() = default;
  ImageEffectPlugin(const ImageEffectPlugin&) = delete;
  ImageEffectPlugin& operator=(const ImageEffectPlugin&) = delete;

  // The suites fetched in the Load action, for the wrappers that take them.
  SuiteContainer suites;

  const OfxHost* host() const { return host_; }

  // Called from the plugin's setHost trampoline, before any action.
  void setHost(OfxHost* host) { host_ = host; }

  // Map an action to its virtual. Exceptions become status codes, so the
  // action implementations may throw: an OfxException's code, kOfxStatErrMemory
  // for std::bad_alloc, kOfxStatErrUnknown for anything else. Nothing escapes,
  // not even from the logging of what was caught.
  OfxStatus dispatch(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                     OfxPropertySetHandle outArgs) noexcept {
    try {
      return dispatchAction(action, handle, inArgs, outArgs);
    } catch (...) {
      logCurrentException("{}", action);
      return statusFromCurrentException(kOfxStatErrUnknown);
    }
  }

  // Fetch into `suites` every suite these bindings know about from `host`.
  // The property, image effect and parameter suites (V1) are required. The
  // memory, multithread, timeline, interact, draw, OpenGL render, OpenCL
  // program, parametric parameter and dialog suites (V1), and the message and
  // progress suites (V1 and V2), are fetched if the host has them and are
  // simply absent if not. Returns kOfxStatErrMissingHostFeature if there is no
  // host, or, logging it, if the host lacks a required suite; what it does
  // have is fetched all the same.
  //
  // dispatch() calls this in the Load action. A plugin with a main entry of
  // its own calls it from there instead, before handing any action on to
  // dispatch().
  static OfxStatus fetchSuites(const OfxHost* host, SuiteContainer& suites) {
    if (!host)
      return kOfxStatErrMissingHostFeature;
    OPENFX_FETCH_SUITE(suites, host, kOfxPropertySuite, 1, OfxPropertySuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxImageEffectSuite, 1, OfxImageEffectSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxParameterSuite, 1, OfxParameterSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxMemorySuite, 1, OfxMemorySuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxMultiThreadSuite, 1, OfxMultiThreadSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxMessageSuite, 1, OfxMessageSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxMessageSuite, 2, OfxMessageSuiteV2);
    OPENFX_FETCH_SUITE(suites, host, kOfxProgressSuite, 1, OfxProgressSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxProgressSuite, 2, OfxProgressSuiteV2);
    OPENFX_FETCH_SUITE(suites, host, kOfxTimeLineSuite, 1, OfxTimeLineSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxInteractSuite, 1, OfxInteractSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxDrawSuite, 1, OfxDrawSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxOpenGLRenderSuite, 1,
                       OfxImageEffectOpenGLRenderSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxOpenCLProgramSuite, 1, OfxOpenCLProgramSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxParametricParameterSuite, 1,
                       OfxParametricParameterSuiteV1);
    OPENFX_FETCH_SUITE(suites, host, kOfxDialogSuite, 1, OfxDialogSuiteV1);
    if (!suites.has<OfxPropertySuiteV1>() || !suites.has<OfxImageEffectSuiteV1>() ||
        !suites.has<OfxParameterSuiteV1>()) {
      Logger::error(
          "host is missing one of the property, image effect and parameter suites");
      return kOfxStatErrMissingHostFeature;
    }
    return kOfxStatOK;
  }

  // The same, into this plugin's own `suites`.
  OfxStatus fetchSuites(const OfxHost* host) { return fetchSuites(host, suites); }

 protected:
  // Every action defaults to "I have nothing to say about this", which is what
  // a plugin that does not implement it must return.
  virtual OfxStatus load() { return kOfxStatReplyDefault; }
  virtual OfxStatus unload() { return kOfxStatReplyDefault; }
  virtual OfxStatus describe(ImageEffect&) { return kOfxStatReplyDefault; }
  virtual OfxStatus describeInContext(ImageEffect&, std::string_view) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus createInstance(ImageEffect&) { return kOfxStatReplyDefault; }
  virtual OfxStatus destroyInstance(ImageEffect&) { return kOfxStatReplyDefault; }
  virtual OfxStatus instanceChanged(ImageEffect&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus beginInstanceChanged(ImageEffect&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus endInstanceChanged(ImageEffect&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus purgeCaches(ImageEffect&) { return kOfxStatReplyDefault; }
  virtual OfxStatus syncPrivateData(ImageEffect&) { return kOfxStatReplyDefault; }
  virtual OfxStatus beginInstanceEdit(ImageEffect&) { return kOfxStatReplyDefault; }
  virtual OfxStatus endInstanceEdit(ImageEffect&) { return kOfxStatReplyDefault; }
  virtual OfxStatus getRegionOfDefinition(ImageEffect&, ActionArgs&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus getRegionsOfInterest(ImageEffect&, ActionArgs&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus getFramesNeeded(ImageEffect&, ActionArgs&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus getClipPreferences(ImageEffect&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus isIdentity(ImageEffect&, ActionArgs&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus render(ImageEffect&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus beginSequenceRender(ImageEffect&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus endSequenceRender(ImageEffect&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus getTimeDomain(ImageEffect&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  virtual OfxStatus getOutputColourspace(ImageEffect&, ActionArgs&, ActionArgs&) {
    return kOfxStatReplyDefault;
  }
  // The dialog action is about no effect: its handle is the pointer the plugin
  // passed to the dialog suite's RequestDialog.
  virtual OfxStatus dialog(void*) { return kOfxStatReplyDefault; }

  // Every action with no virtual above: the OpenGL context actions
  // (kOfxActionOpenGLContextAttached and kOfxActionOpenGLContextDetached), an
  // action of the host's own, or one added to OpenFX after this was written.
  // What the handle and the argument sets are depends on the action, so they
  // arrive exactly as the host passed them.
  virtual OfxStatus otherAction(const char* /*action*/, const void* /*handle*/,
                                OfxPropertySetHandle /*inArgs*/,
                                OfxPropertySetHandle /*outArgs*/) {
    return kOfxStatReplyDefault;
  }

 private:
  // One entry per action that is about an effect, each with a thunk calling the
  // virtual with the arguments it takes. The name is matched before anything
  // else is done with the handle, because a handle only means an effect for
  // these actions: kOfxActionDialog's is the plugin's own RequestDialog user
  // data, and an action otherAction() gets may pass anything at all.
  using ActionThunk = OfxStatus (*)(ImageEffectPlugin&, ImageEffect&, ActionArgs&,
                                    ActionArgs&);

  static ActionThunk findEffectAction(std::string_view name) {
    struct Entry {
      std::string_view name;
      ActionThunk thunk;
    };
    static constexpr Entry kActions[] = {
        {kOfxActionDescribe, [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&,
                                ActionArgs&) { return self.describe(effect); }},
        {kOfxImageEffectActionDescribeInContext,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in, ActionArgs&) {
           return self.describeInContext(
               effect,
               in.as<propsets::ImageEffectActionDescribeInContext_InArgs>().context());
         }},
        {kOfxActionCreateInstance,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs&) {
           return self.createInstance(effect);
         }},
        {kOfxActionDestroyInstance,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs&) {
           return self.destroyInstance(effect);
         }},
        {kOfxActionInstanceChanged,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in, ActionArgs&) {
           return self.instanceChanged(effect, in);
         }},
        {kOfxActionBeginInstanceChanged,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in, ActionArgs&) {
           return self.beginInstanceChanged(effect, in);
         }},
        {kOfxActionEndInstanceChanged,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in, ActionArgs&) {
           return self.endInstanceChanged(effect, in);
         }},
        {kOfxActionPurgeCaches,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs&) {
           return self.purgeCaches(effect);
         }},
        {kOfxActionSyncPrivateData,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs&) {
           return self.syncPrivateData(effect);
         }},
        {kOfxActionBeginInstanceEdit,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs&) {
           return self.beginInstanceEdit(effect);
         }},
        {kOfxActionEndInstanceEdit,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs&) {
           return self.endInstanceEdit(effect);
         }},
        {kOfxImageEffectActionGetRegionOfDefinition,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in,
            ActionArgs& out) { return self.getRegionOfDefinition(effect, in, out); }},
        {kOfxImageEffectActionGetRegionsOfInterest,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in,
            ActionArgs& out) { return self.getRegionsOfInterest(effect, in, out); }},
        {kOfxImageEffectActionGetFramesNeeded,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in,
            ActionArgs& out) { return self.getFramesNeeded(effect, in, out); }},
        {kOfxImageEffectActionGetClipPreferences,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs& out) {
           return self.getClipPreferences(effect, out);
         }},
        {kOfxImageEffectActionIsIdentity,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in,
            ActionArgs& out) { return self.isIdentity(effect, in, out); }},
        {kOfxImageEffectActionRender,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in, ActionArgs&) {
           return self.render(effect, in);
         }},
        {kOfxImageEffectActionBeginSequenceRender,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in, ActionArgs&) {
           return self.beginSequenceRender(effect, in);
         }},
        {kOfxImageEffectActionEndSequenceRender,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in, ActionArgs&) {
           return self.endSequenceRender(effect, in);
         }},
        {kOfxImageEffectActionGetTimeDomain,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs&, ActionArgs& out) {
           return self.getTimeDomain(effect, out);
         }},
        {kOfxImageEffectActionGetOutputColourspace,
         [](ImageEffectPlugin& self, ImageEffect& effect, ActionArgs& in,
            ActionArgs& out) { return self.getOutputColourspace(effect, in, out); }},
    };
    for (const Entry& entry : kActions)
      if (entry.name == name)
        return entry.thunk;
    return nullptr;
  }

  OfxStatus dispatchAction(const char* action, const void* handle,
                           OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    const std::string_view name(action);

    if (name == kOfxActionLoad) {
      OfxStatus status = fetchSuites(host_);
      return status == kOfxStatOK ? load() : status;
    }
    if (name == kOfxActionUnload)
      return unload();
    if (name == kOfxActionDialog)
      return dialog(const_cast<void*>(handle));

    const ActionThunk thunk = findEffectAction(name);
    if (!thunk)
      return otherAction(action, handle, inArgs, outArgs);
    if (!handle)
      return kOfxStatReplyDefault;

    ImageEffect effect(static_cast<OfxImageEffectHandle>(const_cast<void*>(handle)),
                       suites);
    ActionArgs in(inArgs, suites);
    ActionArgs out(outArgs, suites);
    return thunk(*this, effect, in, out);
  }

  OfxHost* host_{nullptr};
};

// The OfxPlugin struct and its C trampolines for a binary holding one plugin
// (PluginEntries below holds several). PluginT must derive from
// ImageEffectPlugin and define kIdentifier.
//
// The plugin is constructed on first use, which is inside one of the
// trampolines, so its constructor may throw: the trampolines catch that as
// they catch everything else. setHost has no status to return, so a failure
// there is logged and kept, and the main entry answers every action with it
// -- kOfxStatErrFatal, or an OfxException's code -- until a later setHost
// constructs the plugin after all.
template <class PluginT>
struct PluginEntry {
  static PluginT& plugin() {
    static PluginT instance;
    return instance;
  }

  static constexpr int numberOfPlugins() noexcept { return 1; }

  static OfxPlugin* get(int nth) noexcept {
    static OfxPlugin descriptor = {kOfxImageEffectPluginApi,
                                   1,
                                   PluginT::kIdentifier,
                                   PluginT::kVersionMajor,
                                   PluginT::kVersionMinor,
                                   setHost,
                                   mainEntry};
    return nth == 0 ? &descriptor : nullptr;
  }

 private:
  static void setHost(OfxHost* host) noexcept {
    try {
      plugin().setHost(host);
      setHostStatus_ = kOfxStatOK;
    } catch (...) {
      logCurrentException("setHost: constructing {}", PluginT::kIdentifier);
      setHostStatus_ = statusFromCurrentException(kOfxStatErrFatal);
    }
  }

  static OfxStatus mainEntry(const char* action, const void* handle,
                             OfxPropertySetHandle inArgs,
                             OfxPropertySetHandle outArgs) noexcept {
    if (setHostStatus_ != kOfxStatOK)
      return setHostStatus_;
    return callAtCBoundary(
        [&] { return plugin().dispatch(action, handle, inArgs, outArgs); },
        kOfxStatErrFatal);
  }

  // kOfxStatOK, or what constructing the plugin in setHost threw, as a status.
  static inline OfxStatus setHostStatus_ = kOfxStatOK;
};

// The OfxPlugin structs for a binary holding several plugins, one per class,
// with get(nth) returning them in the order listed. Each is the struct
// PluginEntry builds for that class, so each plugin has its own instance
// (PluginEntry<PluginT>::plugin()), setHost and main entry.
template <class... Plugins>
struct PluginEntries {
  static_assert(sizeof...(Plugins) > 0, "a binary holds at least one plugin");

  static constexpr int numberOfPlugins() noexcept {
    return static_cast<int>(sizeof...(Plugins));
  }

  static OfxPlugin* get(int nth) noexcept {
    static OfxPlugin* const kPlugins[] = {PluginEntry<Plugins>::get(0)...};
    return nth >= 0 && nth < numberOfPlugins() ? kPlugins[nth] : nullptr;
  }
};

}  // namespace openfx::plugin
