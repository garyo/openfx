// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
//
// ofxtesthost: load OFX image effect plugins, describe them, set parameters,
// render an image through one plugin or a chain of them, and check the result.

#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <openfx/host/ofxDefaultSuites.h>
#include <openfx/ofxLog.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "Effect.h"
#include "Host.h"
#include "ImageIO.h"
#include "Interact.h"

#ifndef _WIN32
#  include <execinfo.h>
#  include <unistd.h>
#  include <csignal>
#endif

using namespace testhost;

namespace {

const char* kUsage = R"(usage: ofxtesthost [options] <plugin-path>...

  <plugin-path>   a .ofx.bundle directory, a bare .ofx binary, or a directory of bundles

Selecting and configuring effects (repeat to render a chain, in order):
  --plugin ID           plugin identifier (default: the first image effect found)
  --context NAME        context to instantiate (default: filter, else general, else generator)
  --param NAME=VALUE    set a parameter on the most recent --plugin; VALUE is comma-separated
                        for multi-value params, true/false for booleans
  --param NAME@TIME=VALUE
                        set a keyframe at TIME instead (repeatable; ignored, with a note,
                        by a parameter type that does not animate)
  --clip NAME=SOURCE    attach an image to clip NAME of the most recent --plugin. SOURCE is an
                        image file, fill:R,G,B,A, ramp, or input (the effect's main input image)

Host choices the spec leaves open (default: what each plugin lists first):
  --components TYPE     negotiate RGBA, RGB or Alpha for every clip that supports it
  --depth TYPE          negotiate Byte, Short or Float if the plugin supports it
  --origin X,Y          place the input image's bounds at (X,Y) instead of (0,0)
  --row-padding N       add N unused bytes to every image row
  --renders N           render the frame N times through the same instance (default 1)
  --time T              frame to render (default 0)
  --colour-management STYLE
                        colour management to advertise: none (default), basic or core.
                        Full and OCIO are not supported; a plugin that asks for one gets
                        the highest style this host offers, as the spec directs
  --colourspace NAME    the colourspace the host supplies its input images in
                        (default: ofx_scene_linear for basic, ACEScg for core)
  --tiles N             render each frame as N x N tiles, one Render action each
  --tile WxH            render each frame in tiles of W x H pixels
  --check-tiles         with --tiles/--tile, also render the frame whole and report the
                        first pixel where the two differ
  --render-scale S|SX,SY
                        render at a proxy scale below 1; inputs are resampled to it
                        (nearest neighbour) and the render window is in scaled pixels
  --frames FIRST-LAST   render a sequence of frames, one BeginSequenceRender for the whole
  --frames N            range and one EndSequenceRender after it; N alone means 0-(N-1)

Overlay interacts (the plugin must declare one; any of these implies --interact):
  --interact            describe and create the plugin's overlay interact on the instance
  --viewport WxH        the view the overlay is drawn in (default: the project size)
  --pen down|move|up X,Y
                        send a pen event at the canonical position X,Y (repeatable, in
                        the order given; the viewport position is derived)
  --key down|up|repeat SYM
                        send a key event; SYM is a kOfxKey_* name (with or without the
                        prefix), a single character, or a number
  --focus in|out        send the gain or lose focus action
  --draw                send the Draw action and print what the plugin drew
  --draw-out FILE.ppm   rasterise the last Draw over the viewport and write it
  --expect-draws [>|>=]N
                        require the last Draw to record exactly N commands, or at least N

Input (one of; default: a 64x64 ramp):
  --in FILE             P6 PPM or PFM image
  --fill R,G,B,A        constant colour
  --ramp                red ramps left to right, green bottom to top, blue 0.5
  --size WxH            image size for --fill and --ramp (default 64x64)

Output and checks:
  --out FILE            write the result (.ppm 8-bit or .pfm float). With a sequence, a run
                        of # or a %04d field in FILE takes the frame number and every frame
                        is written; without one, only the last frame is
  --expect-param NAME=VALUE
                        require parameter NAME to hold VALUE after the interaction
  --expect [T:]X,Y,R,G,B,A[,TOL]
                        require the output pixel at (X,Y) to match, within TOL (default
                        0.004), at frame T if given, else in the last frame rendered
  --list                list the plugins found and exit
  --describe            print each selected plugin's contexts, clips and params
  --randomize SEED      choose size, origin, padding, depth, components, colour management,
                        tiling, render scale, parameter values and keyframes, optional clips,
                        time and frame range at random from SEED; the equivalent explicit
                        command line is printed as "repro:" before rendering
  --verbose             log every action and suite call of interest
)";

// One --param: a value, or a keyframe when it carries a time.
struct ParamSetting {
  std::string name;
  std::string value;
  std::optional<double> time;
};

// --expect-param NAME=VALUE: what a parameter must hold once the scripted
// interaction has run, which is how a pen drag is checked.
struct ParamExpect {
  std::string name;
  std::string value;
};

struct EffectSpec {
  std::string id;
  std::string context;
  std::vector<ParamSetting> params;
  std::vector<std::pair<std::string, std::string>> clips;
  InteractOptions interact;
  std::vector<ParamExpect> expectParams;
};

struct Options {
  std::vector<std::filesystem::path> paths;
  std::vector<EffectSpec> effects;
  std::optional<std::filesystem::path> in, out;
  std::optional<std::array<float, 4>> fill;
  bool ramp = false;
  int width = 64, height = 64;
  double time = 0;
  std::optional<OfxRangeD> frames;  // --frames: the sequence to render
  int renders = 1;
  std::optional<Components> components;
  std::optional<Depth> depth;
  std::optional<OfxPointI> origin;
  int rowPadding = 0;
  openfx::ColourManagementStyle colourManagement = openfx::ColourManagementStyle::None;
  std::string colourspace;
  RenderOptions render;
  std::optional<unsigned> randomize;
  struct Expect {
    int x, y;
    std::array<float, 4> rgba;
    float tol;
    std::optional<double> time;  // the frame to check, else the last one rendered
  };
  std::vector<Expect> expects;
  bool list = false, describe = false;
};

std::vector<float> parseFloats(const std::string& s) {
  std::vector<float> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t comma = s.find(',', start);
    if (comma == std::string::npos)
      comma = s.size();
    out.push_back(std::stof(s.substr(start, comma - start)));
    start = comma + 1;
  }
  return out;
}

// "WxH", as --size and --tile take it.
std::pair<int, int> parseSize(const std::string& s, const char* flag) {
  size_t x = s.find('x');
  if (x == std::string::npos)
    throw std::runtime_error(std::string(flag) + " needs WxH");
  return {std::stoi(s.substr(0, x)), std::stoi(s.substr(x + 1))};
}

std::pair<std::string, std::string> parseAssignment(const std::string& kv,
                                                    const char* flag) {
  size_t eq = kv.find('=');
  if (eq == std::string::npos)
    throw std::runtime_error(std::string(flag) + " needs NAME=VALUE");
  return {kv.substr(0, eq), kv.substr(eq + 1)};
}

// --param NAME=VALUE, or NAME@TIME=VALUE for a keyframe.
ParamSetting parseParamSetting(const std::string& kv) {
  auto [name, value] = parseAssignment(kv, "--param");
  size_t at = name.find('@');
  if (at == std::string::npos)
    return {name, value, std::nullopt};
  try {
    return {name.substr(0, at), value, std::stod(name.substr(at + 1))};
  } catch (const std::exception&) {
    throw std::runtime_error("--param " + kv + ": @ must be followed by a frame number");
  }
}

// "X,Y", as --pen takes a canonical position.
OfxPointD parsePoint(const std::string& s, const char* flag) {
  auto v = parseFloats(s);
  if (v.size() != 2)
    throw std::runtime_error(std::string(flag) + " needs X,Y");
  return {v[0], v[1]};
}

// --expect-draws N, >N or >=N. ">N" is "at least N + 1", so both forms end
// up as one minimum.
InteractOptions::ExpectDraws parseExpectDraws(const std::string& s) {
  InteractOptions::ExpectDraws want;
  size_t digits = 0;
  bool exclusive = false;
  if (s.rfind(">=", 0) == 0) {
    want.atLeast = true;
    digits = 2;
  } else if (s.rfind('>', 0) == 0) {
    want.atLeast = exclusive = true;
    digits = 1;
  }
  try {
    want.count = std::stoi(s.substr(digits)) + (exclusive ? 1 : 0);
  } catch (const std::exception&) {
    throw std::runtime_error("--expect-draws needs N, >N or >=N");
  }
  return want;
}

// --frames FIRST-LAST, or N for 0 to N-1.
OfxRangeD parseFrameRange(const std::string& s) {
  size_t dash = s.find('-', 1);
  try {
    if (dash == std::string::npos) {
      double n = std::stod(s);
      if (n < 1)
        throw std::runtime_error("--frames N needs at least one frame");
      return {0, n - 1};
    }
    OfxRangeD range{std::stod(s.substr(0, dash)), std::stod(s.substr(dash + 1))};
    if (range.max < range.min)
      throw std::runtime_error("--frames FIRST-LAST needs LAST >= FIRST");
    return range;
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("--frames needs N or FIRST-LAST");
  }
}

Options parseArgs(int argc, char** argv) {
  Options o;
  auto need = [&](int& i, const char* flag) -> std::string {
    if (i + 1 >= argc)
      throw std::runtime_error(std::string(flag) + " needs a value");
    return argv[++i];
  };
  auto current = [&]() -> EffectSpec& {
    if (o.effects.empty())
      o.effects.push_back({});
    return o.effects.back();
  };
  // Every interact option is about an overlay, so each one turns it on.
  auto interact = [&]() -> InteractOptions& {
    current().interact.enabled = true;
    return current().interact;
  };
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--plugin") {
      EffectSpec spec;
      spec.id = need(i, "--plugin");
      o.effects.push_back(std::move(spec));
    } else if (a == "--context")
      current().context = need(i, "--context");
    else if (a == "--param")
      current().params.push_back(parseParamSetting(need(i, "--param")));
    else if (a == "--clip")
      current().clips.push_back(parseAssignment(need(i, "--clip"), "--clip"));
    else if (a == "--components") {
      auto c =
          openfx::pixelComponentsFromName("OfxImageComponent" + need(i, "--components"));
      if (!c)
        throw std::runtime_error("--components needs RGBA, RGB or Alpha");
      o.components = *c;
    } else if (a == "--depth") {
      auto d = openfx::pixelDepthFromName("OfxBitDepth" + need(i, "--depth"));
      if (!d)
        throw std::runtime_error("--depth needs Byte, Short or Float");
      o.depth = *d;
    } else if (a == "--origin") {
      auto v = parseFloats(need(i, "--origin"));
      if (v.size() != 2)
        throw std::runtime_error("--origin needs X,Y");
      o.origin = OfxPointI{int(v[0]), int(v[1])};
    } else if (a == "--colour-management") {
      static const std::map<std::string, openfx::ColourManagementStyle> kStyles = {
          {"none", openfx::ColourManagementStyle::None},
          {"basic", openfx::ColourManagementStyle::Basic},
          {"core", openfx::ColourManagementStyle::Core}};
      auto it = kStyles.find(need(i, "--colour-management"));
      if (it == kStyles.end())
        throw std::runtime_error(
            "--colour-management needs none, basic or core; this host does not implement "
            "the full or OCIO styles");
      o.colourManagement = it->second;
    } else if (a == "--colourspace")
      o.colourspace = need(i, "--colourspace");
    else if (a == "--row-padding")
      o.rowPadding = std::stoi(need(i, "--row-padding"));
    else if (a == "--tiles")
      o.render.tiles = std::max(1, std::stoi(need(i, "--tiles")));
    else if (a == "--tile") {
      auto [w, h] = parseSize(need(i, "--tile"), "--tile");
      o.render.tileWidth = std::max(1, w);
      o.render.tileHeight = std::max(1, h);
    } else if (a == "--check-tiles")
      o.render.checkTiles = true;
    else if (a == "--render-scale") {
      auto v = parseFloats(need(i, "--render-scale"));
      if (v.empty() || v.size() > 2 || v[0] <= 0 || v.back() <= 0)
        throw std::runtime_error("--render-scale needs S or SX,SY, both above 0");
      o.render.renderScale = {v[0], v.back()};
    } else if (a == "--renders")
      o.renders = std::max(1, std::stoi(need(i, "--renders")));
    else if (a == "--in")
      o.in = need(i, "--in");
    else if (a == "--out")
      o.out = need(i, "--out");
    else if (a == "--fill") {
      auto v = parseFloats(need(i, "--fill"));
      if (v.size() != 4)
        throw std::runtime_error("--fill needs R,G,B,A");
      o.fill = {v[0], v[1], v[2], v[3]};
    } else if (a == "--ramp")
      o.ramp = true;
    else if (a == "--size") {
      std::tie(o.width, o.height) = parseSize(need(i, "--size"), "--size");
    } else if (a == "--time")
      o.time = std::stod(need(i, "--time"));
    else if (a == "--frames")
      o.frames = parseFrameRange(need(i, "--frames"));
    else if (a == "--expect") {
      std::string spec = need(i, "--expect");
      std::optional<double> at;
      if (size_t colon = spec.find(':');
          colon != std::string::npos && colon < spec.find(',')) {
        at = std::stod(spec.substr(0, colon));
        spec = spec.substr(colon + 1);
      }
      auto v = parseFloats(spec);
      if (v.size() != 6 && v.size() != 7)
        throw std::runtime_error("--expect needs [T:]X,Y,R,G,B,A[,TOL]");
      o.expects.push_back({int(v[0]),
                           int(v[1]),
                           {v[2], v[3], v[4], v[5]},
                           v.size() == 7 ? v[6] : 0.004f,
                           at});
    } else if (a == "--expect-param") {
      auto [name, value] = parseAssignment(need(i, "--expect-param"), "--expect-param");
      current().expectParams.push_back({name, value});
    } else if (a == "--interact")
      current().interact.enabled = true;
    else if (a == "--viewport") {
      auto [w, h] = parseSize(need(i, "--viewport"), "--viewport");
      interact().viewport = OfxPointI{w, h};
    } else if (a == "--pen") {
      static const std::map<std::string, InteractEvent::Kind> kKinds = {
          {"down", InteractEvent::Kind::PenDown},
          {"move", InteractEvent::Kind::PenMotion},
          {"up", InteractEvent::Kind::PenUp}};
      auto it = kKinds.find(need(i, "--pen"));
      if (it == kKinds.end())
        throw std::runtime_error("--pen needs down, move or up");
      InteractEvent e;
      e.kind = it->second;
      e.position = parsePoint(need(i, "--pen"), "--pen");
      interact().events.push_back(e);
    } else if (a == "--key") {
      static const std::map<std::string, InteractEvent::Kind> kKinds = {
          {"down", InteractEvent::Kind::KeyDown},
          {"up", InteractEvent::Kind::KeyUp},
          {"repeat", InteractEvent::Kind::KeyRepeat}};
      auto it = kKinds.find(need(i, "--key"));
      if (it == kKinds.end())
        throw std::runtime_error("--key needs down, up or repeat");
      InteractEvent e;
      e.kind = it->second;
      std::string sym = need(i, "--key");
      if (!parseKeySym(sym, e.keySym, e.keyString))
        throw std::runtime_error("--key " + sym + " is not a kOfxKey_* name or a number");
      interact().events.push_back(e);
    } else if (a == "--focus") {
      std::string where = need(i, "--focus");
      if (where != "in" && where != "out")
        throw std::runtime_error("--focus needs in or out");
      InteractEvent e;
      e.kind =
          where == "in" ? InteractEvent::Kind::GainFocus : InteractEvent::Kind::LoseFocus;
      interact().events.push_back(e);
    } else if (a == "--draw") {
      InteractEvent e;
      e.kind = InteractEvent::Kind::Draw;
      interact().events.push_back(e);
    } else if (a == "--draw-out")
      interact().drawOut = std::filesystem::path(need(i, "--draw-out"));
    else if (a == "--expect-draws")
      interact().expectDraws = parseExpectDraws(need(i, "--expect-draws"));
    else if (a == "--randomize")
      o.randomize = static_cast<unsigned>(std::stoul(need(i, "--randomize")));
    else if (a == "--list")
      o.list = true;
    else if (a == "--describe")
      o.describe = true;
    else if (a == "--verbose" || a == "-v")
      openfx::Logger::setLevel(openfx::Logger::Level::Debug);
    else if (a == "--help" || a == "-h") {
      std::cout << kUsage;
      std::exit(0);
    } else if (a.starts_with('-'))
      throw std::runtime_error("unknown option " + a);
    else
      o.paths.emplace_back(a);
  }
  if (o.paths.empty())
    throw std::runtime_error("no plugin path given (--help for usage)");
  if (!o.colourspace.empty()) {
    if (o.colourManagement == openfx::ColourManagementStyle::None)
      throw std::runtime_error("--colourspace needs --colour-management basic or core");
    if (!openfx::colourspaceAllowedIn(o.colourspace, o.colourManagement))
      throw std::runtime_error(
          "--colourspace " + o.colourspace + " is not a colourspace the " +
          std::string(openfx::colourManagementStyleName(o.colourManagement)) +
          " style offers");
  }
  return o;
}

std::string pickContext(const EffectDescriptor& desc, const std::string& requested) {
  auto contexts = desc.supportedContexts();
  if (!requested.empty()) {
    if (std::find(contexts.begin(), contexts.end(), requested) == contexts.end())
      throw std::runtime_error(desc.plugin().id() + " does not support context " +
                               requested);
    return requested;
  }
  for (const char* c : {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral,
                        kOfxImageEffectContextGenerator})
    if (std::find(contexts.begin(), contexts.end(), c) != contexts.end())
      return c;
  throw std::runtime_error(desc.plugin().id() +
                           " supports none of the filter, general or generator contexts");
}

// An image for --clip: a file, fill:R,G,B,A, ramp, or the main input.
std::shared_ptr<ImageBuffer> clipImage(const std::string& source,
                                       const std::shared_ptr<ImageBuffer>& input) {
  if (source == "input")
    return input;
  if (source == "ramp")
    return rampImage(input->width(), input->height());
  if (source.starts_with("fill:")) {
    auto v = parseFloats(source.substr(5));
    if (v.size() != 4)
      throw std::runtime_error("--clip fill needs fill:R,G,B,A");
    return solidImage(input->width(), input->height(), {v[0], v[1], v[2], v[3]});
  }
  return readImage(source);
}

// The clip a filter-style effect reads its main input from.
Clip* mainInput(EffectInstance& inst) {
  if (Clip* c = inst.clip(kOfxImageEffectSimpleSourceClipName))
    return c;
  for (auto& c : inst.clips())
    if (!c->isOutput() && !c->props().getInt(kOfxImageClipPropIsMask))
      return c.get();
  return nullptr;
}

std::string fmt(double v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

// The file to write one frame of a sequence to: a run of '#' or a %04d-style
// field in the name takes the frame number. Empty if the name has neither, so
// the caller knows only one frame can be written.
std::string framePath(const std::string& name, double time) {
  size_t start = std::string::npos, end = 0, width = 0;
  if (size_t hash = name.find('#'); hash != std::string::npos) {
    start = hash;
    end = std::min(name.find_first_not_of('#', hash), name.size());
    width = end - start;
  } else {
    for (size_t at = name.find('%'); at != std::string::npos;
         at = name.find('%', at + 1)) {
      size_t after = name.find_first_not_of("0123456789", at + 1);
      if (after == std::string::npos || name[after] != 'd')
        continue;
      start = at;
      end = after + 1;
      width = std::stoul("0" + name.substr(at + 1, after - at - 1));
      break;
    }
    if (start == std::string::npos)
      return "";
  }
  long frame = std::lround(time);
  std::string digits = std::to_string(std::labs(frame)), sign = frame < 0 ? "-" : "";
  while (digits.size() + sign.size() < width) digits.insert(digits.begin(), '0');
  return name.substr(0, start) + sign + digits + name.substr(end);
}

// The pixel checks that apply to this frame: those naming its time, and, in
// the last frame, those that name no time at all.
int checkExpects(const std::vector<Options::Expect>& expects, const ImageBuffer& image,
                 double time, bool lastFrame) {
  int failures = 0;
  for (const auto& e : expects) {
    if (e.time ? std::fabs(*e.time - time) > 1e-6 : !lastFrame)
      continue;
    const OfxRectI& b = image.bounds();
    if (e.x < b.x1 || e.x >= b.x2 || e.y < b.y1 || e.y >= b.y2) {
      openfx::Logger::error("expect: pixel ({},{}) is outside the output bounds", e.x,
                            e.y);
      ++failures;
      continue;
    }
    auto p = image.pixel(e.x, e.y);
    bool match = true;
    for (int c = 0; c < 4; ++c) match = match && std::fabs(p[c] - e.rgba[c]) <= e.tol;
    std::cout << "   pixel (" << e.x << "," << e.y << ") = " << p[0] << "," << p[1] << ","
              << p[2] << "," << p[3] << (match ? "  ok" : "  MISMATCH") << " (expected "
              << e.rgba[0] << "," << e.rgba[1] << "," << e.rgba[2] << "," << e.rgba[3]
              << ")\n";
    if (!match)
      ++failures;
  }
  return failures;
}

// --expect-param: what a parameter holds once the scripted interaction has
// run, which is how a pen drag that moves a parameter is checked.
int checkParamExpects(const std::vector<ParamExpect>& expects, EffectInstance& inst,
                      double time) {
  constexpr double kTolerance = 1e-4;
  int failures = 0;
  for (const auto& e : expects) {
    Param* p = inst.params().find(e.name);
    if (!p) {
      openfx::Logger::error("expect-param: no parameter named {}", e.name);
      ++failures;
      continue;
    }
    ParamValue want;
    if (!parseParam(*p, e.value, want)) {
      openfx::Logger::error("expect-param: cannot parse \"{}\" for {}", e.value, e.name);
      ++failures;
      continue;
    }
    const ParamValue got = p->value(time);
    bool match = got.str == want.str && got.ints == want.ints &&
                 got.doubles.size() == want.doubles.size();
    for (size_t i = 0; match && i < want.doubles.size(); ++i)
      match = std::fabs(got.doubles[i] - want.doubles[i]) <= kTolerance;
    std::cout << "   param " << e.name << " = " << paramValueString(*p, time)
              << (match ? "  ok" : "  MISMATCH") << " (expected " << e.value << ")\n";
    if (!match)
      ++failures;
  }
  return failures;
}

// ---------------------------------------------------------------------------
// Randomisation: host choices and parameter values a plugin should survive.
// ---------------------------------------------------------------------------

class Randomizer {
 public:
  explicit Randomizer(unsigned seed) : rng_(seed) {}

  int intIn(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
  double realIn(double lo, double hi) {
    return std::uniform_real_distribution<double>(lo, hi)(rng_);
  }
  bool chance(double p) { return realIn(0, 1) < p; }
  template <typename T>
  const T& pick(const std::vector<T>& v) {
    return v[intIn(0, int(v.size()) - 1)];
  }

  // Global choices, before any plugin is known.
  void image(Options& o) {
    o.width = chance(0.15) ? intIn(1, 4) : intIn(1, 128);
    o.height = chance(0.15) ? intIn(1, 4) : intIn(1, 128);
    if (chance(0.5))
      o.origin = OfxPointI{intIn(-40, 40), intIn(-40, 40)};
    if (chance(0.3))
      o.rowPadding = pick(std::vector<int>{1, 3, 4, 16, 64});
    // Tiles that do not divide the window evenly are the case worth testing, so
    // take a count rather than a size, and always check the frame it assembles.
    if (chance(0.4)) {
      o.render.tiles = intIn(2, 5);
      o.render.checkTiles = true;
    }
    if (chance(0.3)) {
      static const std::vector<double> scales = {0.5, 0.25};
      double s = pick(scales);
      o.render.renderScale = {s, chance(0.25) ? pick(scales) : s};
    }
    if (chance(0.3))
      o.time = chance(0.5) ? intIn(-5, 100) : realIn(-5, 100);
    if (chance(0.25)) {  // a short sequence, which also keys the parameters
      double first = intIn(-3, 8);
      o.frames = OfxRangeD{first, first + intIn(1, 3)};
    }
    if (chance(0.3))
      o.renders = intIn(2, 3);
    if (chance(0.5))
      o.fill = std::array<float, 4>{float(realIn(-0.5, 1.5)), float(realIn(-0.5, 1.5)),
                                    float(realIn(0, 1)), float(realIn(0, 1))};
    else
      o.ramp = true;
    // Host-side format preferences; a plugin that lacks them gets its own first choice.
    o.depth = pick(std::vector<Depth>{Depth::Byte, Depth::Short, Depth::Float});
    o.components = pick(
        std::vector<Components>{Components::RGBA, Components::RGB, Components::Alpha});
    // The colour management the host advertises, which changes what a plugin
    // describes as well as what it is told at render time.
    o.colourManagement = pick(std::vector<openfx::ColourManagementStyle>{
        openfx::ColourManagementStyle::None, openfx::ColourManagementStyle::Basic,
        openfx::ColourManagementStyle::Core});
  }

  // Per-effect choices, once the plugin is described. Over a frame range the
  // parameters that animate may also be keyed, at frames inside it.
  void effect(EffectDescriptor& desc, EffectSpec& spec, const Project& project,
              const std::optional<OfxRangeD>& frames) {
    for (const auto& c : desc.clips()) {
      if (c->isOutput() || c->name() == kOfxImageEffectSimpleSourceClipName)
        continue;
      if (!c->props().getInt(kOfxImageClipPropOptional) || chance(0.6))
        spec.clips.emplace_back(c->name(), chance(0.5)
                                               ? "ramp"
                                               : "fill:" + fmt(realIn(0, 1)) +
                                                     ",0.5,0.5," + fmt(realIn(0, 1)));
    }
    for (const auto& p : desc.params().params()) {
      if (p->kind() == Param::Kind::None || !chance(0.6))
        continue;
      spec.params.push_back({p->name(), value(*p), std::nullopt});
      if (!frames || !p->animates() || !chance(0.5))
        continue;
      for (int key = 0, keys = intIn(1, 2); key < keys; ++key)
        spec.params.push_back(
            {p->name(), value(*p), realIn(frames->min - 1, frames->max + 1)});
    }
    bool drawSuiteOverlay = false;
    if (openfx::host::overlayEntryPoint(desc, &drawSuiteOverlay) && drawSuiteOverlay)
      overlay(spec, project);
  }

  // A short pen drag across an effect that has an overlay, drawn either side
  // of it. The positions are canonical and may fall outside the frame.
  void overlay(EffectSpec& spec, const Project& project) {
    spec.interact.enabled = true;
    auto point = [&] {
      return OfxPointD{realIn(project.originX - 8, project.originX + project.width + 8),
                       realIn(project.originY - 8, project.originY + project.height + 8)};
    };
    auto add = [&](InteractEvent::Kind kind, OfxPointD at) {
      InteractEvent e;
      e.kind = kind;
      e.position = at;
      spec.interact.events.push_back(e);
    };
    add(InteractEvent::Kind::Draw, {});
    if (chance(0.8)) {
      add(InteractEvent::Kind::PenDown, point());
      for (int move = 0, moves = intIn(1, 3); move < moves; ++move)
        add(InteractEvent::Kind::PenMotion, point());
      add(InteractEvent::Kind::PenUp, point());
    }
    add(InteractEvent::Kind::Draw, {});
  }

 private:
  std::string value(const Param& p) {
    const PropertySet& props = p.props();
    if (p.kind() == Param::Kind::String) {
      if (p.type() == kOfxParamTypeStrChoice) {  // always a declared enum: a host never
                                                 // passes anything else
        int n = 0;
        props.dimension(kOfxParamPropChoiceEnum, &n);
        return n > 0 ? props.getString(kOfxParamPropChoiceEnum, intIn(0, n - 1)) : "";
      }
      static const std::vector<std::string> strings = {"",
                                                       "x",
                                                       "hello world",
                                                       std::string(300, 'a'),
                                                       "/no/such/file",
                                                       "%s%n",
                                                       "\xc3\xa9\xe2\x82\xac"};
      return pick(strings);
    }
    std::string out;
    for (int i = 0; i < p.arity(); ++i) {
      std::string part;
      if (p.kind() == Param::Kind::Double) {
        double lo = props.getDouble(kOfxParamPropDisplayMin, i, -1e300),
               hi = props.getDouble(kOfxParamPropDisplayMax, i, 1e300);
        if (!(hi - lo < 1e6)) {  // no usable display range: a plausible one
          lo = std::max(lo, -10.0);
          hi = std::min(hi, 10.0);
        }
        double hardLo = props.getDouble(kOfxParamPropMin, i, -1e300),
               hardHi = props.getDouble(kOfxParamPropMax, i, 1e300);
        double v = chance(0.1)   ? hardLo
                   : chance(0.1) ? hardHi
                   : chance(0.1) ? 0.0
                                 : realIn(lo, hi);
        if (!std::isfinite(v) || std::fabs(v) > 1e300)
          v = v < 0 ? -1e6 : 1e6;
        part = fmt(v);
      } else if (p.type() == kOfxParamTypeBoolean) {
        part = std::to_string(intIn(0, 1));
      } else if (p.type() == kOfxParamTypeChoice) {
        int n = 0;
        props.dimension(kOfxParamPropChoiceOption, &n);
        part = std::to_string(n > 0 && !chance(0.05)
                                  ? intIn(0, n - 1)
                                  : intIn(-1, n + 1));  // occasionally out of range
      } else {
        int lo = props.getInt(kOfxParamPropDisplayMin, i, INT_MIN),
            hi = props.getInt(kOfxParamPropDisplayMax, i, INT_MAX);
        if (lo <= -100000 || hi >= 100000) {
          lo = std::max(lo, -100);
          hi = std::min(hi, 100);
        }
        int v = chance(0.1)   ? props.getInt(kOfxParamPropMin, i, INT_MIN)
                : chance(0.1) ? props.getInt(kOfxParamPropMax, i, INT_MAX)
                              : intIn(lo, hi);
        part = std::to_string(v);
      }
      out += (i ? "," : "") + part;
    }
    return out;
  }

  std::mt19937 rng_;
};

// The explicit command line equivalent to this run, for reproducing a random one.
std::string reproLine(const Options& o, const std::vector<EffectSpec>& specs) {
  std::ostringstream os;
  os << "repro: ofxtesthost";
  for (const auto& p : o.paths) os << " " << p.string();
  os << " --size " << o.width << "x" << o.height;
  if (o.origin)
    os << " --origin " << o.origin->x << "," << o.origin->y;
  if (o.rowPadding)
    os << " --row-padding " << o.rowPadding;
  if (o.render.tiles > 1)
    os << " --tiles " << o.render.tiles;
  if (o.render.tileWidth)
    os << " --tile " << o.render.tileWidth << "x" << o.render.tileHeight;
  if (o.render.checkTiles)
    os << " --check-tiles";
  if (o.render.scaled())
    os << " --render-scale " << fmt(o.render.renderScale.x) << ","
       << fmt(o.render.renderScale.y);
  if (o.depth)
    os << " --depth "
       << std::string(openfx::pixelDepthName(*o.depth))
              .substr(std::strlen("OfxBitDepth"));
  if (o.components)
    os << " --components "
       << std::string(openfx::pixelComponentsName(*o.components))
              .substr(std::strlen("OfxImageComponent"));
  if (o.colourManagement != openfx::ColourManagementStyle::None)
    os << " --colour-management "
       << (o.colourManagement == openfx::ColourManagementStyle::Basic ? "basic" : "core");
  if (!o.colourspace.empty())
    os << " --colourspace " << o.colourspace;
  if (o.in)
    os << " --in " << o.in->string();
  else if (o.fill)
    os << " --fill " << (*o.fill)[0] << "," << (*o.fill)[1] << "," << (*o.fill)[2] << ","
       << (*o.fill)[3];
  else
    os << " --ramp";
  if (o.time != 0)
    os << " --time " << o.time;
  if (o.frames)
    os << " --frames " << fmt(o.frames->min) << "-" << fmt(o.frames->max);
  if (o.renders != 1)
    os << " --renders " << o.renders;
  for (const auto& s : specs) {
    os << " --plugin " << s.id;
    if (!s.context.empty())
      os << " --context " << s.context;
    for (const auto& p : s.params)
      os << " --param '" << p.name << (p.time ? "@" + fmt(*p.time) : "") << "=" << p.value
         << "'";
    for (const auto& [n, v] : s.clips) os << " --clip '" << n << "=" << v << "'";
    if (s.interact.enabled)
      os << " --interact";
    if (s.interact.viewport)
      os << " --viewport " << s.interact.viewport->x << "x" << s.interact.viewport->y;
    for (const auto& e : s.interact.events) {
      switch (e.kind) {
        case InteractEvent::Kind::PenDown:
        case InteractEvent::Kind::PenMotion:
        case InteractEvent::Kind::PenUp:
          os << " --pen "
             << (e.kind == InteractEvent::Kind::PenDown     ? "down"
                 : e.kind == InteractEvent::Kind::PenMotion ? "move"
                                                            : "up")
             << " " << fmt(e.position.x) << "," << fmt(e.position.y);
          break;
        case InteractEvent::Kind::KeyDown:
        case InteractEvent::Kind::KeyUp:
        case InteractEvent::Kind::KeyRepeat:
          os << " --key "
             << (e.kind == InteractEvent::Kind::KeyDown ? "down"
                 : e.kind == InteractEvent::Kind::KeyUp ? "up"
                                                        : "repeat")
             << " " << e.keySym;
          break;
        case InteractEvent::Kind::GainFocus:
          os << " --focus in";
          break;
        case InteractEvent::Kind::LoseFocus:
          os << " --focus out";
          break;
        case InteractEvent::Kind::Draw:
          os << " --draw";
          break;
      }
    }
  }
  return os.str();
}

int run(Options o) {
  // Load every plugin binary and index the image effects by identifier.
  std::vector<std::unique_ptr<openfx::host::PluginBinary>> bundles;
  for (const auto& p : o.paths)
    for (auto& b : openfx::host::PluginBinary::load(p)) bundles.push_back(std::move(b));
  std::vector<std::unique_ptr<Plugin>> plugins;
  for (const auto& b : bundles)
    for (OfxPlugin* p : b->plugins()) {
      auto plugin = std::make_unique<Plugin>(p, *b);
      if (plugin->isImageEffect())
        plugins.push_back(std::move(plugin));
      else
        openfx::Logger::info("skipping {} (api {})", p->pluginIdentifier, p->pluginApi);
    }
  if (plugins.empty())
    throw std::runtime_error("no image effect plugins found");

  if (o.list) {
    for (const auto& p : plugins)
      std::cout << p->id() << " v" << p->versionMajor() << "." << p->versionMinor()
                << "  (" << p->bundlePath().string() << ")\n";
    return 0;
  }

  std::optional<Randomizer> random;
  if (o.randomize) {
    random.emplace(*o.randomize);
    random->image(o);
  }

  std::vector<EffectSpec> specs = o.effects;
  if (specs.empty())
    specs.push_back({});
  if (specs[0].id.empty())
    specs[0].id = plugins.front()->id();

  // The style is a property of the host itself, so it must be settled before
  // any plugin is handed the OfxHost struct.
  setColourManagementStyle(o.colourManagement);
  // The frames to render: the whole sequence, or the single --time frame.
  const OfxRangeD range = o.frames.value_or(OfxRangeD{o.time, o.time});
  const double frameStep = 1.0;
  const int frameCount = int(std::floor(range.max - range.min + 1e-9)) + 1;

  Project project;
  project.width = o.width;
  project.height = o.height;
  project.preferredComponents = o.components;
  project.preferredDepth = o.depth;
  project.colourManagement = o.colourManagement;
  project.colourspace = o.colourspace;
  project.firstFrame = range.min;
  project.frames = frameCount;
  project.sequential = o.frames.has_value();
  openfx::host::timeline() = {range.min, range.max, range.min};

  // Source image.
  std::shared_ptr<ImageBuffer> image;
  if (o.in)
    image = readImage(*o.in);
  else if (o.fill)
    image = solidImage(o.width, o.height, *o.fill);
  else
    image = rampImage(o.width, o.height);
  if (o.origin || o.rowPadding)
    image = image->reframed(o.origin.value_or(OfxPointI{0, 0}), o.rowPadding);
  project.width = image->width();
  project.height = image->height();
  project.originX = image->bounds().x1;
  project.originY = image->bounds().y1;

  // Phase 1: describe and instantiate every effect, so the whole run is
  // known (and printable) before any plugin renders.
  std::vector<std::unique_ptr<EffectDescriptor>> descriptors;
  std::vector<std::unique_ptr<EffectInstance>> instances;
  // Declared after the instances so an overlay is destroyed before the effect
  // it belongs to, which is the order the specification requires.
  std::vector<std::unique_ptr<Overlay>> overlays;
  int failures = 0;
  for (auto& spec : specs) {
    auto it = std::find_if(plugins.begin(), plugins.end(),
                           [&](auto& p) { return p->id() == spec.id; });
    if (it == plugins.end())
      throw std::runtime_error("no plugin with identifier " + spec.id);
    Plugin& plugin = **it;
    std::cout << "== " << plugin.id() << " v" << plugin.versionMajor() << "."
              << plugin.versionMinor() << "\n";

    plugin.load(host());
    auto global = plugin.describe();
    if (random && spec.context.empty()) {
      auto contexts = global->supportedContexts();
      std::vector<std::string> usable;
      for (const char* c : {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral,
                            kOfxImageEffectContextGenerator})
        if (std::find(contexts.begin(), contexts.end(), c) != contexts.end())
          usable.push_back(c);
      if (!usable.empty())
        spec.context = random->pick(usable);
    }
    spec.context = pickContext(*global, spec.context);
    auto desc = plugin.describeInContext(*global, spec.context);
    if (o.describe)
      std::cout << describeEffect(*desc);
    if (random)
      random->effect(*desc, spec, project, o.frames);

    auto inst = std::make_unique<EffectInstance>(*desc, project);
    inst->create();
    // The overlay, if the plugin has one and it was asked for: it belongs to
    // the instance, so it is created after it and destroyed before it.
    std::unique_ptr<Overlay> overlay;
    if (spec.interact.enabled) {
      overlay = Overlay::create(plugin, *desc, *inst, project, spec.interact);
      if (!overlay)
        openfx::Logger::info("{} has no overlay interact this host can drive", spec.id);
    }
    // A host brackets the period its user can edit an instance, which is where
    // every --param change belongs.
    openfx::host::requireSuccess(inst->beginInstanceEdit(), "BeginInstanceEdit failed");
    for (const auto& p : spec.params) {
      inst->setParam(p.name, p.value, p.time);
      const Param& param = *inst->params().find(p.name);
      openfx::Logger::info("set {}{} = {}", p.name, p.time ? "@" + fmt(*p.time) : "",
                           paramValueString(param, p.time.value_or(range.min)));
      // An interact slaved to the parameter must be redrawn now it has changed.
      if (overlay)
        overlay->parameterChanged(p.name);
    }
    openfx::host::requireSuccess(inst->endInstanceEdit(), "EndInstanceEdit failed");
    inst->updateClipPreferences();
    if (overlay)
      failures += overlay->runScript();
    failures += checkParamExpects(spec.expectParams, *inst, range.min);
    // The effect's own frame range, which only a general or generator effect has.
    if (spec.context == kOfxImageEffectContextGeneral ||
        spec.context == kOfxImageEffectContextGenerator) {
      if (auto domain = inst->getTimeDomain())
        openfx::Logger::info("time domain: {} .. {}", domain->min, domain->max);
      else
        openfx::Logger::debug("no time domain: the host's own frame range stands");
    }
    inst->setRenderOptions(o.render);

    descriptors.push_back(std::move(global));
    descriptors.push_back(std::move(desc));
    instances.push_back(std::move(inst));
    overlays.push_back(std::move(overlay));
  }
  if (random)
    std::cout << reproLine(o, specs) << '\n' << std::flush;  // a crash must not lose it

  // Phase 2: render the chain, once per frame of the sequence. The clips a
  // --clip attaches do not change from frame to frame, so they are connected
  // once; the main input is connected per frame because in a chain it is the
  // frame the plugin before just rendered.
  std::vector<Clip*> mainInputs;
  for (size_t i = 0; i < instances.size(); ++i) {
    EffectInstance& inst = *instances[i];
    const EffectSpec& spec = specs[i];
    mainInputs.push_back(mainInput(inst));
    if (!mainInputs.back() && spec.context != kOfxImageEffectContextGenerator)
      openfx::Logger::warn("{} has no input clip to connect", spec.id);
    for (const auto& [name, source] : spec.clips) {
      inst.connectInput(name, clipImage(source, image));
      openfx::Logger::info("connected clip {} to {}", name, source);
    }
    if (o.frames)
      inst.beginSequence(range, frameStep);
  }

  const std::shared_ptr<ImageBuffer> source = image;
  for (int frame = 0; frame < frameCount; ++frame) {
    const double time = range.min + frame * frameStep;
    openfx::host::timeline().current = time;
    image = source;
    for (size_t i = 0; i < instances.size(); ++i) {
      EffectInstance& inst = *instances[i];
      if (mainInputs[i])
        inst.connectInput(mainInputs[i]->name(), image);
      auto start = std::chrono::steady_clock::now();
      for (int r = 0; r < o.renders; ++r) image = inst.renderFrame(time);
      auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
      Clip* out = inst.clip(kOfxImageEffectOutputClipName);
      std::cout << "   " << specs[i].id << ": "
                << (o.frames ? "frame " + fmt(time) + ": " : "") << "rendered "
                << image->width() << "x" << image->height() << " "
                << openfx::pixelComponentsName(out->components()) << " "
                << openfx::pixelDepthName(out->depth())
                << (o.renders > 1 ? " x" + std::to_string(o.renders) : "") << " in "
                << std::lround(ms) << " ms\n";
    }

    const bool lastFrame = frame + 1 == frameCount;
    if (o.out) {
      // A frame pattern writes every frame; without one, only the last.
      std::string path = framePath(o.out->string(), time);
      if (path.empty() && lastFrame)
        path = o.out->string();
      if (!path.empty()) {
        writeImage(path, *image);
        std::cout << "   wrote " << path << "\n";
      }
    }
    failures += checkExpects(o.expects, *image, time, lastFrame);
  }

  for (auto& inst : instances) inst->endSequence();

  // A check on a frame that was never rendered would otherwise pass silently.
  for (const auto& e : o.expects) {
    if (!e.time)
      continue;
    const double frame = (*e.time - range.min) / frameStep;
    if (frame < -1e-6 || frame > frameCount - 1 + 1e-6 ||
        std::fabs(frame - std::round(frame)) > 1e-6) {
      openfx::Logger::error("expect: frame {} is not one of the frames rendered",
                            *e.time);
      ++failures;
    }
  }

  // Tear down in reverse: overlays before instances, instances before
  // descriptors, then unload.
  overlays.clear();
  instances.clear();
  descriptors.clear();
  for (auto& p : plugins) p->unload();
  return failures ? 1 : 0;
}

#ifndef _WIN32
// A plugin crash takes the host down; at least say which plugin and action.
void crashHandler(int sig) {
  const char* action = Plugin::currentAction;
  const char* plugin = Plugin::currentPlugin;
  auto put = [](const char* s) { (void)!write(2, s, std::char_traits<char>::length(s)); };
  put("\nFATAL: signal ");
  put(sig == SIGSEGV   ? "SIGSEGV"
      : sig == SIGBUS  ? "SIGBUS"
      : sig == SIGABRT ? "SIGABRT"
      : sig == SIGTRAP ? "SIGTRAP"
                       : "?");
  if (action) {
    put(" while ");
    put(plugin ? plugin : "?");
    put(" was handling ");
    put(action);
  } else {
    put(" in the host");
  }
  put("\n");
  void* frames[48];
  int n = backtrace(frames, 48);
  backtrace_symbols_fd(frames, n, 2);
  signal(sig, SIG_DFL);
  raise(sig);
}

void installCrashHandler() {
  for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP})
    signal(sig, crashHandler);
}
#else
void installCrashHandler() {}
#endif

}  // namespace

// Host messages go to stderr with a prefix per level, which fuzz.py relies on.
void logToStderr(openfx::Logger::Level level, std::chrono::system_clock::time_point,
                 const std::string& message) {
  using Level = openfx::Logger::Level;
  const char* prefix = level == Level::Debug     ? "  . "
                       : level == Level::Warning ? "  ! "
                       : level == Level::Error   ? "ERROR: "
                                                 : "  ";
  std::cerr << prefix << message << "\n";
}

int main(int argc, char** argv) {
  try {
    installCrashHandler();
    openfx::Logger::setLogHandler(logToStderr);
    return run(parseArgs(argc, argv));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "ERROR: %s\n",
                 e.what());  // same prefix as the log handler, nothing that can throw
    return 2;
  }
}
