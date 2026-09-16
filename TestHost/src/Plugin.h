// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "PropertySet.h"

namespace testhost {

class EffectDescriptor;

// One plugin binary (.ofx) and the OfxPlugin structs it exports.
class Bundle {
 public:
  // Accepts a .ofx.bundle directory, a bare .ofx binary, or a directory
  // containing bundles (each of which is loaded).
  static std::vector<std::unique_ptr<Bundle>> load(const std::filesystem::path& path);

  explicit Bundle(const std::filesystem::path& binary);
  ~Bundle();
  Bundle(const Bundle&) = delete;
  Bundle& operator=(const Bundle&) = delete;

  const std::filesystem::path& path() const { return path_; }
  const std::vector<OfxPlugin*>& plugins() const { return plugins_; }

 private:
  std::filesystem::path path_;
  void* dl_ = nullptr;
  std::vector<OfxPlugin*> plugins_;
};

// The host's OfxHost struct and property set, shared by every plugin.
class Host {
 public:
  static Host& get();
  OfxHost* ofx() { return &host_; }
  PropertySet& props() { return props_; }

 private:
  Host();
  OfxHost host_{};
  PropertySet props_;
};

// One image effect plugin, driven through its main entry point.
class Plugin {
 public:
  Plugin(OfxPlugin* plugin, const Bundle& bundle);
  ~Plugin();

  std::string id() const { return plugin_->pluginIdentifier; }
  int versionMajor() const { return plugin_->pluginVersionMajor; }
  int versionMinor() const { return plugin_->pluginVersionMinor; }
  const std::filesystem::path& bundlePath() const { return bundlePath_; }
  bool isImageEffect() const;
  OfxPlugin* ofxPlugin() const { return plugin_; }

  OfxStatus call(const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs);

  // The action and plugin currently inside mainEntry, for crash reports.
  static const char* volatile currentAction;
  static const char* volatile currentPlugin;

  void load();    // setHost + kOfxActionLoad, once
  void unload();  // kOfxActionUnload, if loaded

  // kOfxActionDescribe; the result lists supported contexts.
  std::unique_ptr<EffectDescriptor> describe();
  // kOfxImageEffectActionDescribeInContext; the result carries clips and params.
  std::unique_ptr<EffectDescriptor> describeInContext(const EffectDescriptor& global, const std::string& context);

 private:
  OfxPlugin* plugin_;
  std::filesystem::path bundlePath_;
  bool loaded_ = false;
};

}  // namespace testhost
