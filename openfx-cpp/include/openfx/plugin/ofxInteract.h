// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side interacts: a wrapper over OfxInteractHandle and the action
// dispatcher for an overlay, mirroring ImageEffect and ImageEffectPlugin.
//
//   class Crosshair : public InteractPlugin<Crosshair> {
//    protected:
//     OfxStatus draw(Interact& interact, ActionArgs& in) override { ... }
//     OfxStatus penDown(Interact& interact, ActionArgs& in) override { ... }
//   };
//
//   // in the effect's describe():
//   effect.descriptor().setOverlayInteractV2(Crosshair::entryPoint(suites));
//
// Wiring: an overlay's actions arrive at a bare C entry point whose only
// argument is the interact handle, so there is nowhere for the host to pass
// the suites. They belong to the ImageEffectPlugin that owns the overlay, and
// that plugin hands them over in the one place it must mention the overlay
// anyway -- registering the entry point on its effect descriptor. entryPoint()
// therefore takes the owner's SuiteContainer, stores a pointer to it on the
// single InteractPlugin instance the trampoline dispatches to, and returns the
// trampoline. The container is the plugin's own member, filled in the Load
// action and alive as long as the plugin is, and the host cannot call the
// entry point before it has been given it, so the pointer is always valid by
// the time an action arrives.

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxKeySyms.h>

#include <string>
#include <string_view>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/ofxSuites.h"
#include "openfx/plugin/ofxDraw.h"
#include "openfx/plugin/ofxEffect.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

namespace detail {

inline const OfxInteractSuiteV1* requireInteractSuite(const SuiteContainer& suites) {
  const auto* suite = suites.get<OfxInteractSuiteV1>();
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxInteractSuite);
  return suite;
}

}  // namespace detail

// An interact descriptor or instance: its property set, the effect it belongs
// to, and the two things a plugin may ask of its host through the interact
// suite. Which of accessor() and descriptor() applies is decided by the action
// the overlay is in, exactly as for ImageEffect.
class Interact {
 public:
  Interact(OfxInteractHandle interact, const SuiteContainer& suites)
      : suites_(&suites), suite_(detail::requireInteractSuite(suites)),
        interact_(interact), props_(interact, suites) {}

  OfxInteractHandle handle() const { return interact_; }
  const SuiteContainer& suites() const { return *suites_; }
  PropertyAccessor& props() { return props_; }

  propsets::InteractInstance accessor() const {
    return propsets::InteractInstance(props_);
  }
  propsets::InteractDescriptor descriptor() const {
    return propsets::InteractDescriptor(props_);
  }

  // Ask the host to swap the interact's double buffer. Only a custom
  // parameter interact needs this; an image effect overlay does not.
  void swapBuffers() {
    check(suite_->interactSwapBuffers(interact_), "interactSwapBuffers");
  }

  // Ask the host to send the Draw action again, because something the overlay
  // shows has changed.
  void redraw() { check(suite_->interactRedraw(interact_), "interactRedraw"); }

  // The effect instance this interact was created for (kOfxPropEffectInstance);
  // null on a descriptor, which belongs to no instance yet.
  OfxImageEffectHandle effectHandle() const {
    return static_cast<OfxImageEffectHandle>(
        props_.get<PropId::OfxPropEffectInstance>(0, false));
  }

  // The effect as the plugin bindings see it, for its parameters and clips.
  ImageEffect effect() const { return ImageEffect(effectHandle(), *suites_); }

  // Ask the host to redraw this interact whenever a parameter of the effect
  // changes (kOfxInteractPropSlaveToParam). Set while creating the instance,
  // once per parameter the overlay shows.
  void slaveToParam(std::string_view name, int index = 0) {
    const std::string param(name);
    props_.set<PropId::OfxInteractPropSlaveToParam>(param.c_str(), index);
  }

  // Per-instance state the overlay keeps between actions, which it allocates
  // in createInstance and frees in destroyInstance.
  void* instanceData() const { return props_.get<PropId::OfxPropInstanceData>(0, false); }
  void setInstanceData(void* data) { props_.set<PropId::OfxPropInstanceData>(data); }

 private:
  static void check(OfxStatus status, const char* what) {
    if (status != kOfxStatOK)
      throw OfxException(status, what);
  }

  const SuiteContainer* suites_;
  const OfxInteractSuiteV1* suite_;
  OfxInteractHandle interact_;
  PropertyAccessor props_;
};

// The action dispatcher for one overlay, as ImageEffectPlugin is for an
// effect: every action is a virtual taking the Interact and the action's
// arguments, and every one defaults to "nothing to say about this".
//
// Derived is the overlay class itself (CRTP), because the host is handed a
// plain function pointer and so the instance it dispatches to must be
// reachable without one.
template <class Derived>
class InteractPlugin {
 public:
  virtual ~InteractPlugin() = default;
  InteractPlugin(const InteractPlugin&) = delete;
  InteractPlugin& operator=(const InteractPlugin&) = delete;

  // The one overlay object the trampoline dispatches to.
  static Derived& instance() {
    static Derived overlay;
    return overlay;
  }

  // The value to put on the effect descriptor, with the suites the overlay
  // will need:
  //   effect.descriptor().setOverlayInteractV2(MyOverlay::entryPoint(suites));
  static void* entryPoint(const SuiteContainer& suites) {
    instance().suites_ = &suites;
    return reinterpret_cast<void*>(&mainEntry);
  }

  // The overlay's main entry point, in the form OFX wants it. The overlay
  // object is constructed on first use, possibly here; if its constructor
  // throws, the host is told kOfxStatFailed, which has it ignore the interact.
  static OfxStatus mainEntry(const char* action, const void* handle,
                             OfxPropertySetHandle inArgs,
                             OfxPropertySetHandle outArgs) noexcept {
    return callAtCBoundary(
        [&] { return instance().dispatch(action, handle, inArgs, outArgs); });
  }

  // Map an action to its virtual. Exceptions become status codes, so the
  // action implementations may throw: an OfxException's code, kOfxStatErrMemory
  // for std::bad_alloc, kOfxStatErrUnknown for anything else. Nothing escapes,
  // not even from the logging of what was caught.
  OfxStatus dispatch(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                     OfxPropertySetHandle outArgs) noexcept {
    try {
      return dispatchAction(action, handle, inArgs, outArgs);
    } catch (...) {
      logCurrentException("interact {}", action);
      return statusFromCurrentException(kOfxStatErrUnknown);
    }
  }

 protected:
  // The owning plugin's suites, as entryPoint() was given them.
  const SuiteContainer& suites() const { return *suites_; }

  virtual OfxStatus describe(Interact&) { return kOfxStatReplyDefault; }
  virtual OfxStatus createInstance(Interact&) { return kOfxStatReplyDefault; }
  virtual OfxStatus destroyInstance(Interact&) { return kOfxStatReplyDefault; }
  virtual OfxStatus draw(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus penDown(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus penMotion(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus penUp(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus keyDown(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus keyUp(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus keyRepeat(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus gainFocus(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }
  virtual OfxStatus loseFocus(Interact&, ActionArgs&) { return kOfxStatReplyDefault; }

 private:
  // Only the overlay class itself constructs one, through instance().
  InteractPlugin() = default;
  friend Derived;

  OfxStatus dispatchAction(const char* action, const void* handle,
                           OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    (void)outArgs;  // every interact action's outArgs is null
    if (!handle || !suites_)
      return kOfxStatReplyDefault;

    const std::string_view name(action);
    Interact interact(static_cast<OfxInteractHandle>(const_cast<void*>(handle)),
                      *suites_);
    ActionArgs in(inArgs, *suites_);

    if (name == kOfxActionDescribe)
      return describe(interact);
    if (name == kOfxActionCreateInstance)
      return createInstance(interact);
    if (name == kOfxActionDestroyInstance)
      return destroyInstance(interact);
    if (name == kOfxInteractActionDraw)
      return draw(interact, in);
    if (name == kOfxInteractActionPenDown)
      return penDown(interact, in);
    if (name == kOfxInteractActionPenMotion)
      return penMotion(interact, in);
    if (name == kOfxInteractActionPenUp)
      return penUp(interact, in);
    if (name == kOfxInteractActionKeyDown)
      return keyDown(interact, in);
    if (name == kOfxInteractActionKeyUp)
      return keyUp(interact, in);
    if (name == kOfxInteractActionKeyRepeat)
      return keyRepeat(interact, in);
    if (name == kOfxInteractActionGainFocus)
      return gainFocus(interact, in);
    if (name == kOfxInteractActionLoseFocus)
      return loseFocus(interact, in);

    return kOfxStatReplyDefault;
  }

  const SuiteContainer* suites_ = nullptr;
};

}  // namespace openfx::plugin
