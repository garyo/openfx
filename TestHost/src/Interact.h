// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <openfx/host/ofxInteract.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DrawRecorder.h"
#include "Effect.h"
#include "Host.h"

namespace testhost {

// One event of the scripted interaction, in command-line order.
struct InteractEvent {
  enum class Kind {
    PenDown,
    PenMotion,
    PenUp,
    KeyDown,
    KeyUp,
    KeyRepeat,
    GainFocus,
    LoseFocus,
    Draw
  };

  Kind kind = Kind::Draw;
  OfxPointD position{};  // pen events, in canonical coordinates
  int keySym = 0;        // key events
  std::string keyString;
};

// What --interact and its companions asked for, per effect.
struct InteractOptions {
  bool enabled = false;
  std::optional<OfxPointI> viewport;  // default: the project size
  std::vector<InteractEvent> events;
  std::optional<std::filesystem::path> drawOut;
  // --expect-draws: the number of recorded draw commands the last Draw action
  // must produce, either exactly or at least.
  struct ExpectDraws {
    int count = 0;
    bool atLeast = false;
  };
  std::optional<ExpectDraws> expectDraws;
};

// A kOfxKey_* name, with or without the prefix, or a decimal keysym. Fills
// keyString with the UTF8 the key stands for, which is empty for a key that
// has none. Returns false if the name is neither.
bool parseKeySym(std::string_view name, int& keySym, std::string& keyString);

// An InteractInstance that counts the redraw and buffer-swap requests the
// interact suite hooks call, so a verbose log can report them and the
// slaved-parameter logic can tell whether the plugin asked for a redraw.
class CountingInteractInstance : public openfx::host::InteractInstance {
 public:
  using openfx::host::InteractInstance::InteractInstance;

  int redrawsRequested() const { return redraws_; }
  int bufferSwaps() const { return swaps_; }
  void clearRequests() {
    redraws_ = 0;
    swaps_ = 0;
  }

  void redrawRequested() override { ++redraws_; }
  void buffersSwapped() override { ++swaps_; }

 private:
  int redraws_ = 0;
  int swaps_ = 0;
};

// An effect instance's overlay while the host drives it: the interact
// descriptor and instance, the view they are drawn in, and the draw context
// that records what the plugin drew.
class Overlay {
 public:
  // The overlay of this effect, described and created, or null if the plugin
  // declared none. descriptor must be the context descriptor the instance was
  // made from; the overlay entry point is inherited from the global one.
  static std::unique_ptr<Overlay> create(Plugin& plugin, EffectDescriptor& descriptor,
                                         EffectInstance& instance, const Project& project,
                                         const InteractOptions& options);
  ~Overlay();

  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  // One Draw action; logs the recorded commands at Info and returns how many
  // there were. `why` says what prompted it, for the log.
  size_t draw(std::string_view why);

  // A parameter of the effect changed. The interact's slave-to-param list says
  // whether that means the overlay must be redrawn; if it does, it is.
  void parameterChanged(std::string_view name);

  // Runs the scripted events in order and applies --expect-draws and
  // --draw-out. Returns the number of checks that failed.
  int runScript();

 private:
  Overlay(std::unique_ptr<openfx::host::InteractDescriptor> descriptor,
          EffectInstance& instance, const Project& project,
          const InteractOptions& options);

  // The canonical position of a pen event mapped into viewport pixels.
  OfxPointI toViewport(OfxPointD position) const;
  void writeDrawOut(const std::filesystem::path& path) const;

  std::unique_ptr<openfx::host::InteractDescriptor> desc_;
  std::unique_ptr<CountingInteractInstance> instance_;
  RecordingDrawContext context_;
  ViewportMapping mapping_;
  InteractOptions options_;
  size_t lastDrawCommands_ = 0;
};

}  // namespace testhost
