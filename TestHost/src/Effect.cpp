// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Effect.h"

#include <openfx/host/ofxPropSetAccessors.h>
#include <openfx/ofxLog.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/ofxStatusStrings.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace testhost {

namespace {

std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
  return out;
}

// The host's preferred type if the clip supports it, else the first the plugin lists.
Components pickComponents(const PropertySet& clipDesc, std::optional<Components> preferred) {
  auto supported = clipDesc.getStrings(kOfxImageEffectPropSupportedComponents);
  if (preferred && std::find(supported.begin(), supported.end(), openfx::pixelComponentsName(*preferred)) != supported.end())
    return *preferred;
  for (const auto& name : supported) {
    if (auto c = openfx::pixelComponentsFromName(name)) return *c;
  }
  return Components::RGBA;
}

Depth pickDepth(const EffectDescriptor& desc, std::optional<Depth> preferred) {
  auto supported = desc.supportedDepths();
  if (preferred && std::find(supported.begin(), supported.end(), *preferred) != supported.end()) return *preferred;
  for (Depth d : {Depth::Float, Depth::Byte, Depth::Short})
    if (std::find(supported.begin(), supported.end(), d) != supported.end()) return d;
  return Depth::Float;
}

// The project properties the framework writes onto an instance. The extent is
// rooted at 0,0, while the project window starts at the image's origin.
openfx::host::InstanceProject instanceProject(const Project& p) {
  double w = p.width, h = p.height, ox = p.originX, oy = p.originY;
  openfx::host::InstanceProject ip;
  ip.size = {w, h};
  ip.offset = {ox, oy};
  ip.extent = {std::max(w, ox + w), std::max(h, oy + h)};
  ip.frameRate = p.frameRate;
  ip.duration = p.frames;
  return ip;
}

}  // namespace

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------

std::shared_ptr<ImageBuffer> ImageBuffer::create(OfxRectI bounds, Components components, Depth depth, int rowPadding) {
  std::shared_ptr<ImageBuffer> img(new ImageBuffer);
  img->bounds_ = bounds;
  img->components_ = components;
  img->depth_ = depth;
  img->rowPadding_ = std::max(0, rowPadding);
  size_t pixels = static_cast<size_t>(img->rowBytes()) * std::max(0, img->height());
  img->data_.assign(pixels + 2 * kGuardBytes, kGuardPattern);
  std::fill_n(img->data_.begin() + kGuardBytes, pixels, std::byte{0});
  return img;
}

std::string ImageBuffer::checkGuards() const {
  auto intact = [&](size_t from) {
    for (size_t i = from; i < from + kGuardBytes; ++i)
      if (data_[i] != kGuardPattern) return false;
    return true;
  };
  bool before = !intact(0), after = !intact(data_.size() - kGuardBytes);
  if (before && after) return "before and after";
  return before ? "before" : after ? "after" : "";
}

int ImageBuffer::channels() const { return openfx::channelCount(components_); }

int ImageBuffer::bytesPerChannel() const { return openfx::bytesPerChannel(depth_); }

std::array<float, 4> ImageBuffer::pixel(int x, int y) const {
  const std::byte* p = data() + static_cast<size_t>(y - bounds_.y1) * rowBytes() +
                       static_cast<size_t>(x - bounds_.x1) * channels() * bytesPerChannel();
  auto read = [&](int c) -> float {
    switch (depth_) {
      case Depth::Byte: return reinterpret_cast<const uint8_t*>(p)[c] / 255.f;
      case Depth::Short: return reinterpret_cast<const uint16_t*>(p)[c] / 65535.f;
      case Depth::Float: return reinterpret_cast<const float*>(p)[c];
    }
    return 0.f;
  };
  switch (components_) {
    case Components::RGBA: return {read(0), read(1), read(2), read(3)};
    case Components::RGB: return {read(0), read(1), read(2), 1.f};
    case Components::Alpha: {
      float a = read(0);
      return {a, a, a, a};
    }
  }
  return {};
}

void ImageBuffer::setPixel(int x, int y, std::array<float, 4> rgba) {
  std::byte* p = data() + static_cast<size_t>(y - bounds_.y1) * rowBytes() +
                 static_cast<size_t>(x - bounds_.x1) * channels() * bytesPerChannel();
  auto write = [&](int c, float v) {
    switch (depth_) {
      case Depth::Byte: reinterpret_cast<uint8_t*>(p)[c] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); break;
      case Depth::Short: reinterpret_cast<uint16_t*>(p)[c] = static_cast<uint16_t>(std::lround(std::clamp(v, 0.f, 1.f) * 65535.f)); break;
      case Depth::Float: reinterpret_cast<float*>(p)[c] = v; break;
    }
  };
  switch (components_) {
    case Components::RGBA: for (int c = 0; c < 4; ++c) write(c, rgba[c]); break;
    case Components::RGB: for (int c = 0; c < 3; ++c) write(c, rgba[c]); break;
    case Components::Alpha: write(0, rgba[3]); break;
  }
}

std::shared_ptr<ImageBuffer> ImageBuffer::converted(Components components, Depth depth) const {
  auto out = create(bounds_, components, depth, rowPadding_);
  for (int y = bounds_.y1; y < bounds_.y2; ++y)
    for (int x = bounds_.x1; x < bounds_.x2; ++x) out->setPixel(x, y, pixel(x, y));
  return out;
}

std::shared_ptr<ImageBuffer> ImageBuffer::reframed(OfxPointI origin, int rowPadding) const {
  auto out = create({origin.x, origin.y, origin.x + width(), origin.y + height()}, components_, depth_, rowPadding);
  for (int y = 0; y < height(); ++y)
    for (int x = 0; x < width(); ++x) out->setPixel(origin.x + x, origin.y + y, pixel(bounds_.x1 + x, bounds_.y1 + y));
  return out;
}

size_t ImageBuffer::nonFiniteCount() const {
  if (depth_ != Depth::Float) return 0;
  size_t n = 0;
  for (int y = bounds_.y1; y < bounds_.y2; ++y)
    for (int x = bounds_.x1; x < bounds_.x2; ++x)
      for (float v : pixel(x, y))
        if (!std::isfinite(v)) ++n;
  return n;
}

// ---------------------------------------------------------------------------
// Parameter values on the command line, and pretty printing
// ---------------------------------------------------------------------------

bool parseParam(Param& p, std::string_view text) {
  if (p.kind() == Param::Kind::String) {
    p.str = text;
    return true;
  }
  if (p.kind() == Param::Kind::None) return false;
  std::vector<std::string> parts;
  for (size_t start = 0; start <= text.size();) {
    size_t comma = text.find(',', start);
    if (comma == std::string_view::npos) comma = text.size();
    parts.emplace_back(text.substr(start, comma - start));
    start = comma + 1;
  }
  if (static_cast<int>(parts.size()) != p.arity()) return false;
  try {
    for (int i = 0; i < p.arity(); ++i) {
      const std::string& part = parts[i];
      if (p.kind() == Param::Kind::Double) p.doubles[i] = std::stod(part);
      else if (part == "true" || part == "on") p.ints[i] = 1;
      else if (part == "false" || part == "off") p.ints[i] = 0;
      else p.ints[i] = std::stoi(part);
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

std::string paramValueString(const Param& p) {
  std::ostringstream os;
  switch (p.kind()) {
    case Param::Kind::Double:
      for (size_t i = 0; i < p.doubles.size(); ++i) os << (i ? "," : "") << p.doubles[i];
      break;
    case Param::Kind::Int:
      for (size_t i = 0; i < p.ints.size(); ++i) os << (i ? "," : "") << p.ints[i];
      break;
    case Param::Kind::String:
      os << '"' << p.str << '"';
      break;
    case Param::Kind::None:
      os << "-";
      break;
  }
  return os.str();
}

std::string describeEffect(const EffectDescriptor& desc) {
  std::ostringstream os;
  const PropertySet& p = desc.props();
  os << "  label:        " << p.getString(kOfxPropLabel) << "\n";
  os << "  grouping:     " << p.getString(kOfxImageEffectPluginPropGrouping) << "\n";
  os << "  contexts:     " << join(desc.supportedContexts()) << "\n";
  os << "  pixel depths: " << join(p.getStrings(kOfxImageEffectPropSupportedPixelDepths)) << "\n";
  os << "  tiles: " << p.getInt(kOfxImageEffectPropSupportsTiles) << "  multires: " << p.getInt(kOfxImageEffectPropSupportsMultiResolution)
     << "  temporal: " << p.getInt(kOfxImageEffectPropTemporalClipAccess) << "  thread safety: "
     << p.getString(kOfxImageEffectPluginRenderThreadSafety) << "\n";
  if (!desc.context().empty()) {
    os << "  context " << desc.context() << ":\n";
    for (const auto& c : desc.clips()) {
      const PropertySet& cp = c->props();
      os << "    clip " << c->name() << ": " << join(cp.getStrings(kOfxImageEffectPropSupportedComponents))
         << (cp.getInt(kOfxImageClipPropOptional) ? " optional" : "") << (cp.getInt(kOfxImageClipPropIsMask) ? " mask" : "") << "\n";
    }
    for (const auto& prm : desc.params().params()) {
      const PropertySet& pp = prm->props();
      os << "    param " << prm->name() << " (" << prm->type() << ")";
      if (prm->kind() != Param::Kind::None) {
        Param tmp(prm->name(), prm->type(), &pp);
        tmp.initFromDefault();
        os << " default=" << paramValueString(tmp);
      }
      if (std::string parent = pp.getString(kOfxParamPropParent); !parent.empty()) os << " in " << parent;
      if (std::string hint = pp.getString(kOfxParamPropHint); !hint.empty()) os << "  \"" << hint << '"';
      os << "\n";
    }
  }
  return os.str();
}

// ---------------------------------------------------------------------------
// EffectInstance
// ---------------------------------------------------------------------------

EffectInstance::EffectInstance(const EffectDescriptor& contextDescriptor, const Project& project)
    : openfx::host::EffectInstance(contextDescriptor, instanceProject(project)),
      project_(project),
      depth_(pickDepth(contextDescriptor, project.preferredDepth)) {
  createClips();
  for (const auto& p : params().params()) scaleNormalisedDefault(*p);
}

EffectInstance::~EffectInstance() { destroyInstance(); }

std::unique_ptr<Clip> EffectInstance::makeClip(const Clip& descriptorClip) {
  return std::make_unique<TestClip>(descriptorClip.name(), "ClipInstance", &descriptorClip.props());
}

openfx::host::ClipProperties EffectInstance::clipProperties(const Clip& descriptorClip) const {
  openfx::host::ClipProperties cp;
  cp.components = pickComponents(descriptorClip.props(), project_.preferredComponents);
  cp.depth = depth_;
  cp.frameRate = project_.frameRate;
  cp.frameRange = {0.0, double(project_.frames - 1)};
  return cp;
}

// A spatial double param may declare its default in normalised coordinates;
// the host converts it to canonical coordinates using the project extent.
void EffectInstance::scaleNormalisedDefault(Param& p) {
  if (p.kind() != Param::Kind::Double) return;
  if (p.props().getString(kOfxParamPropDefaultCoordinateSystem) != kOfxParamCoordinatesNormalised) return;
  std::string type = p.props().getString(kOfxParamPropDoubleType);
  double w = project_.width, h = project_.height;
  if (type == kOfxParamDoubleTypeX || type == kOfxParamDoubleTypeXAbsolute) p.doubles[0] *= w;
  else if (type == kOfxParamDoubleTypeY || type == kOfxParamDoubleTypeYAbsolute) p.doubles[0] *= h;
  else if ((type == kOfxParamDoubleTypeXY || type == kOfxParamDoubleTypeXYAbsolute) && p.doubles.size() >= 2) {
    p.doubles[0] *= w;
    p.doubles[1] *= h;
  }
}

OfxRectI EffectInstance::projectRect() const {
  return {project_.originX, project_.originY, project_.originX + project_.width, project_.originY + project_.height};
}

void EffectInstance::connectInput(std::string_view clipName, std::shared_ptr<ImageBuffer> image) {
  Clip* c = clip(clipName);
  if (!c) throw std::runtime_error("no clip named " + std::string(clipName));
  if (c->isOutput()) throw std::runtime_error("cannot connect an image to the output clip");
  pixels(*c).buffer = std::move(image);
  c->props().set(kOfxImageClipPropConnected, 0, 1);
}

void EffectInstance::setParam(std::string_view name, std::string_view value) {
  Param* p = params().find(name);
  if (!p) throw std::runtime_error("no parameter named " + std::string(name));
  if (!parseParam(*p, value))
    throw std::runtime_error("cannot parse \"" + std::string(value) + "\" for " + p->type() + " parameter " + p->name());
  if (p->type() == kOfxParamTypeStrChoice) {
    // The spec leaves a value outside the declared enums undefined and recommends
    // the host substitute the default, as it would for a removed option in a project.
    auto enums = p->props().getStrings(kOfxParamPropChoiceEnum);
    if (std::find(enums.begin(), enums.end(), p->str) == enums.end()) {
      std::string fallback = p->props().getString(kOfxParamPropDefault, 0, enums.empty() ? "" : enums.front());
      openfx::Logger::warn("{}: \"{}\" is not one of the declared enums; using \"{}\"", p->name(), p->str, fallback);
      p->str = fallback;
    }
  }
  paramChanged(*p, kOfxChangeUserEdited, 0.0, {1.0, 1.0});
}

void EffectInstance::updateClipPreferences() { getClipPreferences(); }

bool EffectInstance::clipRegionOfDefinition(Clip& clip, OfxTime time, OfxRectD& out) {
  if (const auto& buffer = pixels(clip).buffer) {
    const OfxRectI& b = buffer->bounds();
    out = {double(b.x1), double(b.y1), double(b.x2), double(b.y2)};
    return true;
  }
  return openfx::host::EffectInstance::clipRegionOfDefinition(clip, time, out);
}

std::shared_ptr<ImageBuffer> EffectInstance::renderFrame(double time) {
  Clip* output = clip(kOfxImageEffectOutputClipName);
  if (!output) throw std::runtime_error("effect has no output clip");
  for (const auto& c : clips())
    if (!c->isOutput() && !pixels(*c).buffer && !c->props().getInt(kOfxImageClipPropOptional))
      openfx::Logger::warn("input clip {} is not connected", c->name());

  // Render the effect's region of definition clipped to the project: a
  // generator may declare an infinite region, and a host only asks for what it needs.
  OfxRectD rod = regionOfDefinition(time);
  OfxRectI pr = projectRect();
  OfxRectI window{int(std::floor(std::max(rod.x1, double(pr.x1)))), int(std::floor(std::max(rod.y1, double(pr.y1)))),
                  int(std::ceil(std::min(rod.x2, double(pr.x2)))), int(std::ceil(std::min(rod.y2, double(pr.y2))))};
  int padding = 0;
  for (const auto& c : clips())
    if (const auto& b = pixels(*c).buffer; !c->isOutput() && b)
      padding = std::max(padding, b->rowBytes() - b->width() * b->channels() * b->bytesPerChannel());
  if (window.x2 <= window.x1 || window.y2 <= window.y1) {
    // Nothing of the effect falls inside the project: the frame is empty, and
    // the plugin must not be asked to render outside its region of definition.
    openfx::Logger::info("region of definition ({},{})-({},{}) is outside the project; rendering nothing", rod.x1, rod.y1, rod.x2, rod.y2);
    output_ = ImageBuffer::create(pr, output->components(), output->depth(), padding);
    pixels(*output).buffer = output_;
    return output_;
  }
  output_ = ImageBuffer::create(window, output->components(), output->depth(), padding);
  pixels(*output).buffer = output_;

  if (auto identityClip = isIdentity(time, window, {1.0, 1.0}, kOfxImageFieldNone)) {
    openfx::Logger::info("plugin reports identity from clip {}", *identityClip);
    if (Clip* src = clip(*identityClip); src)
      if (const auto& buffer = pixels(*src).buffer) {
        for (int y = window.y1; y < window.y2; ++y)
          for (int x = window.x1; x < window.x2; ++x) output_->setPixel(x, y, buffer->pixel(x, y));
      }
    return output_;
  }

  openfx::host::RenderArgs args;
  args.time = time;
  args.renderWindow = window;
  args.frameRange = {time, time};
  beginSequenceRender(args);
  OfxStatus s = render(args);
  endSequenceRender(args);
  if (s != kOfxStatOK) throw std::runtime_error("render failed: " + std::string(ofxStatusToString(s)));

  for (const auto& c : clips()) {
    TestClip& tc = pixels(*c);
    if (!tc.liveImages.empty()) {
      openfx::Logger::warn("plugin left {} image(s) of clip {} unreleased", tc.liveImages.size(), c->name());
      tc.liveImages.clear();
    }
    if (tc.buffer)
      if (std::string where = tc.buffer->checkGuards(); !where.empty())
        openfx::Logger::warn("plugin wrote outside the bounds of the {} image ({} the pixel data)", c->name(), where);
  }
  if (size_t bad = output_->nonFiniteCount()) openfx::Logger::warn("output has {} non-finite channel values", bad);
  return output_;
}

Image* EffectInstance::fetchImage(Clip& clip, OfxTime time, const OfxRectD*) {
  TestClip& tc = pixels(clip);
  std::shared_ptr<ImageBuffer> buffer = tc.buffer;
  if (!buffer) return nullptr;
  if (buffer->components() != clip.components() || buffer->depth() != clip.depth()) {
    tc.buffer = buffer = buffer->converted(clip.components(), clip.depth());  // cache the negotiated format
  }
  auto image = std::make_unique<TestImage>();
  image->buffer = buffer;
  image->clip = &clip;
  const OfxRectI& b = buffer->bounds();
  std::string id = clip.name() + "@" + std::to_string(time);
  openfx::PropertyAccessor acc(image->handle(), PropertySet::suite());
  openfx::host::propsets::Image props(acc);
  props.setType(kOfxTypeImage)
      .setPixelDepth(openfx::pixelDepthName(buffer->depth()))
      .setComponents(openfx::pixelComponentsName(buffer->components()))
      .setPreMultiplication(openfx::host::premultiplicationFor(buffer->components()))
      .setRenderScale({1.0, 1.0})
      .setPixelAspectRatio(1.0)
      .setData(buffer->data())
      .setBounds({b.x1, b.y1, b.x2, b.y2})
      .setRegionOfDefinition({b.x1, b.y1, b.x2, b.y2})
      .setRowBytes(buffer->rowBytes())
      .setField(kOfxImageFieldNone)
      .setUniqueIdentifier(id.c_str());
  tc.liveImages.push_back(std::move(image));
  openfx::Logger::debug("clipGetImage {} -> {} ({} live)", clip.name(), id, tc.liveImages.size());
  return tc.liveImages.back().get();
}

void EffectInstance::releaseImage(Image& image) {
  auto& live = pixels(*image.clip).liveImages;
  openfx::Logger::debug("clipReleaseImage {} ({} live)", image.clip->name(), live.size());
  live.erase(std::remove_if(live.begin(), live.end(), [&](const auto& p) { return p.get() == &image; }), live.end());
}

}  // namespace testhost
