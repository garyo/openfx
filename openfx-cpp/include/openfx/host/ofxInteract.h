// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// The host side of interacts: the objects behind OfxInteractHandle, the
// overlay lifecycle and the event drivers, plus OfxInteractSuiteV1 over them.
//
// An overlay interact belongs to an image effect. The plugin puts the
// overlay's entry point on its effect descriptor
// (kOfxImageEffectPluginPropOverlayInteractV2, or V1 for an OpenGL overlay),
// and the host drives it through that entry point rather than through the
// plugin's own main entry: an InteractDescriptor is described once per effect
// descriptor, and one InteractInstance per effect instance is created,
// driven with draw and pen/key/focus events, and destroyed before its effect.
//
// A plugin draws through the DrawContext a host implements (host/
// ofxDrawSuiteHost.h): a real host renders it, while a host with no display
// can still record what the plugin would have drawn.

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxKeySyms.h>

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "openfx/host/ofxDrawSuiteHost.h"
#include "openfx/host/ofxEffect.h"
#include "openfx/host/ofxPropSetAccessors.h"
#include "openfx/host/ofxPropertySet.h"
#include "openfx/ofxExceptions.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxStatusStrings.h"

namespace openfx::host {

// What an OfxInteractHandle points to: an interact descriptor or an instance.
// A plugin is handed both -- interactGetPropertySet is called on a descriptor
// during Describe and on an instance thereafter -- so they share a base, as
// EffectDescriptor and EffectInstance do.
class InteractBase {
 public:
  virtual ~InteractBase() = default;

  InteractBase(const InteractBase&) = delete;
  InteractBase& operator=(const InteractBase&) = delete;

  virtual bool isInstance() const = 0;

  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }

  OfxInteractHandle handle() { return reinterpret_cast<OfxInteractHandle>(this); }
  static InteractBase* from(OfxInteractHandle h) {
    return reinterpret_cast<InteractBase*>(h);
  }

 protected:
  InteractBase() = default;

  PropertySet props_;
};

// The overlay entry point a plugin put on its effect descriptor: the OFX 1.5
// Draw-suite overlay if it declared one, else the OpenGL overlay. Null if the
// effect has no overlay at all. A context descriptor inherits the property
// from the global descriptor, so either may be passed.
inline OfxPluginEntryPoint* overlayEntryPoint(EffectDescriptor& effect,
                                              bool* usesDrawSuite = nullptr) {
  propsets::EffectDescriptor desc(effect.props().handle(), PropertySet::suite());
  void* entry = desc.overlayInteractV2(false);
  if (usesDrawSuite)
    *usesDrawSuite = entry != nullptr;
  if (!entry)
    entry = desc.overlayInteractV1(false);
  return reinterpret_cast<OfxPluginEntryPoint*>(entry);
}

// The result of the interact's Describe action: the plugin's overlay entry
// point plus the "InteractDescriptor" property set the host writes.
class InteractDescriptor : public InteractBase {
 public:
  // entry is the effect descriptor's overlay entry point; see
  // overlayEntryPoint() above.
  InteractDescriptor(Plugin& plugin, OfxPluginEntryPoint* entry, bool usesDrawSuite)
      : plugin_(plugin), entry_(entry), drawSuite_(usesDrawSuite) {
    if (!entry_)
      throw std::runtime_error(plugin.id() + ": interact with no entry point");
    props_ = PropertySet("InteractDescriptor");
    // The two properties the specification has the host write here describe
    // the frame buffer the interact draws into. The recording draw context is
    // 8 bits per component with no alpha, which is what a plain overlay is.
    accessor().setBitDepth(8).setHasAlpha(false);
  }

  bool isInstance() const override { return false; }

  Plugin& plugin() const { return plugin_; }
  OfxPluginEntryPoint* entryPoint() const { return entry_; }
  // Whether the overlay was declared through kOfxImageEffectPluginPropOverlayInteractV2
  // and so must draw through the Draw suite.
  bool usesDrawSuite() const { return drawSuite_; }

  propsets::InteractDescriptor accessor() {
    return propsets::InteractDescriptor(props_.handle(), PropertySet::suite());
  }

  // One action on this interact's entry point, logged like an effect action
  // and attributed the same way if the plugin crashes inside it.
  OfxStatus call(const char* action, InteractBase& on, PropertySet* inArgs) {
    Plugin::currentAction = action;
    Plugin::currentPlugin = plugin_.ofxPlugin()->pluginIdentifier;
    OfxStatus s =
        entry_(action, on.handle(), inArgs ? inArgs->handle() : nullptr, nullptr);
    Plugin::currentAction = nullptr;
    Logger::debug("interact {} -> {}", action, ofxStatusToString(s));
    return s;
  }

  // kOfxActionDescribe, once per effect descriptor.
  OfxStatus describe() { return call(kOfxActionDescribe, *this, nullptr); }

 private:
  Plugin& plugin_;
  OfxPluginEntryPoint* entry_;
  bool drawSuite_;
};

// The overlay of an effect descriptor, described and ready to make instances
// from; null if the plugin declared no overlay. If the plugin fails to
// describe the one it declared, this throws openfx::OfxException, whose code()
// is the status.
inline std::unique_ptr<InteractDescriptor> describeOverlay(Plugin& plugin,
                                                           EffectDescriptor& effect) {
  bool drawSuite = false;
  OfxPluginEntryPoint* entry = overlayEntryPoint(effect, &drawSuite);
  if (!entry)
    return nullptr;
  auto overlay = std::make_unique<InteractDescriptor>(plugin, entry, drawSuite);
  requireSuccess(overlay->describe(), plugin.id() + ": overlay describe failed");
  return overlay;
}

// One live overlay: the "InteractInstance" property set, parented to the
// descriptor's, and the actions the host sends it. The view it is drawn in --
// viewport size, pixel scale, background and suggested colour -- lives here
// and goes out with every action, as the specification has a host supply it.
class InteractInstance : public InteractBase {
 public:
  InteractInstance(InteractDescriptor& descriptor, EffectInstance& effect)
      : desc_(descriptor), effect_(effect) {
    props_ = PropertySet("InteractInstance", &descriptor.props());
    accessor().setEffectInstance(reinterpret_cast<void*>(effect.handle()));
    writeView();
  }

  ~InteractInstance() override { destroy(); }

  bool isInstance() const override { return true; }

  InteractDescriptor& descriptor() const { return desc_; }
  EffectInstance& effect() const { return effect_; }

  propsets::InteractInstance accessor() {
    return propsets::InteractInstance(props_.handle(), PropertySet::suite());
  }

  // --- The view the overlay is drawn in ------------------------------------

  // size is the viewport in screen pixels and pixelScale the size of one of
  // those pixels in the interact's canonical projection.
  void setViewport(OfxPointI size, OfxPointD pixelScale) {
    viewportSize_ = size;
    pixelScale_ = pixelScale;
    writeView();
  }
  void setBackgroundColour(const std::array<double, 3>& c) {
    background_ = c;
    writeView();
  }
  void setSuggestedColour(const std::array<double, 3>& c) {
    suggested_ = c;
    writeView();
  }
  // The frame and the render scale every action reports.
  void setTime(OfxTime time) { time_ = time; }
  void setRenderScale(OfxPointD scale) { renderScale_ = scale; }

  OfxPointI viewportSize() const { return viewportSize_; }
  OfxPointD pixelScale() const { return pixelScale_; }
  const std::array<double, 3>& backgroundColour() const { return background_; }
  const std::array<double, 3>& suggestedColour() const { return suggested_; }
  OfxTime time() const { return time_; }

  // The parameters the plugin slaved this interact to: a change to any of them
  // means the host should redraw. Set by the plugin on the instance, and read
  // through it, so a value left on the descriptor is seen too.
  std::vector<std::string> slaveToParams() const {
    std::vector<std::string> out;
    for (auto& name : props_.getStrings(kOfxInteractPropSlaveToParam))
      if (!name.empty())
        out.push_back(std::move(name));
    return out;
  }

  // What the plugin asked of the host through the interact suite: called by
  // interactRedraw and interactSwapBuffers below. A host with a real
  // viewport overrides these to actually redraw and swap; the base does
  // nothing, since a host is not required to act on either immediately.
  // Either may throw: the suite entry catches the exception and hands the
  // plugin a status instead -- the code of an openfx::OfxException,
  // kOfxStatErrMemory for std::bad_alloc, and kOfxStatFailed for anything else.
  virtual void redrawRequested() {}
  virtual void buffersSwapped() {}

  // --- The lifecycle -------------------------------------------------------

  // kOfxActionCreateInstance.
  OfxStatus create() {
    OfxStatus s = send(kOfxActionCreateInstance, nullptr);
    created_ = actionSucceeded(s);
    return s;
  }

  // kOfxActionDestroyInstance, once. Destructors run this, so it swallows
  // everything the action or the logging could throw.
  void destroy() noexcept {
    if (!created_)
      return;
    created_ = false;
    try {
      send(kOfxActionDestroyInstance, nullptr);
    } catch (...) {
      std::fprintf(stderr, "  ! destroy interact instance failed\n");
    }
  }

  // --- The events ----------------------------------------------------------

  // kOfxInteractActionDraw. The draw context is open for the action alone, so
  // a plugin that hangs on to it and draws later is refused, as it should be.
  OfxStatus draw(DrawContext& context) {
    PropertySet in = inArgs(kOfxInteractActionDraw);
    propsets::InteractActionDraw_InArgs args(in.handle(), PropertySet::suite());
    args.setDrawContext(reinterpret_cast<void*>(context.handle()));
    struct Open {  // closed again however the action ends
      explicit Open(DrawContext& c) : context(c) { context.open(); }
      ~Open() { context.close(); }
      Open(const Open&) = delete;
      Open& operator=(const Open&) = delete;
      DrawContext& context;
    } open(context);
    return send(kOfxInteractActionDraw, &in);
  }

  OfxStatus penDown(OfxPointD position, OfxPointI viewportPosition, double pressure) {
    return pen(kOfxInteractActionPenDown, position, viewportPosition, pressure);
  }
  OfxStatus penMotion(OfxPointD position, OfxPointI viewportPosition, double pressure) {
    return pen(kOfxInteractActionPenMotion, position, viewportPosition, pressure);
  }
  OfxStatus penUp(OfxPointD position, OfxPointI viewportPosition, double pressure) {
    return pen(kOfxInteractActionPenUp, position, viewportPosition, pressure);
  }

  OfxStatus keyDown(int keySym, const std::string& keyString) {
    return key(kOfxInteractActionKeyDown, keySym, keyString);
  }
  OfxStatus keyUp(int keySym, const std::string& keyString) {
    return key(kOfxInteractActionKeyUp, keySym, keyString);
  }
  OfxStatus keyRepeat(int keySym, const std::string& keyString) {
    return key(kOfxInteractActionKeyRepeat, keySym, keyString);
  }

  OfxStatus gainFocus() {
    PropertySet in = inArgs(kOfxInteractActionGainFocus);
    return send(kOfxInteractActionGainFocus, &in);
  }
  OfxStatus loseFocus() {
    PropertySet in = inArgs(kOfxInteractActionLoseFocus);
    return send(kOfxInteractActionLoseFocus, &in);
  }

 protected:
  // Around every action this instance sends, as EffectInstance::beforeAction()
  // and afterAction() are around an effect's: inArgs is the set the driver
  // built, null for CreateInstance and DestroyInstance, and interact actions
  // have no out-args. InteractDescriptor::call() bypasses both, and the
  // DestroyInstance this class's destructor sends reaches only these no-ops,
  // unless a derived destructor calls destroy() first.
  virtual void beforeAction(const char* /*action*/, PropertySet* /*inArgs*/) {}
  virtual void afterAction(const char* /*action*/, PropertySet* /*inArgs*/,
                           OfxStatus /*status*/) {}

 private:
  OfxStatus send(const char* action, PropertySet* in) {
    beforeAction(action, in);
    const OfxStatus status = desc_.call(action, *this, in);
    afterAction(action, in, status);
    return status;
  }

  // The properties every interact action carries: which effect, where it is
  // being drawn, when, and at what render scale.
  PropertySet inArgs(const char* action) const {
    PropertySet in = PropertySet::forAction(action, "inArgs");
    in.set(kOfxPropEffectInstance, 0, reinterpret_cast<void*>(effect_.handle()));
    in.set(kOfxPropTime, 0, time_);
    in.set(kOfxImageEffectPropRenderScale, 0, renderScale_.x);
    in.set(kOfxImageEffectPropRenderScale, 1, renderScale_.y);
    // The key actions carry neither, which forAction() reflects: a set that
    // does not declare a property refuses the write and says so, so only
    // write them where the action has them.
    if (in.has(kOfxInteractPropPixelScale)) {
      in.set(kOfxInteractPropPixelScale, 0, pixelScale_.x);
      in.set(kOfxInteractPropPixelScale, 1, pixelScale_.y);
    }
    if (in.has(kOfxInteractPropBackgroundColour))
      for (int i = 0; i < 3; ++i)
        in.set(kOfxInteractPropBackgroundColour, i, background_[size_t(i)]);
    return in;
  }

  OfxStatus pen(const char* action, OfxPointD position, OfxPointI viewportPosition,
                double pressure) {
    PropertySet in = inArgs(action);
    in.set(kOfxInteractPropPenPosition, 0, position.x);
    in.set(kOfxInteractPropPenPosition, 1, position.y);
    in.set(kOfxInteractPropPenViewportPosition, 0, viewportPosition.x);
    in.set(kOfxInteractPropPenViewportPosition, 1, viewportPosition.y);
    in.set(kOfxInteractPropPenPressure, 0, pressure);
    return send(action, &in);
  }

  OfxStatus key(const char* action, int keySym, const std::string& keyString) {
    PropertySet in = inArgs(action);
    in.set(kOfxPropKeySym, 0, keySym);
    in.set(kOfxPropKeyString, 0, keyString.c_str());
    return send(action, &in);
  }

  // The view, onto the instance's own property set. kOfxInteractPropViewportSize
  // was removed from the API in OFX 1.4 (it lives in ofxOld.h), so the
  // InteractInstance property set rightly omits it; it is written all the same
  // for plugins built against 1.3 and earlier, which still read it. The store
  // creates it on the first write from its own metadata.
  void writeView() {
    props_.set(kOfxInteractPropViewportSize, 0, viewportSize_.x);
    props_.set(kOfxInteractPropViewportSize, 1, viewportSize_.y);
    accessor()
        .setPixelScale({pixelScale_.x, pixelScale_.y})
        .setBackgroundColour(background_)
        .setSuggestedColour(suggested_);
  }

  InteractDescriptor& desc_;
  EffectInstance& effect_;
  OfxPointI viewportSize_{0, 0};
  OfxPointD pixelScale_{1, 1};
  std::array<double, 3> background_{0, 0, 0};
  std::array<double, 3> suggested_{1, 1, 1};
  OfxTime time_ = 0;
  OfxPointD renderScale_{1, 1};
  bool created_ = false;
};

// ---------------------------------------------------------------------------
// OfxInteractSuiteV1
// ---------------------------------------------------------------------------

namespace detail {

inline InteractInstance* asInteractInstance(OfxInteractHandle handle) {
  InteractBase* interact = InteractBase::from(handle);
  return interact && interact->isInstance() ? static_cast<InteractInstance*>(interact)
                                            : nullptr;
}

// Every entry point runs through callAtCBoundary, so that an exception from
// the host's InteractInstance hooks reaches the plugin as a status.

// A host with a real viewport swaps its double buffer here; the base
// InteractInstance does nothing, so a host that needs to act on this
// overrides buffersSwapped().
inline OfxStatus interactSwapBuffers(OfxInteractHandle handle) noexcept {
  return callAtCBoundary([&] {
    InteractInstance* instance = asInteractInstance(handle);
    if (!instance)
      return kOfxStatErrBadHandle;
    instance->buffersSwapped();
    return kOfxStatOK;
  });
}

// Likewise a redraw request: a host that draws again when it gets one
// overrides redrawRequested().
inline OfxStatus interactRedraw(OfxInteractHandle handle) noexcept {
  return callAtCBoundary([&] {
    InteractInstance* instance = asInteractInstance(handle);
    if (!instance)
      return kOfxStatErrBadHandle;
    instance->redrawRequested();
    return kOfxStatOK;
  });
}

inline OfxStatus interactGetPropertySet(OfxInteractHandle handle,
                                        OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary([&] {
    InteractBase* interact = InteractBase::from(handle);
    if (!interact || !props)
      return kOfxStatErrBadHandle;
    *props = interact->props().handle();
    return kOfxStatOK;
  });
}

}  // namespace detail

inline const OfxInteractSuiteV1* interactSuite() {
  static const OfxInteractSuiteV1 kSuite = {detail::interactSwapBuffers,
                                            detail::interactRedraw,
                                            detail::interactGetPropertySet};
  return &kSuite;
}

}  // namespace openfx::host
