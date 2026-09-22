// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// A base class that turns the image effect actions into virtual functions with
// typed arguments, and the boilerplate a single-plugin binary needs:
//
//   class MyPlugin : public openfx::plugin::ImageEffectPlugin {
//    public:
//     static constexpr const char* kIdentifier = "com.example.MyPlugin";
//    protected:
//     OfxStatus describe(ImageEffect& effect) override { ... }
//     OfxStatus render(ImageEffect& effect, ActionArgs& in) override { ... }
//   };
//
//   using Entry = openfx::plugin::PluginEntry<MyPlugin>;
//   OfxExport int OfxGetNumberOfPlugins(void) { return Entry::numberOfPlugins(); }
//   OfxExport OfxPlugin* OfxGetPlugin(int nth) { return Entry::get(nth); }

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProgress.h>
#include <ofxProperty.h>
#include <ofxTimeLine.h>

#include <cstring>
#include <exception>
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

  const OfxHost* host() const { return mHost; }

  // Called from the plugin's setHost trampoline, before any action.
  void setHost(OfxHost* host) { mHost = host; }

  // Map an action to its virtual. Exceptions become status codes, so the
  // action implementations may throw.
  OfxStatus dispatch(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                     OfxPropertySetHandle outArgs) {
    try {
      return dispatchAction(action, handle, inArgs, outArgs);
    } catch (const OfxException& e) {
      Logger::error("{}: {}", action, e.what());
      return e.code();
    } catch (const std::exception& e) {
      Logger::error("{}: {}", action, e.what());
      return kOfxStatErrUnknown;
    } catch (...) {
      Logger::error("{}: unknown exception", action);
      return kOfxStatErrUnknown;
    }
  }

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

  // Fill `suites` from the host. The property, image effect and parameter
  // suites are required; the rest are simply absent if the host lacks them.
  OfxStatus fetchSuites(OfxHost* host) {
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
    if (!suites.has<OfxPropertySuiteV1>() || !suites.has<OfxImageEffectSuiteV1>() ||
        !suites.has<OfxParameterSuiteV1>()) {
      Logger::error(
          "host is missing one of the property, image effect and parameter suites");
      return kOfxStatErrMissingHostFeature;
    }
    return kOfxStatOK;
  }

 private:
  OfxStatus dispatchAction(const char* action, const void* handle,
                           OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    const std::string_view name(action);

    if (name == kOfxActionLoad) {
      OfxStatus status = fetchSuites(mHost);
      return status == kOfxStatOK ? load() : status;
    }
    if (name == kOfxActionUnload)
      return unload();

    // Every remaining action is about an effect; an action this plugin does
    // not know may not be.
    if (!handle)
      return kOfxStatReplyDefault;

    ImageEffect effect(static_cast<OfxImageEffectHandle>(const_cast<void*>(handle)),
                       suites);
    ActionArgs in(inArgs, suites);
    ActionArgs out(outArgs, suites);

    if (name == kOfxActionDescribe)
      return describe(effect);
    if (name == kOfxImageEffectActionDescribeInContext)
      return describeInContext(
          effect, in.as<propsets::ImageEffectActionDescribeInContext_InArgs>().context());
    if (name == kOfxActionCreateInstance)
      return createInstance(effect);
    if (name == kOfxActionDestroyInstance)
      return destroyInstance(effect);
    if (name == kOfxActionInstanceChanged)
      return instanceChanged(effect, in);
    if (name == kOfxActionBeginInstanceChanged)
      return beginInstanceChanged(effect, in);
    if (name == kOfxActionEndInstanceChanged)
      return endInstanceChanged(effect, in);
    if (name == kOfxActionPurgeCaches)
      return purgeCaches(effect);
    if (name == kOfxActionSyncPrivateData)
      return syncPrivateData(effect);
    if (name == kOfxActionBeginInstanceEdit)
      return beginInstanceEdit(effect);
    if (name == kOfxActionEndInstanceEdit)
      return endInstanceEdit(effect);
    if (name == kOfxImageEffectActionGetRegionOfDefinition)
      return getRegionOfDefinition(effect, in, out);
    if (name == kOfxImageEffectActionGetRegionsOfInterest)
      return getRegionsOfInterest(effect, in, out);
    if (name == kOfxImageEffectActionGetFramesNeeded)
      return getFramesNeeded(effect, in, out);
    if (name == kOfxImageEffectActionGetClipPreferences)
      return getClipPreferences(effect, out);
    if (name == kOfxImageEffectActionIsIdentity)
      return isIdentity(effect, in, out);
    if (name == kOfxImageEffectActionRender)
      return render(effect, in);
    if (name == kOfxImageEffectActionBeginSequenceRender)
      return beginSequenceRender(effect, in);
    if (name == kOfxImageEffectActionEndSequenceRender)
      return endSequenceRender(effect, in);
    if (name == kOfxImageEffectActionGetTimeDomain)
      return getTimeDomain(effect, out);

    return kOfxStatReplyDefault;
  }

  OfxHost* mHost{nullptr};
};

// The OfxPlugin struct and its C trampolines for a binary holding one plugin.
// PluginT must derive from ImageEffectPlugin and define kIdentifier.
template <class PluginT>
struct PluginEntry {
  static PluginT& plugin() {
    static PluginT instance;
    return instance;
  }

  static constexpr int numberOfPlugins() { return 1; }

  static OfxPlugin* get(int nth) {
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
  static void setHost(OfxHost* host) { plugin().setHost(host); }

  static OfxStatus mainEntry(const char* action, const void* handle,
                             OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    return plugin().dispatch(action, handle, inArgs, outArgs);
  }
};

}  // namespace openfx::plugin
