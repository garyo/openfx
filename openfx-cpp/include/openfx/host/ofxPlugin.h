// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "openfx/host/ofxHost.h"
#include "openfx/host/ofxPluginBinary.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxStatusStrings.h"

namespace openfx::host {

class EffectDescriptor;

// An action that replied kOfxStatReplyDefault did nothing and left the default
// behaviour to the host, so both codes mean the call was handled.
inline bool actionSucceeded(OfxStatus status) {
  return status == kOfxStatOK || status == kOfxStatReplyDefault;
}

// One image effect plugin of a PluginBinary, driven through its main entry point.
class Plugin {
 public:
  Plugin(OfxPlugin* plugin, const PluginBinary& binary) : plugin_(plugin), bundlePath_(binary.path()) {}

  ~Plugin() {
    try {
      unload();
    } catch (const std::exception& e) {  // a destructor must not throw, so no logger here either
      std::fprintf(stderr, "  ! %s: unload failed: %s\n", plugin_->pluginIdentifier, e.what());
    }
  }

  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;

  std::string id() const { return plugin_->pluginIdentifier; }
  int versionMajor() const { return plugin_->pluginVersionMajor; }
  int versionMinor() const { return plugin_->pluginVersionMinor; }
  const std::filesystem::path& bundlePath() const { return bundlePath_; }
  bool isImageEffect() const { return std::strcmp(plugin_->pluginApi, kOfxImageEffectPluginApi) == 0; }
  OfxPlugin* ofxPlugin() const { return plugin_; }

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

  // setHost + kOfxActionLoad, once.
  void load(Host& host) {
    if (loaded_) return;
    plugin_->setHost(host.ofx());
    OfxStatus s = call(kOfxActionLoad, nullptr, nullptr, nullptr);
    if (!actionSucceeded(s)) throw std::runtime_error(id() + ": load action failed: " + ofxStatusToString(s));
    loaded_ = true;
  }

  // kOfxActionUnload, if loaded.
  void unload() {
    if (!loaded_) return;
    call(kOfxActionUnload, nullptr, nullptr, nullptr);
    loaded_ = false;
  }

  bool isLoaded() const { return loaded_; }

  // kOfxActionDescribe; the result lists the contexts the plugin supports.
  // Defined in ofxEffect.h, where EffectDescriptor is complete.
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
