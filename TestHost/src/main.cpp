// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
//
// ofxtesthost: load OFX image effect plugins, describe them, set parameters,
// render an image through one plugin or a chain of them, and check the result.

#include <ofxImageEffect.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Effect.h"
#include "ImageIO.h"
#include "Log.h"
#include "Plugin.h"
#include "Suites.h"

#ifndef _WIN32
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
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
  --components TYPE     negotiate RGBA, RGB or Alpha for every clip that supports it (default:
                        the first type each clip lists)

Input (one of; default: a 64x64 ramp):
  --in FILE             P6 PPM or PFM image
  --fill R,G,B,A        constant colour
  --ramp                red ramps left to right, green bottom to top, blue 0.5
  --size WxH            image size for --fill and --ramp (default 64x64)
  --time T              frame to render (default 0)

Output and checks:
  --out FILE            write the result (.ppm 8-bit or .pfm float)
  --expect X,Y,R,G,B,A[,TOL]
                        require the output pixel at (X,Y) to match, within TOL (default 0.004)
  --list                list the plugins found and exit
  --describe            print each selected plugin's contexts, clips and params
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
  struct Expect {
    int x, y;
    std::array<float, 4> rgba;
    float tol;
  };
  std::vector<Expect> expects;
  bool list = false, describe = false;
  std::optional<Components> components;
};

std::vector<float> parseFloats(const std::string& s) {
  std::vector<float> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t comma = s.find(',', start);
    if (comma == std::string::npos) comma = s.size();
    out.push_back(std::stof(s.substr(start, comma - start)));
    start = comma + 1;
  }
  return out;
}

Options parseArgs(int argc, char** argv) {
  Options o;
  auto need = [&](int& i, const char* flag) -> std::string {
    if (i + 1 >= argc) throw std::runtime_error(std::string(flag) + " needs a value");
    return argv[++i];
  };
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--plugin") o.effects.push_back({need(i, "--plugin"), "", {}, {}});
    else if (a == "--context") {
      if (o.effects.empty()) o.effects.push_back({});
      o.effects.back().context = need(i, "--context");
    } else if (a == "--param") {
      std::string kv = need(i, "--param");
      size_t eq = kv.find('=');
      if (eq == std::string::npos) throw std::runtime_error("--param needs NAME=VALUE");
      if (o.effects.empty()) o.effects.push_back({});
      o.effects.back().params.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
    } else if (a == "--clip") {
      std::string kv = need(i, "--clip");
      size_t eq = kv.find('=');
      if (eq == std::string::npos) throw std::runtime_error("--clip needs NAME=SOURCE");
      if (o.effects.empty()) o.effects.push_back({});
      o.effects.back().clips.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
    } else if (a == "--components") {
      std::string t = need(i, "--components");
      Components c;
      if (!componentsFromName("OfxImageComponent" + t, &c)) throw std::runtime_error("--components needs RGBA, RGB or Alpha");
      o.components = c;
    } else if (a == "--in") o.in = need(i, "--in");
    else if (a == "--out") o.out = need(i, "--out");
    else if (a == "--fill") {
      auto v = parseFloats(need(i, "--fill"));
      if (v.size() != 4) throw std::runtime_error("--fill needs R,G,B,A");
      o.fill = {v[0], v[1], v[2], v[3]};
    } else if (a == "--ramp") o.ramp = true;
    else if (a == "--size") {
      std::string s = need(i, "--size");
      size_t x = s.find('x');
      if (x == std::string::npos) throw std::runtime_error("--size needs WxH");
      o.width = std::stoi(s.substr(0, x));
      o.height = std::stoi(s.substr(x + 1));
    } else if (a == "--time") o.time = std::stod(need(i, "--time"));
    else if (a == "--expect") {
      auto v = parseFloats(need(i, "--expect"));
      if (v.size() != 6 && v.size() != 7) throw std::runtime_error("--expect needs X,Y,R,G,B,A[,TOL]");
      o.expects.push_back({int(v[0]), int(v[1]), {v[2], v[3], v[4], v[5]}, v.size() == 7 ? v[6] : 0.004f});
    } else if (a == "--list") o.list = true;
    else if (a == "--describe") o.describe = true;
    else if (a == "--verbose" || a == "-v") log::verbose = true;
    else if (a == "--help" || a == "-h") {
      std::cout << kUsage;
      std::exit(0);
    } else if (a.starts_with("-")) throw std::runtime_error("unknown option " + a);
    else o.paths.emplace_back(a);
  }
  if (o.paths.empty()) throw std::runtime_error("no plugin path given (--help for usage)");
  return o;
}

std::string pickContext(const EffectDescriptor& desc, const std::string& requested) {
  auto contexts = desc.supportedContexts();
  if (!requested.empty()) {
    if (std::find(contexts.begin(), contexts.end(), requested) == contexts.end())
      throw std::runtime_error(desc.plugin().id() + " does not support context " + requested);
    return requested;
  }
  for (const char* c : {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral, kOfxImageEffectContextGenerator})
    if (std::find(contexts.begin(), contexts.end(), c) != contexts.end()) return c;
  throw std::runtime_error(desc.plugin().id() + " supports none of the filter, general or generator contexts");
}

// An image for --clip: a file, fill:R,G,B,A, ramp, or the main input.
std::shared_ptr<ImageBuffer> clipImage(const std::string& source, const std::shared_ptr<ImageBuffer>& input) {
  if (source == "input") return input;
  if (source == "ramp") return rampImage(input->width(), input->height());
  if (source.starts_with("fill:")) {
    auto v = parseFloats(source.substr(5));
    if (v.size() != 4) throw std::runtime_error("--clip fill needs fill:R,G,B,A");
    return solidImage(input->width(), input->height(), {v[0], v[1], v[2], v[3]});
  }
  return readImage(source);
}

// The clip a filter-style effect reads its main input from.
Clip* mainInput(EffectInstance& inst) {
  if (Clip* c = inst.clip(kOfxImageEffectSimpleSourceClipName)) return c;
  for (auto& c : inst.clips())
    if (!c->isOutput() && !c->props().getInt(kOfxImageClipPropIsMask)) return c.get();
  return nullptr;
}

int run(const Options& o) {
  // Load every plugin binary and index the image effects by identifier.
  std::vector<std::unique_ptr<Bundle>> bundles;
  for (const auto& p : o.paths)
    for (auto& b : Bundle::load(p)) bundles.push_back(std::move(b));
  std::vector<std::unique_ptr<Plugin>> plugins;
  for (const auto& b : bundles)
    for (OfxPlugin* p : b->plugins()) {
      auto plugin = std::make_unique<Plugin>(p, *b);
      if (plugin->isImageEffect()) plugins.push_back(std::move(plugin));
      else log::info("skipping {} (api {})", p->pluginIdentifier, p->pluginApi);
    }
  if (plugins.empty()) throw std::runtime_error("no image effect plugins found");

  if (o.list) {
    for (const auto& p : plugins)
      std::cout << p->id() << " v" << p->versionMajor() << "." << p->versionMinor() << "  (" << p->bundlePath().string() << ")\n";
    return 0;
  }

  std::vector<EffectSpec> specs = o.effects;
  if (specs.empty()) specs.push_back({});
  if (specs[0].id.empty()) specs[0].id = plugins.front()->id();

  Project project{o.width, o.height, 24.0, 1, o.components};
  suites::timeline() = {0, 0, o.time};

  // Source image.
  std::shared_ptr<ImageBuffer> image;
  if (o.in) image = readImage(*o.in);
  else if (o.fill) image = solidImage(o.width, o.height, *o.fill);
  else image = rampImage(o.width, o.height);
  project.width = image->width();
  project.height = image->height();

  // Descriptors and instances, kept alive in order so a chain can hand images along.
  std::vector<std::unique_ptr<EffectDescriptor>> descriptors;
  std::vector<std::unique_ptr<EffectInstance>> instances;
  for (const auto& spec : specs) {
    auto it = std::find_if(plugins.begin(), plugins.end(), [&](auto& p) { return p->id() == spec.id; });
    if (it == plugins.end()) throw std::runtime_error("no plugin with identifier " + spec.id);
    Plugin& plugin = **it;
    std::cout << "== " << plugin.id() << " v" << plugin.versionMajor() << "." << plugin.versionMinor() << "\n";

    auto global = plugin.describe();
    std::string context = pickContext(*global, spec.context);
    auto desc = plugin.describeInContext(*global, context);
    if (o.describe) std::cout << desc->describe();

    auto inst = std::make_unique<EffectInstance>(*desc, project);
    inst->create();
    for (const auto& [name, value] : spec.params) {
      inst->setParam(name, value);
      log::info("set {} = {}", name, inst->params().find(name)->valueString());
    }
    inst->updateClipPreferences();

    if (Clip* in = mainInput(*inst)) inst->connectInput(in->name(), image);
    else if (context != kOfxImageEffectContextGenerator) log::warn("{} has no input clip to connect", plugin.id());
    for (const auto& [name, source] : spec.clips) {
      inst->connectInput(name, clipImage(source, image));
      log::info("connected clip {} to {}", name, source);
    }

    auto start = std::chrono::steady_clock::now();
    image = inst->render(o.time);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    Clip* out = inst->clip(kOfxImageEffectOutputClipName);
    std::cout << "   rendered " << image->width() << "x" << image->height() << " " << componentsName(out->components()) << " "
              << depthName(out->depth()) << " in " << std::lround(ms) << " ms\n";

    descriptors.push_back(std::move(global));
    descriptors.push_back(std::move(desc));
    instances.push_back(std::move(inst));
  }

  if (o.out) {
    writeImage(*o.out, *image);
    std::cout << "   wrote " << o.out->string() << "\n";
  }

  int failures = 0;
  for (const auto& e : o.expects) {
    const OfxRectI& b = image->bounds();
    if (e.x < b.x1 || e.x >= b.x2 || e.y < b.y1 || e.y >= b.y2) {
      log::error("expect: pixel ({},{}) is outside the output bounds", e.x, e.y);
      ++failures;
      continue;
    }
    auto p = image->pixel(e.x, e.y);
    bool match = true;
    for (int c = 0; c < 4; ++c) match = match && std::fabs(p[c] - e.rgba[c]) <= e.tol;
    std::cout << "   pixel (" << e.x << "," << e.y << ") = " << p[0] << "," << p[1] << "," << p[2] << "," << p[3]
              << (match ? "  ok" : "  MISMATCH") << " (expected " << e.rgba[0] << "," << e.rgba[1] << "," << e.rgba[2] << ","
              << e.rgba[3] << ")\n";
    if (!match) ++failures;
  }

  // Tear down in reverse: instances before descriptors, then unload.
  instances.clear();
  descriptors.clear();
  for (auto& p : plugins) p->unload();
  return failures ? 1 : 0;
}

}  // namespace

#ifndef _WIN32
// A plugin crash takes the host down; at least say which plugin and action.
void crashHandler(int sig) {
  const char* action = Plugin::currentAction;
  const char* plugin = Plugin::currentPlugin;
  auto put = [](const char* s) { (void)!write(2, s, std::char_traits<char>::length(s)); };
  put("\nFATAL: signal ");
  put(sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGABRT ? "SIGABRT" : "?");
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
  for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) signal(sig, crashHandler);
}
#else
void installCrashHandler() {}
#endif

int main(int argc, char** argv) {
  installCrashHandler();
  try {
    return run(parseArgs(argc, argv));
  } catch (const std::exception& e) {
    log::error("{}", e.what());
    return 2;
  }
}
