// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Plugin.h"

#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <openfx/host/ofxPropSetAccessors.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/ofxStatusStrings.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "Effect.h"
#include "Log.h"
#include "Suites.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace testhost {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Bundle
// ---------------------------------------------------------------------------

namespace {

// Architecture directories to try inside a bundle, most specific first (OFX 1.5 packaging).
std::vector<std::string> archDirs() {
#if defined(__APPLE__)
#if defined(__arm64__)
  return {"MacOS-arm-64", "MacOS"};
#else
  return {"MacOS-x86-64", "MacOS"};
#endif
#elif defined(_WIN32)
  return {"Win64", "Win32"};
#else
  return {"Linux-x86-64", "Linux-x86"};
#endif
}

fs::path binaryInBundle(const fs::path& bundle) {
  std::string base = bundle.filename().string();
  base = base.substr(0, base.size() - std::strlen(".bundle"));  // Foo.ofx
  for (const auto& arch : archDirs()) {
    fs::path candidate = bundle / "Contents" / arch / base;
    if (fs::exists(candidate)) return candidate;
  }
  throw std::runtime_error("no plugin binary for this architecture in " + bundle.string());
}

bool isBundleDir(const fs::path& p) { return fs::is_directory(p) && p.filename().string().ends_with(".ofx.bundle"); }

}  // namespace

std::vector<std::unique_ptr<Bundle>> Bundle::load(const fs::path& path) {
  std::vector<std::unique_ptr<Bundle>> out;
  if (isBundleDir(path)) {
    out.push_back(std::make_unique<Bundle>(binaryInBundle(path)));
  } else if (fs::is_directory(path)) {
    std::vector<fs::path> bundles;
    for (const auto& entry : fs::directory_iterator(path))
      if (isBundleDir(entry.path())) bundles.push_back(entry.path());
    std::sort(bundles.begin(), bundles.end());
    for (const auto& b : bundles) {
      try {
        out.push_back(std::make_unique<Bundle>(binaryInBundle(b)));
      } catch (const std::exception& e) {
        log::warn("skipping {}: {}", b.string(), e.what());
      }
    }
  } else if (fs::exists(path)) {
    out.push_back(std::make_unique<Bundle>(path));
  } else {
    throw std::runtime_error("no such plugin path: " + path.string());
  }
  return out;
}

Bundle::Bundle(const fs::path& binary) : path_(binary) {
#ifdef _WIN32
  HMODULE mod = LoadLibraryW(binary.wstring().c_str());
  if (!mod) throw std::runtime_error("cannot load " + binary.string());
  dl_ = mod;
  auto* getNumber = reinterpret_cast<int (*)()>(GetProcAddress(mod, "OfxGetNumberOfPlugins"));
  auto* getPlugin = reinterpret_cast<OfxPlugin* (*)(int)>(GetProcAddress(mod, "OfxGetPlugin"));
#else
  dl_ = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!dl_) throw std::runtime_error(std::string("cannot load ") + binary.string() + ": " + dlerror());
  auto* getNumber = reinterpret_cast<int (*)()>(dlsym(dl_, "OfxGetNumberOfPlugins"));
  auto* getPlugin = reinterpret_cast<OfxPlugin* (*)(int)>(dlsym(dl_, "OfxGetPlugin"));
#endif
  if (!getNumber || !getPlugin) throw std::runtime_error(binary.string() + " does not export the OFX entry points");
  int n = getNumber();
  for (int i = 0; i < n; ++i)
    if (OfxPlugin* p = getPlugin(i)) plugins_.push_back(p);
  log::debug("loaded {} ({} plugin{})", binary.string(), n, n == 1 ? "" : "s");
}

Bundle::~Bundle() {
  // Plugin binaries are deliberately never unloaded: the OfxPlugin structs and
  // any static state they hold must outlive every use, and unloading buys nothing here.
}

// ---------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------

namespace {
const void* fetchSuite(OfxPropertySetHandle, const char* name, int version) { return suites::fetch(name, version); }
}  // namespace

Host& Host::get() {
  static Host host;
  return host;
}

Host::Host() : props_("ImageEffectHost") {
  openfx::PropertyAccessor acc(props_.handle(), PropertySet::suite());
  openfx::host::propsets::ImageEffectHost host(acc);
  host.setType(kOfxTypeImageEffectHost)
      .setName("org.openeffects.testhost")
      .setLabel("OpenFX Test Host")
      .setVersion({1, 0, 0})
      .setVersionLabel("1.0")
      .setAPIVersion({1, 5})  // the headers carry no numeric API version
      .setIsBackground(1)
      .setSupportsOverlays(0)
      .setSupportsMultiResolution(1)
      .setSupportsTiles(1)
      .setTemporalClipAccess(1)
      .setSupportedComponents({kOfxImageComponentRGBA, kOfxImageComponentRGB, kOfxImageComponentAlpha})
      .setSupportedContexts({kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral, kOfxImageEffectContextGenerator})
      .setMultipleClipDepths(0)
      .setSupportsMultipleClipPARs(0)
      .setSetableFrameRate(0)
      .setSetableFielding(0)
      .setSequentialRender(0)
      .setSupportsStringAnimation(0)
      .setSupportsCustomInteract(0)
      .setSupportsChoiceAnimation(0)
      .setSupportsStrChoice(1)
      .setSupportsStrChoiceAnimation(0)
      .setSupportsBooleanAnimation(0)
      .setSupportsCustomAnimation(0)
      .setSupportsParametricAnimation(0)
      .setMaxParameters(-1)
      .setMaxPages(0)
      .setPageRowColumnCount({0, 0})
      .setHostOSHandle(nullptr)
      .setNativeOrigin(kOfxHostNativeOriginBottomLeft)
      .setRenderQualityDraft(0)
      .setOpenGLRenderSupported("false")
      .setOpenCLSupported("false")
      .setCPURenderSupported("true")
      .setColourManagementStyle(kOfxImageEffectColourManagementNone);
  // Not in the generated host set, but plugins ask for these.
  props_.set(kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
  props_.set(kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
  props_.set(kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat);
  props_.set(kOfxImageEffectPropCudaRenderSupported, 0, "false");
  props_.set(kOfxImageEffectPropCudaStreamSupported, 0, "false");
  props_.set(kOfxImageEffectPropMetalRenderSupported, 0, "false");
  props_.set(kOfxImageEffectPropOpenCLRenderSupported, 0, "false");

  host_.host = props_.handle();
  host_.fetchSuite = fetchSuite;
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

Plugin::Plugin(OfxPlugin* plugin, const Bundle& bundle) : plugin_(plugin), bundlePath_(bundle.path()) {}

Plugin::~Plugin() { unload(); }

bool Plugin::isImageEffect() const { return std::strcmp(plugin_->pluginApi, kOfxImageEffectPluginApi) == 0; }

const char* volatile Plugin::currentAction = nullptr;
const char* volatile Plugin::currentPlugin = nullptr;

OfxStatus Plugin::call(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle out) {
  currentAction = action;
  currentPlugin = plugin_->pluginIdentifier;
  OfxStatus s = plugin_->mainEntry(action, handle, in, out);
  currentAction = nullptr;
  log::debug("{} -> {}", action, ofxStatusToString(s));
  return s;
}

void Plugin::load() {
  if (loaded_) return;
  plugin_->setHost(Host::get().ofx());
  OfxStatus s = call(kOfxActionLoad, nullptr, nullptr, nullptr);
  if (s != kOfxStatOK && s != kOfxStatReplyDefault)
    throw std::runtime_error(id() + ": load action failed: " + ofxStatusToString(s));
  loaded_ = true;
}

void Plugin::unload() {
  if (!loaded_) return;
  call(kOfxActionUnload, nullptr, nullptr, nullptr);
  loaded_ = false;
}

std::unique_ptr<EffectDescriptor> Plugin::describe() {
  load();
  auto desc = std::make_unique<EffectDescriptor>(*this, nullptr, "");
  OfxStatus s = call(kOfxActionDescribe, desc->handle(), nullptr, nullptr);
  if (s != kOfxStatOK && s != kOfxStatReplyDefault)
    throw std::runtime_error(id() + ": describe failed: " + ofxStatusToString(s));
  return desc;
}

std::unique_ptr<EffectDescriptor> Plugin::describeInContext(const EffectDescriptor& global, const std::string& context) {
  auto desc = std::make_unique<EffectDescriptor>(*this, &global, context);
  PropertySet inArgs = PropertySet::forAction(kOfxImageEffectActionDescribeInContext, "inArgs");
  inArgs.set(kOfxImageEffectPropContext, 0, context.c_str());
  OfxStatus s = call(kOfxImageEffectActionDescribeInContext, desc->handle(), inArgs.handle(), nullptr);
  if (s != kOfxStatOK && s != kOfxStatReplyDefault)
    throw std::runtime_error(id() + ": describe in context " + context + " failed: " + ofxStatusToString(s));
  return desc;
}

}  // namespace testhost
