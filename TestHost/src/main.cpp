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
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "Effect.h"
#include "Host.h"
#include "ImageIO.h"

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
  --clip NAME=SOURCE    attach an image to clip NAME of the most recent --plugin. SOURCE is an
                        image file, fill:R,G,B,A, ramp, or input (the effect's main input image)

Host choices the spec leaves open (default: what each plugin lists first):
  --components TYPE     negotiate RGBA, RGB or Alpha for every clip that supports it
  --depth TYPE          negotiate Byte, Short or Float if the plugin supports it
  --origin X,Y          place the input image's bounds at (X,Y) instead of (0,0)
  --row-padding N       add N unused bytes to every image row
  --renders N           render the frame N times through the same instance (default 1)
  --time T              frame to render (default 0)

Input (one of; default: a 64x64 ramp):
  --in FILE             P6 PPM or PFM image
  --fill R,G,B,A        constant colour
  --ramp                red ramps left to right, green bottom to top, blue 0.5
  --size WxH            image size for --fill and --ramp (default 64x64)

Output and checks:
  --out FILE            write the result (.ppm 8-bit or .pfm float)
  --expect X,Y,R,G,B,A[,TOL]
                        require the output pixel at (X,Y) to match, within TOL (default 0.004)
  --list                list the plugins found and exit
  --describe            print each selected plugin's contexts, clips and params
  --randomize SEED      choose size, origin, padding, depth, components, parameter values,
                        optional clips and time at random from SEED; the equivalent explicit
                        command line is printed as "repro:" before rendering
  --verbose             log every action and suite call of interest
)";

struct EffectSpec {
  std::string id;
  std::string context;
  std::vector<std::pair<std::string, std::string>> params;
  std::vector<std::pair<std::string, std::string>> clips;
};

struct Options {
  std::vector<std::filesystem::path> paths;
  std::vector<EffectSpec> effects;
  std::optional<std::filesystem::path> in, out;
  std::optional<std::array<float, 4>> fill;
  bool ramp = false;
  int width = 64, height = 64;
  double time = 0;
  int renders = 1;
  std::optional<Components> components;
  std::optional<Depth> depth;
  std::optional<OfxPointI> origin;
  int rowPadding = 0;
  std::optional<unsigned> randomize;
  struct Expect {
    int x, y;
    std::array<float, 4> rgba;
    float tol;
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

std::pair<std::string, std::string> parseAssignment(const std::string& kv,
                                                    const char* flag) {
  size_t eq = kv.find('=');
  if (eq == std::string::npos)
    throw std::runtime_error(std::string(flag) + " needs NAME=VALUE");
  return {kv.substr(0, eq), kv.substr(eq + 1)};
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
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--plugin")
      o.effects.push_back({need(i, "--plugin"), "", {}, {}});
    else if (a == "--context")
      current().context = need(i, "--context");
    else if (a == "--param")
      current().params.push_back(parseAssignment(need(i, "--param"), "--param"));
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
    } else if (a == "--row-padding")
      o.rowPadding = std::stoi(need(i, "--row-padding"));
    else if (a == "--renders")
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
      std::string s = need(i, "--size");
      size_t x = s.find('x');
      if (x == std::string::npos)
        throw std::runtime_error("--size needs WxH");
      o.width = std::stoi(s.substr(0, x));
      o.height = std::stoi(s.substr(x + 1));
    } else if (a == "--time")
      o.time = std::stod(need(i, "--time"));
    else if (a == "--expect") {
      auto v = parseFloats(need(i, "--expect"));
      if (v.size() != 6 && v.size() != 7)
        throw std::runtime_error("--expect needs X,Y,R,G,B,A[,TOL]");
      o.expects.push_back({int(v[0]),
                           int(v[1]),
                           {v[2], v[3], v[4], v[5]},
                           v.size() == 7 ? v[6] : 0.004f});
    } else if (a == "--randomize")
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
    if (chance(0.3))
      o.time = chance(0.5) ? intIn(-5, 100) : realIn(-5, 100);
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
  }

  // Per-effect choices, once the plugin is described.
  void effect(const EffectDescriptor& desc, EffectSpec& spec) {
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
      spec.params.emplace_back(p->name(), value(*p));
    }
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
  if (o.depth)
    os << " --depth "
       << std::string(openfx::pixelDepthName(*o.depth))
              .substr(std::strlen("OfxBitDepth"));
  if (o.components)
    os << " --components "
       << std::string(openfx::pixelComponentsName(*o.components))
              .substr(std::strlen("OfxImageComponent"));
  if (o.in)
    os << " --in " << o.in->string();
  else if (o.fill)
    os << " --fill " << (*o.fill)[0] << "," << (*o.fill)[1] << "," << (*o.fill)[2] << ","
       << (*o.fill)[3];
  else
    os << " --ramp";
  if (o.time != 0)
    os << " --time " << o.time;
  if (o.renders != 1)
    os << " --renders " << o.renders;
  for (const auto& s : specs) {
    os << " --plugin " << s.id;
    if (!s.context.empty())
      os << " --context " << s.context;
    for (const auto& [n, v] : s.params) os << " --param '" << n << "=" << v << "'";
    for (const auto& [n, v] : s.clips) os << " --clip '" << n << "=" << v << "'";
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

  Project project;
  project.width = o.width;
  project.height = o.height;
  project.preferredComponents = o.components;
  project.preferredDepth = o.depth;
  openfx::host::timeline().current = o.time;

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
      random->effect(*desc, spec);

    auto inst = std::make_unique<EffectInstance>(*desc, project);
    inst->create();
    for (const auto& [name, value] : spec.params) {
      inst->setParam(name, value);
      openfx::Logger::info("set {} = {}", name,
                           paramValueString(*inst->params().find(name)));
    }
    inst->updateClipPreferences();

    descriptors.push_back(std::move(global));
    descriptors.push_back(std::move(desc));
    instances.push_back(std::move(inst));
  }
  if (random)
    std::cout << reproLine(o, specs) << '\n' << std::flush;  // a crash must not lose it

  // Phase 2: render the chain.
  for (size_t i = 0; i < instances.size(); ++i) {
    EffectInstance& inst = *instances[i];
    const EffectSpec& spec = specs[i];
    if (Clip* in = mainInput(inst))
      inst.connectInput(in->name(), image);
    else if (spec.context != kOfxImageEffectContextGenerator)
      openfx::Logger::warn("{} has no input clip to connect", spec.id);
    for (const auto& [name, source] : spec.clips) {
      inst.connectInput(name, clipImage(source, image));
      openfx::Logger::info("connected clip {} to {}", name, source);
    }

    auto start = std::chrono::steady_clock::now();
    for (int r = 0; r < o.renders; ++r) image = inst.renderFrame(o.time);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                        start)
                  .count();
    Clip* out = inst.clip(kOfxImageEffectOutputClipName);
    std::cout << "   " << spec.id << ": rendered " << image->width() << "x"
              << image->height() << " " << openfx::pixelComponentsName(out->components())
              << " " << openfx::pixelDepthName(out->depth())
              << (o.renders > 1 ? " x" + std::to_string(o.renders) : "") << " in "
              << std::lround(ms) << " ms\n";
  }

  if (o.out) {
    writeImage(*o.out, *image);
    std::cout << "   wrote " << o.out->string() << "\n";
  }

  int failures = 0;
  for (const auto& e : o.expects) {
    const OfxRectI& b = image->bounds();
    if (e.x < b.x1 || e.x >= b.x2 || e.y < b.y1 || e.y >= b.y2) {
      openfx::Logger::error("expect: pixel ({},{}) is outside the output bounds", e.x,
                            e.y);
      ++failures;
      continue;
    }
    auto p = image->pixel(e.x, e.y);
    bool match = true;
    for (int c = 0; c < 4; ++c) match = match && std::fabs(p[c] - e.rgba[c]) <= e.tol;
    std::cout << "   pixel (" << e.x << "," << e.y << ") = " << p[0] << "," << p[1] << ","
              << p[2] << "," << p[3] << (match ? "  ok" : "  MISMATCH") << " (expected "
              << e.rgba[0] << "," << e.rgba[1] << "," << e.rgba[2] << "," << e.rgba[3]
              << ")\n";
    if (!match)
      ++failures;
  }

  // Tear down in reverse: instances before descriptors, then unload.
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
