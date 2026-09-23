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
// therefore copies the owner's suites onto instance(), the one overlay object
// the trampoline dispatches to, and returns the trampoline.
//
// Every overlay object keeps a copy of its own, which setSuites() gives it. A
// plugin that registers mainEntry itself calls instance().setSuites(suites),
// and one that makes overlay objects of its own and calls their dispatch()
// gives each its suites first. An overlay with none answers every action it
// has a virtual for with kOfxStatErrMissingHostFeature, and logs why.

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxKeySyms.h>

#include <initializer_list>
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

inline const OfxInteractSuiteV1* requireInteractSuite(const OfxInteractSuiteV1* suite) {
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxInteractSuite);
  return suite;
}

}  // namespace detail

// An interact descriptor or instance: its property set, the effect it belongs
// to, and the two things a plugin may ask of its host through the interact
// suite. Which of accessor() and descriptor() applies is decided by the action
// the overlay is in, exactly as for ImageEffect. It copies the suite pointers
// it needs, so a container built on the spot may go once it is made.
class Interact {
 public:
  Interact(OfxInteractHandle interact, const SuiteContainer& suites)
      : Interact(interact, suites.get<OfxInteractSuiteV1>(),
                 suites.get<OfxPropertySuiteV1>(), suites.get<OfxImageEffectSuiteV1>(),
                 suites.get<OfxParameterSuiteV1>()) {}

  // The image effect and parameter suites are needed only by effect().
  Interact(OfxInteractHandle interact, const OfxInteractSuiteV1* interactSuite,
           const OfxPropertySuiteV1* propSuite,
           const OfxImageEffectSuiteV1* effectSuite = nullptr,
           const OfxParameterSuiteV1* paramSuite = nullptr)
      : suite_(detail::requireInteractSuite(interactSuite)), propSuite_(propSuite),
        effectSuite_(effectSuite), paramSuite_(paramSuite), interact_(interact),
        props_(interact, suite_, propSuite) {}

  OfxInteractHandle handle() const { return interact_; }
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
        props_.soft().get<PropId::OfxPropEffectInstance>());
  }

  // The effect as the plugin bindings see it, for its parameters and clips.
  ImageEffect effect() const {
    return ImageEffect(effectHandle(), effectSuite_, propSuite_, paramSuite_);
  }

  // Ask the host to redraw this interact whenever a parameter of the effect
  // changes (kOfxInteractPropSlaveToParam). Set while creating the instance,
  // once per parameter the overlay shows: each call adds its parameter after
  // those already there, as the specification has a plugin set index 0, then
  // index 1 and so on. A host that has not made the property yet has none.
  void slaveToParam(std::string_view name) {
    const std::string param(name);
    props_.set<PropId::OfxInteractPropSlaveToParam>(
        param.c_str(), props_.soft().getDimension<PropId::OfxInteractPropSlaveToParam>());
  }

  // The same for several parameters at once: slaveToParams({"centre", "radius"}).
  void slaveToParams(std::initializer_list<std::string_view> names) {
    for (std::string_view name : names) slaveToParam(name);
  }

  // Per-instance state the overlay keeps between actions, which it allocates
  // in createInstance and frees in destroyInstance; null on a descriptor.
  void* instanceData() const { return props_.soft().get<PropId::OfxPropInstanceData>(); }
  void setInstanceData(void* data) { props_.set<PropId::OfxPropInstanceData>(data); }

 private:
  static void check(OfxStatus status, const char* what) {
    if (status != kOfxStatOK)
      throw OfxException(status, what);
  }

  const OfxInteractSuiteV1* suite_;
  const OfxPropertySuiteV1* propSuite_;
  const OfxImageEffectSuiteV1* effectSuite_;
  const OfxParameterSuiteV1* paramSuite_;
  OfxInteractHandle interact_;
  PropertyAccessor props_;
};

// The action dispatcher for one overlay, as ImageEffectPlugin is for an
// effect: every action is a virtual taking the Interact and the action's
// arguments, and every one defaults to "nothing to say about this". An action
// with no virtual of its own reaches otherAction() with its raw arguments.
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

  // Give this overlay object the suites its actions need, which it copies: the
  // interact and property suites for every action, the image effect and
  // parameter suites for Interact::effect(), and the draw suite for a Draw.
  void setSuites(const SuiteContainer& suites) { suites_ = suites; }

  // The value to put on the effect descriptor, having given instance() the
  // suites the overlay will need:
  //   effect.descriptor().setOverlayInteractV2(MyOverlay::entryPoint(suites));
  static void* entryPoint(const SuiteContainer& suites) {
    instance().setSuites(suites);
    return reinterpret_cast<void*>(&mainEntry);
  }

  // The overlay's main entry point, in the form OFX wants it. The overlay
  // object is constructed on first use, possibly here; if its constructor
  // throws, the host is told the exception's status as dispatch() maps it,
  // kOfxStatFailed for anything but an OfxException or an allocation failure.
  static OfxStatus mainEntry(const char* action, const void* handle,
                             OfxPropertySetHandle inArgs,
                             OfxPropertySetHandle outArgs) noexcept {
    return callAtCBoundary(
        [&] { return instance().dispatch(action, handle, inArgs, outArgs); });
  }

  // Map an action to its virtual. Exceptions become status codes, so the
  // action implementations may throw: an OfxException's code, kOfxStatErrMemory
  // for std::bad_alloc, and kOfxStatFailed, the specification's generic action
  // failure, for anything else. Nothing escapes, not even from the logging of
  // what was caught.
  OfxStatus dispatch(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                     OfxPropertySetHandle outArgs) noexcept {
    try {
      return dispatchAction(action, handle, inArgs, outArgs);
    } catch (...) {
      logCurrentException("interact {}", action);
      return statusFromCurrentException(kOfxStatFailed);
    }
  }

 protected:
  // The suites setSuites() or entryPoint() was given.
  const SuiteContainer& suites() const { return suites_; }

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

  // Every action with no virtual above, such as one of the host's own, with
  // its arguments exactly as the host passed them.
  virtual OfxStatus otherAction(const char* /*action*/, const void* /*handle*/,
                                OfxPropertySetHandle /*inArgs*/,
                                OfxPropertySetHandle /*outArgs*/) {
    return kOfxStatReplyDefault;
  }

 private:
  // Only the overlay class itself constructs one, through instance().
  InteractPlugin() = default;
  friend Derived;

  // One entry per interact action, each with a thunk calling the virtual with
  // the arguments it takes. As for an effect, the name is matched before
  // anything is done with the handle, which only these actions promise is an
  // interact. Every interact action's outArgs is null.
  using ActionThunk = OfxStatus (*)(InteractPlugin&, Interact&, ActionArgs&);

  static ActionThunk findInteractAction(std::string_view name) {
    struct Entry {
      std::string_view name;
      ActionThunk thunk;
    };
    static constexpr Entry kActions[] = {
        {kOfxActionDescribe, [](InteractPlugin& self, Interact& interact,
                                ActionArgs&) { return self.describe(interact); }},
        {kOfxActionCreateInstance,
         [](InteractPlugin& self, Interact& interact, ActionArgs&) {
           return self.createInstance(interact);
         }},
        {kOfxActionDestroyInstance,
         [](InteractPlugin& self, Interact& interact, ActionArgs&) {
           return self.destroyInstance(interact);
         }},
        {kOfxInteractActionDraw, [](InteractPlugin& self, Interact& interact,
                                    ActionArgs& in) { return self.draw(interact, in); }},
        {kOfxInteractActionPenDown,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.penDown(interact, in);
         }},
        {kOfxInteractActionPenMotion,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.penMotion(interact, in);
         }},
        {kOfxInteractActionPenUp,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.penUp(interact, in);
         }},
        {kOfxInteractActionKeyDown,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.keyDown(interact, in);
         }},
        {kOfxInteractActionKeyUp,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.keyUp(interact, in);
         }},
        {kOfxInteractActionKeyRepeat,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.keyRepeat(interact, in);
         }},
        {kOfxInteractActionGainFocus,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.gainFocus(interact, in);
         }},
        {kOfxInteractActionLoseFocus,
         [](InteractPlugin& self, Interact& interact, ActionArgs& in) {
           return self.loseFocus(interact, in);
         }},
    };
    for (const Entry& entry : kActions)
      if (entry.name == name)
        return entry.thunk;
    return nullptr;
  }

  OfxStatus dispatchAction(const char* action, const void* handle,
                           OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    const ActionThunk thunk = findInteractAction(action);
    if (!thunk)
      return otherAction(action, handle, inArgs, outArgs);
    // Thrown for dispatch() to log and turn into its status.
    if (suites_.suites.empty())
      throw SuiteNotFoundException(kOfxStatErrMissingHostFeature,
                                   "the overlay has no suites: give it them with "
                                   "setSuites() or entryPoint()");
    if (!handle)
      return kOfxStatReplyDefault;

    Interact interact(static_cast<OfxInteractHandle>(const_cast<void*>(handle)), suites_);
    ActionArgs in(inArgs, suites_);
    return thunk(*this, interact, in);
  }

  SuiteContainer suites_;
};

}  // namespace openfx::plugin
