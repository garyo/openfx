// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Interact.h"

#include <ofxKeySyms.h>
#include <openfx/ofxLog.h>
#include <openfx/ofxStatusStrings.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <string>

#include "ImageIO.h"

namespace testhost {

namespace {

using openfx::Logger;
using openfx::host::InteractDescriptor;

// The view the test host pretends to draw the overlay in: a neutral dark
// background and a white suggested colour, both of which a plugin may read.
constexpr std::array<double, 3> kBackgroundColour{0.2, 0.2, 0.2};
constexpr std::array<double, 3> kSuggestedColour{1.0, 1.0, 1.0};
constexpr double kPenPressureDown = 1.0;
constexpr double kPenPressureUp = 0.0;

// The kOfxKey_* names worth naming on a command line. Every other key is a
// character, whose keysym is its ASCII code (kOfxKey_a is 0x061), or a number.
const std::map<std::string, int>& namedKeys() {
  static const std::map<std::string, int> kKeys = {
      {"Unknown", kOfxKey_Unknown},
      {"BackSpace", kOfxKey_BackSpace},
      {"Tab", kOfxKey_Tab},
      {"Return", kOfxKey_Return},
      {"Escape", kOfxKey_Escape},
      {"Delete", kOfxKey_Delete},
      {"space", kOfxKey_space},
      {"Home", kOfxKey_Home},
      {"End", kOfxKey_End},
      {"Left", kOfxKey_Left},
      {"Up", kOfxKey_Up},
      {"Right", kOfxKey_Right},
      {"Down", kOfxKey_Down},
      {"Page_Up", kOfxKey_Page_Up},
      {"Page_Down", kOfxKey_Page_Down},
      {"Shift_L", kOfxKey_Shift_L},
      {"Shift_R", kOfxKey_Shift_R},
      {"Control_L", kOfxKey_Control_L},
      {"Control_R", kOfxKey_Control_R},
      {"Alt_L", kOfxKey_Alt_L},
      {"Alt_R", kOfxKey_Alt_R},
  };
  return kKeys;
}

const char* eventName(InteractEvent::Kind kind) {
  switch (kind) {
    case InteractEvent::Kind::PenDown:
      return "pen down";
    case InteractEvent::Kind::PenMotion:
      return "pen motion";
    case InteractEvent::Kind::PenUp:
      return "pen up";
    case InteractEvent::Kind::KeyDown:
      return "key down";
    case InteractEvent::Kind::KeyUp:
      return "key up";
    case InteractEvent::Kind::KeyRepeat:
      return "key repeat";
    case InteractEvent::Kind::GainFocus:
      return "gain focus";
    case InteractEvent::Kind::LoseFocus:
      return "lose focus";
    case InteractEvent::Kind::Draw:
      return "draw";
  }
  return "?";
}

}  // namespace

bool parseKeySym(std::string_view name, int& keySym, std::string& keyString) {
  std::string key(name);
  if (key.rfind("kOfxKey_", 0) == 0)
    key = key.substr(std::strlen("kOfxKey_"));
  if (key.empty())
    return false;
  if (auto it = namedKeys().find(key); it != namedKeys().end()) {
    keySym = it->second;
    keyString = key == "space" ? " " : "";
    return true;
  }
  // A single printable character is its own keysym and its own UTF8 value.
  if (key.size() == 1 && key[0] > 0x20 && key[0] < 0x7f) {
    keySym = static_cast<unsigned char>(key[0]);
    keyString = key;
    return true;
  }
  try {
    size_t used = 0;
    keySym = std::stoi(key, &used, 0);
    keyString.clear();
    return used == key.size();
  } catch (const std::exception&) {
    return false;
  }
}

std::unique_ptr<Overlay> Overlay::create(Plugin& plugin, EffectDescriptor& descriptor,
                                         EffectInstance& instance, const Project& project,
                                         const InteractOptions& options) {
  bool usesDrawSuite = false;
  if (!openfx::host::overlayEntryPoint(descriptor, &usesDrawSuite))
    return nullptr;
  // A V1 overlay draws with OpenGL, and this host has no context to draw in;
  // sending it the Draw action would have it issue calls into nothing.
  if (!usesDrawSuite) {
    Logger::info("the overlay is an OpenGL (V1) interact, which this host cannot drive");
    return nullptr;
  }
  auto desc = openfx::host::describeOverlay(plugin, descriptor);
  if (!desc)
    return nullptr;
  return std::unique_ptr<Overlay>(
      new Overlay(std::move(desc), instance, project, options));
}

Overlay::Overlay(std::unique_ptr<InteractDescriptor> descriptor, EffectInstance& instance,
                 const Project& project, const InteractOptions& options)
    : desc_(std::move(descriptor)), options_(options) {
  const OfxPointI size =
      options.viewport.value_or(OfxPointI{project.width, project.height});
  // The viewport shows the whole project, so one screen pixel is that much of
  // the canonical plane: kOfxInteractPropPixelScale exactly.
  mapping_.size = size;
  mapping_.origin = {double(project.originX), double(project.originY)};
  mapping_.pixelScale = {size.x > 0 ? double(project.width) / size.x : 1.0,
                         size.y > 0 ? double(project.height) / size.y : 1.0};

  instance_ = std::make_unique<CountingInteractInstance>(*desc_, instance);
  instance_->setViewport(size, mapping_.pixelScale);
  instance_->setBackgroundColour(kBackgroundColour);
  instance_->setSuggestedColour(kSuggestedColour);
  Logger::info("overlay: {} interact, viewport {}x{}, pixel scale {},{}",
               desc_->usesDrawSuite() ? "Draw suite" : "OpenGL", size.x, size.y,
               mapping_.pixelScale.x, mapping_.pixelScale.y);
  OfxStatus s = instance_->create();
  if (!openfx::host::actionSucceeded(s))
    Logger::warn("overlay create instance failed: {}", ofxStatusToString(s));
}

Overlay::~Overlay() {
  instance_.reset();  // the interact is destroyed before the effect it belongs to
  desc_.reset();
}

OfxPointI Overlay::toViewport(OfxPointD position) const {
  return {static_cast<int>(
              std::lround((position.x - mapping_.origin.x) / mapping_.pixelScale.x)),
          static_cast<int>(
              std::lround((position.y - mapping_.origin.y) / mapping_.pixelScale.y))};
}

size_t Overlay::draw(std::string_view why) {
  instance_->setTime(openfx::host::timeline().current);
  OfxStatus s = instance_->draw(context_);
  lastDrawCommands_ = context_.commands().size();
  Logger::info("overlay draw ({}): {} command{} -> {}", why, lastDrawCommands_,
               lastDrawCommands_ == 1 ? "" : "s", ofxStatusToString(s));
  for (const auto& c : context_.commands()) Logger::info("     {}", c.summary());
  return lastDrawCommands_;
}

void Overlay::parameterChanged(std::string_view name) {
  const auto slaves = instance_->slaveToParams();
  if (std::find(slaves.begin(), slaves.end(), name) == slaves.end())
    return;
  draw(std::string("slaved to ") + std::string(name));
}

int Overlay::runScript() {
  for (const InteractEvent& e : options_.events) {
    instance_->setTime(openfx::host::timeline().current);
    OfxStatus s = kOfxStatReplyDefault;
    switch (e.kind) {
      case InteractEvent::Kind::PenDown:
        s = instance_->penDown(e.position, toViewport(e.position), kPenPressureDown);
        break;
      case InteractEvent::Kind::PenMotion:
        s = instance_->penMotion(e.position, toViewport(e.position), kPenPressureDown);
        break;
      case InteractEvent::Kind::PenUp:
        s = instance_->penUp(e.position, toViewport(e.position), kPenPressureUp);
        break;
      case InteractEvent::Kind::KeyDown:
        s = instance_->keyDown(e.keySym, e.keyString);
        break;
      case InteractEvent::Kind::KeyUp:
        s = instance_->keyUp(e.keySym, e.keyString);
        break;
      case InteractEvent::Kind::KeyRepeat:
        s = instance_->keyRepeat(e.keySym, e.keyString);
        break;
      case InteractEvent::Kind::GainFocus:
        s = instance_->gainFocus();
        break;
      case InteractEvent::Kind::LoseFocus:
        s = instance_->loseFocus();
        break;
      case InteractEvent::Kind::Draw:
        draw("--draw");
        continue;
    }
    Logger::debug("overlay {} -> {}", eventName(e.kind), ofxStatusToString(s));
  }
  if (int redraws = instance_->redrawsRequested(); redraws > 0) {
    Logger::info("overlay asked for {} redraw{}", redraws, redraws == 1 ? "" : "s");
    instance_->clearRequests();
    draw("redraw requested");
  }

  int failures = 0;
  if (options_.expectDraws) {
    const auto& want = *options_.expectDraws;
    const bool ok = want.atLeast ? int(lastDrawCommands_) >= want.count
                                 : int(lastDrawCommands_) == want.count;
    std::cout << "   draw commands = " << lastDrawCommands_
              << (ok ? "  ok" : "  MISMATCH") << " (expected "
              << (want.atLeast ? ">=" : "") << want.count << ")\n";
    if (!ok)
      ++failures;
  }
  if (options_.drawOut)
    writeDrawOut(*options_.drawOut);
  return failures;
}

void Overlay::writeDrawOut(const std::filesystem::path& path) const {
  if (mapping_.size.x <= 0 || mapping_.size.y <= 0) {
    Logger::warn("--draw-out: the viewport is empty");
    return;
  }
  auto image = ImageBuffer::create({0, 0, mapping_.size.x, mapping_.size.y},
                                   Components::RGBA, Depth::Float);
  const std::array<float, 4> background{float(kBackgroundColour[0]),
                                        float(kBackgroundColour[1]),
                                        float(kBackgroundColour[2]), 1.0f};
  for (int y = 0; y < mapping_.size.y; ++y)
    for (int x = 0; x < mapping_.size.x; ++x) image->setPixel(x, y, background);

  rasterise(context_, mapping_, [&](int x, int y, const OfxRGBAColourF& c) {
    // The specification has a host composite a non-opaque colour "over".
    auto under = image->pixel(x, y);
    const float a = std::clamp(c.a, 0.0f, 1.0f);
    image->setPixel(x, y,
                    {c.r * a + under[0] * (1 - a), c.g * a + under[1] * (1 - a),
                     c.b * a + under[2] * (1 - a), a + under[3] * (1 - a)});
  });
  writeImage(path, *image);
  std::cout << "   wrote " << path.string() << "\n";
}

}  // namespace testhost
