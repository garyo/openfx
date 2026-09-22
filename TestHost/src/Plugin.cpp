// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Plugin.h"

#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <openfx/host/ofxPropSetAccessors.h>
#include <openfx/ofxLog.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/ofxStatusStrings.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "Effect.h"
#include "Suites.h"

namespace testhost {

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
      .setApiVersion({1, 5})  // the headers carry no numeric API version
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
      .setSupportedPixelDepths({kOfxBitDepthByte, kOfxBitDepthShort, kOfxBitDepthFloat})
      .setOpenGLRenderSupported("false")
      .setOpenCLSupported("false")
      .setOpenCLRenderSupported("false")
      .setCudaRenderSupported("false")
      .setCudaStreamSupported("false")
      .setMetalRenderSupported("false")
      .setCpuRenderSupported("true")
      .setColourManagementStyle(kOfxImageEffectColourManagementNone);

  host_.host = props_.handle();
  host_.fetchSuite = fetchSuite;
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

Plugin::Plugin(OfxPlugin* plugin, const PluginBinary& binary) : plugin_(plugin), bundlePath_(binary.path()) {}

Plugin::~Plugin() {
  try {
    unload();
  } catch (const std::exception& e) {  // a destructor must not throw, so no logger here either
    std::fprintf(stderr, "  ! %s: unload failed: %s\n", plugin_->pluginIdentifier, e.what());
  }
}

bool Plugin::isImageEffect() const { return std::strcmp(plugin_->pluginApi, kOfxImageEffectPluginApi) == 0; }

const char* volatile Plugin::currentAction = nullptr;
const char* volatile Plugin::currentPlugin = nullptr;

OfxStatus Plugin::call(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle out) {
  currentAction = action;
  currentPlugin = plugin_->pluginIdentifier;
  OfxStatus s = plugin_->mainEntry(action, handle, in, out);
  currentAction = nullptr;
  openfx::Logger::debug("{} -> {}", action, ofxStatusToString(s));
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
