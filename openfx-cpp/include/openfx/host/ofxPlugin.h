// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "openfx/host/ofxHost.h"
#include "openfx/host/ofxPluginBinary.h"
#include "openfx/ofxExceptions.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxStatusStrings.h"

namespace openfx::host {

class EffectDescriptor;

// An action that replied kOfxStatReplyDefault did nothing and left the default
// behaviour to the host, so both codes mean the call was handled.
inline bool actionSucceeded(OfxStatus status) {
  return status == kOfxStatOK || status == kOfxStatReplyDefault;
}

// For a host that cannot go on after an action fails: throws
// openfx::OfxException, whose code() is the status, unless it is a success.
// `what` says what failed.
inline void requireSuccess(OfxStatus status, std::string_view what) {
  if (!actionSucceeded(status))
    throw OfxException(status, std::string(what));
}

// One image effect plugin of a PluginBinary, driven through its main entry point.
class Plugin {
 public:
  Plugin(OfxPlugin* plugin, const PluginBinary& binary) : Plugin(plugin, binary.path()) {}

  // A plugin with no binary behind it: one linked into the host itself, or a
  // test's stub. bundlePath is what kOfxPluginPropFilePath reports.
  Plugin(OfxPlugin* plugin, std::filesystem::path bundlePath)
      : plugin_(plugin), bundlePath_(std::move(bundlePath)) {}

  // A destructor must not throw, so whatever the Unload or the logging around
  // it throws is logged, as best it can be, and goes no further.
  ~Plugin() {
    try {
      unload();
    } catch (...) {
      logCurrentException("{}: unload", plugin_->pluginIdentifier);
    }
  }

  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;

  std::string id() const { return plugin_->pluginIdentifier; }
  int versionMajor() const { return plugin_->pluginVersionMajor; }
  int versionMinor() const { return plugin_->pluginVersionMinor; }
  const std::filesystem::path& bundlePath() const { return bundlePath_; }
  bool isImageEffect() const {
    return std::strcmp(plugin_->pluginApi, kOfxImageEffectPluginApi) == 0;
  }
  OfxPlugin* ofxPlugin() const { return plugin_; }

  // One action, straight to the plugin's main entry. For an instance's
  // actions this bypasses the instance's own bookkeeping -- its action hooks,
  // and its record of whether the plugin created it -- which
  // EffectInstance::action() keeps.
  OfxStatus call(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                 OfxPropertySetHandle outArgs) {
    currentAction = action;
    currentPlugin = plugin_->pluginIdentifier;
    OfxStatus s = plugin_->mainEntry(action, handle, inArgs, outArgs);
    currentAction = nullptr;
    Logger::debug("{} -> {}", action, ofxStatusToString(s));
    return s;
  }

  // The plugin and action currently inside mainEntry, or null between calls.
  // They exist for a signal handler to attribute a crash to the plugin: nothing
  // else in a host's state can safely be read from one.
  static inline const char* volatile currentAction = nullptr;
  static inline const char* volatile currentPlugin = nullptr;

  // setHost + kOfxActionLoad, once. The OfxHost is the framework's, from
  // load(Host&), or one a host filled in itself; the plugin may keep it until
  // it is unloaded. A failed Load throws openfx::OfxException.
  void load(OfxHost* host) {
    if (loaded_)
      return;
    plugin_->setHost(host);
    requireSuccess(call(kOfxActionLoad, nullptr, nullptr, nullptr),
                   id() + ": load action failed");
    loaded_ = true;
  }
  void load(Host& host) { load(host.ofx()); }

  // kOfxActionUnload, if loaded.
  void unload() {
    if (!loaded_)
      return;
    call(kOfxActionUnload, nullptr, nullptr, nullptr);
    loaded_ = false;
  }

  bool isLoaded() const { return loaded_; }

  // kOfxActionDescribe; the result lists the contexts the plugin supports.
  // Defined in ofxEffect.h, where EffectDescriptor is complete. Either
  // describe action failing throws openfx::OfxException.
  std::unique_ptr<EffectDescriptor> describe();
  // kOfxImageEffectActionDescribeInContext; the result carries clips and params.
  std::unique_ptr<EffectDescriptor> describeInContext(const EffectDescriptor& global,
                                                      const std::string& context);

 private:
  OfxPlugin* plugin_;
  std::filesystem::path bundlePath_;
  bool loaded_ = false;
};

}  // namespace openfx::host
