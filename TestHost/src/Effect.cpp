// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Effect.h"

#include <openfx/host/ofxPropSetAccessors.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/ofxStatusStrings.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "Log.h"
#include "Plugin.h"

namespace testhost {

using openfx::PropertyAccessor;

namespace {

PropertyAccessor access(PropertySet& set) { return PropertyAccessor(set.handle(), PropertySet::suite()); }

bool ok(OfxStatus s) { return s == kOfxStatOK || s == kOfxStatReplyDefault; }

std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------

const char* depthName(Depth d) {
  switch (d) {
    case Depth::Byte: return kOfxBitDepthByte;
    case Depth::Short: return kOfxBitDepthShort;
    case Depth::Float: return kOfxBitDepthFloat;
  }
  return kOfxBitDepthNone;
}

const char* componentsName(Components c) {
  switch (c) {
    case Components::RGBA: return kOfxImageComponentRGBA;
    case Components::RGB: return kOfxImageComponentRGB;
    case Components::Alpha: return kOfxImageComponentAlpha;
  }
  return kOfxImageComponentNone;
}

bool depthFromName(std::string_view name, Depth* out) {
  for (Depth d : {Depth::Byte, Depth::Short, Depth::Float})
    if (name == depthName(d)) return *out = d, true;
  return false;
}

bool componentsFromName(std::string_view name, Components* out) {
  for (Components c : {Components::RGBA, Components::RGB, Components::Alpha})
    if (name == componentsName(c)) return *out = c, true;
  return false;
}

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

int ImageBuffer::channels() const {
  switch (components_) {
    case Components::RGBA: return 4;
    case Components::RGB: return 3;
    case Components::Alpha: return 1;
  }
  return 0;
}

int ImageBuffer::bytesPerChannel() const {
  switch (depth_) {
    case Depth::Byte: return 1;
    case Depth::Short: return 2;
    case Depth::Float: return 4;
  }
  return 0;
}

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
// Clip
// ---------------------------------------------------------------------------

Clip::Clip(std::string name, std::string_view propSet, const PropertySet* parent)
    : name_(std::move(name)), props_(propSet, parent) {}

Components Clip::components() const {
  Components c = Components::RGBA;
  componentsFromName(props_.getString(kOfxImageEffectPropComponents), &c);
  return c;
}

Depth Clip::depth() const {
  Depth d = Depth::Float;
  depthFromName(props_.getString(kOfxImageEffectPropPixelDepth), &d);
  return d;
}

// ---------------------------------------------------------------------------
// Param
// ---------------------------------------------------------------------------

namespace {

struct ParamKindInfo {
  const char* type;
  Param::Kind kind;
  int arity;
  const char* propSet;
};

const ParamKindInfo kParamKinds[] = {
    {kOfxParamTypeInteger, Param::Kind::Int, 1, "ParamsByte"},
    {kOfxParamTypeInteger2D, Param::Kind::Int, 2, "ParamsInt2D3D"},
    {kOfxParamTypeInteger3D, Param::Kind::Int, 3, "ParamsInt2D3D"},
    {kOfxParamTypeBoolean, Param::Kind::Int, 1, "ParamsByte"},
    {kOfxParamTypeChoice, Param::Kind::Int, 1, "ParamsChoice"},
    {kOfxParamTypeStrChoice, Param::Kind::String, 1, "ParamsStrChoice"},
    {kOfxParamTypeDouble, Param::Kind::Double, 1, "ParamsDouble1D"},
    {kOfxParamTypeDouble2D, Param::Kind::Double, 2, "ParamsDouble2D3D"},
    {kOfxParamTypeDouble3D, Param::Kind::Double, 3, "ParamsDouble2D3D"},
    {kOfxParamTypeRGB, Param::Kind::Double, 3, "ParamsRGB"},
    {kOfxParamTypeRGBA, Param::Kind::Double, 4, "ParamsRGBA"},
    {kOfxParamTypeString, Param::Kind::String, 1, "ParamsString"},
    {kOfxParamTypeCustom, Param::Kind::String, 1, "ParamsCustom"},
    {kOfxParamTypeGroup, Param::Kind::None, 0, "ParamsGroup"},
    {kOfxParamTypePage, Param::Kind::None, 0, "ParamsPage"},
    {kOfxParamTypePushButton, Param::Kind::None, 0, "ParamsByte"},
    {kOfxParamTypeParametric, Param::Kind::None, 0, "ParamsParametric"},
};

const ParamKindInfo* paramKind(std::string_view type) {
  for (const auto& k : kParamKinds)
    if (type == k.type) return &k;
  return nullptr;
}

}  // namespace

Param::Param(std::string name, std::string type, const PropertySet* parent)
    : name_(std::move(name)), type_(std::move(type)) {
  const ParamKindInfo* info = paramKind(type_);
  if (!info) throw std::runtime_error("unknown parameter type " + type_);
  kind_ = info->kind;
  arity_ = info->arity;
  props_ = PropertySet(info->propSet, parent);
  if (parent) {  // instance: the host-written animation state
    props_.set(kOfxParamPropIsAnimating, 0, 0);
    props_.set(kOfxParamPropIsAutoKeying, 0, 0);
    return;
  }
  // Descriptor: the spec defaults a plugin may rely on (as in HostSupport).
  props_.set(kOfxPropType, 0, kOfxTypeParameter);
  props_.set(kOfxPropName, 0, name_.c_str());
  props_.set(kOfxPropLabel, 0, name_.c_str());
  props_.set(kOfxPropShortLabel, 0, name_.c_str());
  props_.set(kOfxPropLongLabel, 0, name_.c_str());
  props_.set(kOfxParamPropType, 0, type_.c_str());
  props_.set(kOfxParamPropScriptName, 0, name_.c_str());
  props_.set(kOfxParamPropEnabled, 0, 1);
  props_.set(kOfxParamPropPersistant, 0, 1);
  props_.set(kOfxParamPropEvaluateOnChange, 0, 1);
  props_.set(kOfxParamPropCanUndo, 0, 1);
  props_.set(kOfxParamPropCacheInvalidation, 0, kOfxParamInvalidateValueChange);
  props_.set(kOfxParamPropAnimates, 0, kind_ == Kind::Double || type_ == kOfxParamTypeInteger ? 1 : 0);
  props_.set(kOfxParamPropInteractSizeAspect, 0, 1.0);
  for (int i = 0; i < 2; ++i) {
    props_.set(kOfxParamPropInteractMinimumSize, i, 10.0);
    props_.set(kOfxParamPropInteractPreferedSize, i, 10);
  }
  bool colour = type_ == kOfxParamTypeRGB || type_ == kOfxParamTypeRGBA;
  for (int i = 0; i < arity_; ++i) {
    if (kind_ == Kind::Double) {
      props_.set(kOfxParamPropDefault, i, 0.0);
      props_.set(kOfxParamPropMin, i, -DBL_MAX);
      props_.set(kOfxParamPropMax, i, DBL_MAX);
      props_.set(kOfxParamPropDisplayMin, i, colour ? 0.0 : -DBL_MAX);
      props_.set(kOfxParamPropDisplayMax, i, colour ? 1.0 : DBL_MAX);
    } else if (kind_ == Kind::Int) {
      props_.set(kOfxParamPropDefault, i, 0);
      props_.set(kOfxParamPropMin, i, INT_MIN);
      props_.set(kOfxParamPropMax, i, INT_MAX);
      props_.set(kOfxParamPropDisplayMin, i, INT_MIN);
      props_.set(kOfxParamPropDisplayMax, i, INT_MAX);
    } else if (kind_ == Kind::String) {
      props_.set(kOfxParamPropDefault, i, "");
    }
  }
  if (kind_ == Kind::Double) {
    props_.set(kOfxParamPropIncrement, 0, 1.0);
    props_.set(kOfxParamPropDigits, 0, 2);
    props_.set(kOfxParamPropDoubleType, 0, kOfxParamDoubleTypePlain);
    props_.set(kOfxParamPropDefaultCoordinateSystem, 0, kOfxParamCoordinatesCanonical);
  }
  if (type_ == kOfxParamTypeString) props_.set(kOfxParamPropStringMode, 0, kOfxParamStringIsSingleLine);
  if (type_ == kOfxParamTypeGroup) props_.set(kOfxParamPropGroupOpen, 0, 1);
}

void Param::initFromDefault() {
  switch (kind_) {
    case Kind::Double:
      doubles.assign(arity_, 0.0);
      for (int i = 0; i < arity_; ++i) doubles[i] = props_.getDouble(kOfxParamPropDefault, i);
      break;
    case Kind::Int:
      ints.assign(arity_, 0);
      for (int i = 0; i < arity_; ++i) ints[i] = props_.getInt(kOfxParamPropDefault, i);
      break;
    case Kind::String:
      str = props_.getString(kOfxParamPropDefault);
      break;
    case Kind::None:
      break;
  }
}

std::string Param::valueString() const {
  std::ostringstream os;
  switch (kind_) {
    case Kind::Double:
      for (size_t i = 0; i < doubles.size(); ++i) os << (i ? "," : "") << doubles[i];
      break;
    case Kind::Int:
      for (size_t i = 0; i < ints.size(); ++i) os << (i ? "," : "") << ints[i];
      break;
    case Kind::String:
      os << '"' << str << '"';
      break;
    case Kind::None:
      os << "-";
      break;
  }
  return os.str();
}

bool Param::parse(std::string_view text) {
  if (kind_ == Kind::String) {
    str = text;
    return true;
  }
  if (kind_ == Kind::None) return false;
  std::vector<std::string> parts;
  for (size_t start = 0; start <= text.size();) {
    size_t comma = text.find(',', start);
    if (comma == std::string_view::npos) comma = text.size();
    parts.emplace_back(text.substr(start, comma - start));
    start = comma + 1;
  }
  if (static_cast<int>(parts.size()) != arity_) return false;
  try {
    for (int i = 0; i < arity_; ++i) {
      const std::string& p = parts[i];
      if (kind_ == Kind::Double) doubles[i] = std::stod(p);
      else if (p == "true" || p == "on") ints[i] = 1;
      else if (p == "false" || p == "off") ints[i] = 0;
      else ints[i] = std::stoi(p);
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

Param* ParamSet::find(std::string_view name) {
  for (auto& p : params_)
    if (p->name() == name) return p.get();
  return nullptr;
}

// ---------------------------------------------------------------------------
// EffectBase / EffectDescriptor
// ---------------------------------------------------------------------------

Clip* EffectBase::clip(std::string_view name) {
  for (auto& c : clips_)
    if (c->name() == name) return c.get();
  return nullptr;
}

EffectDescriptor::EffectDescriptor(Plugin& plugin, const EffectDescriptor* global, std::string context)
    : EffectBase(plugin), context_(std::move(context)) {
  props_ = PropertySet("EffectDescriptor", global ? &global->props() : nullptr);
  if (global) return;
  // Host-written and spec-default descriptor properties (as in HostSupport).
  props_.set(kOfxPropType, 0, kOfxTypeImageEffect);
  props_.set(kOfxPluginPropFilePath, 0, plugin.bundlePath().string().c_str());
  props_.set(kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderInstanceSafe);
  props_.set(kOfxImageEffectPluginPropHostFrameThreading, 0, 1);
  props_.set(kOfxImageEffectPropSupportsMultiResolution, 0, 1);
  props_.set(kOfxImageEffectPropSupportsTiles, 0, 1);
  props_.set(kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 1);
  props_.set(kOfxImageEffectPropOpenGLRenderSupported, 0, "false");
  props_.set(kOfxImageEffectPropCudaRenderSupported, 0, "false");
  props_.set(kOfxImageEffectPropCudaStreamSupported, 0, "false");
  props_.set(kOfxImageEffectPropMetalRenderSupported, 0, "false");
  props_.set(kOfxImageEffectPropOpenCLRenderSupported, 0, "false");
}

std::vector<std::string> EffectDescriptor::supportedContexts() const {
  return props_.getStrings(kOfxImageEffectPropSupportedContexts);
}

std::vector<Depth> EffectDescriptor::supportedDepths() const {
  std::vector<Depth> out;
  for (const auto& name : props_.getStrings(kOfxImageEffectPropSupportedPixelDepths)) {
    Depth d;
    if (depthFromName(name, &d)) out.push_back(d);
  }
  return out;
}

std::string EffectDescriptor::label() const { return props_.getString(kOfxPropLabel); }

Clip* EffectDescriptor::defineClip(const std::string& name) {
  auto clip = std::make_unique<Clip>(name, "ClipDescriptor", nullptr);
  PropertySet& p = clip->props();
  p.set(kOfxPropType, 0, kOfxTypeClip);
  p.set(kOfxPropName, 0, name.c_str());
  p.set(kOfxPropLabel, 0, name.c_str());
  p.set(kOfxPropShortLabel, 0, name.c_str());
  p.set(kOfxPropLongLabel, 0, name.c_str());
  p.set(kOfxImageClipPropFieldExtraction, 0, kOfxImageFieldDoubled);
  p.set(kOfxImageEffectPropSupportsTiles, 0, 1);
  clips_.push_back(std::move(clip));
  return clips_.back().get();
}

Param* EffectDescriptor::defineParam(const std::string& type, const std::string& name) {
  params_.params().push_back(std::make_unique<Param>(name, type, nullptr));
  return params_.params().back().get();
}

std::string EffectDescriptor::describe() const {
  std::ostringstream os;
  const PropertySet& p = props_;
  os << "  label:        " << p.getString(kOfxPropLabel) << "\n";
  os << "  grouping:     " << p.getString(kOfxImageEffectPluginPropGrouping) << "\n";
  os << "  contexts:     " << join(supportedContexts()) << "\n";
  os << "  pixel depths: " << join(p.getStrings(kOfxImageEffectPropSupportedPixelDepths)) << "\n";
  os << "  tiles: " << p.getInt(kOfxImageEffectPropSupportsTiles) << "  multires: " << p.getInt(kOfxImageEffectPropSupportsMultiResolution)
     << "  temporal: " << p.getInt(kOfxImageEffectPropTemporalClipAccess) << "  thread safety: "
     << p.getString(kOfxImageEffectPluginRenderThreadSafety) << "\n";
  if (!context_.empty()) {
    os << "  context " << context_ << ":\n";
    for (const auto& c : clips_) {
      const PropertySet& cp = c->props();
      os << "    clip " << c->name() << ": " << join(cp.getStrings(kOfxImageEffectPropSupportedComponents))
         << (cp.getInt(kOfxImageClipPropOptional) ? " optional" : "") << (cp.getInt(kOfxImageClipPropIsMask) ? " mask" : "") << "\n";
    }
    for (const auto& prm : params_.params()) {
      const PropertySet& pp = prm->props();
      os << "    param " << prm->name() << " (" << prm->type() << ")";
      if (prm->kind() != Param::Kind::None) {
        Param tmp(prm->name(), prm->type(), &pp);
        tmp.initFromDefault();
        os << " default=" << tmp.valueString();
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

namespace {

// The host's preferred type if the clip supports it, else the first the plugin lists.
Components pickComponents(const PropertySet& clipDesc, std::optional<Components> preferred) {
  auto supported = clipDesc.getStrings(kOfxImageEffectPropSupportedComponents);
  if (preferred && std::find(supported.begin(), supported.end(), componentsName(*preferred)) != supported.end())
    return *preferred;
  for (const auto& name : supported) {
    Components c;
    if (componentsFromName(name, &c)) return c;
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

const char* premultFor(Components c) { return c == Components::RGBA ? kOfxImagePreMultiplied : kOfxImageOpaque; }

}  // namespace

EffectInstance::EffectInstance(const EffectDescriptor& desc, const Project& project)
    : EffectBase(desc.plugin()), desc_(desc), project_(project) {
  props_ = PropertySet("EffectInstance", &desc.props());
  auto acc = access(props_);
  openfx::host::propsets::EffectInstance inst(acc);
  double w = project.width, h = project.height, ox = project.originX, oy = project.originY;
  inst.setType(kOfxTypeImageEffectInstance)
      .setContext(desc.context().c_str())
      .setPluginHandle(desc.plugin().ofxPlugin())
      .setInstanceData(nullptr)
      .setProjectSize({w, h})
      .setProjectOffset({ox, oy})
      .setProjectExtent({std::max(w, ox + w), std::max(h, oy + h)})  // the extent is rooted at 0,0
      .setPixelAspectRatio(1.0)
      .setEffectDuration(project.frames)
      .setSequentialRender(0)
      .setFrameRate(project.frameRate)
      .setIsInteractive(0);

  Depth depth = pickDepth(desc, project.preferredDepth);
  for (const auto& descClip : desc.clips()) {
    auto clip = std::make_unique<Clip>(descClip->name(), "ClipInstance", &descClip->props());
    clip->owner = this;
    Components comps = pickComponents(descClip->props(), project.preferredComponents);
    auto cacc = access(clip->props());
    openfx::host::propsets::ClipInstance ci(cacc);
    ci.setType(kOfxTypeClip)
        .setName(descClip->name().c_str())
        .setPixelDepth(depthName(depth))
        .setComponents(componentsName(comps))
        .setUnmappedPixelDepth(depthName(depth))
        .setUnmappedComponents(componentsName(comps))
        .setPreMultiplication(premultFor(comps))
        .setPixelAspectRatio(1.0)
        .setFrameRate(project.frameRate)
        .setFrameRange({0.0, double(project.frames - 1)})
        .setUnmappedFrameRate(project.frameRate)
        .setUnmappedFrameRange({0.0, double(project.frames - 1)})
        .setFieldOrder(kOfxImageFieldNone)
        .setConnected(clip->isOutput() ? 1 : 0)
        .setContinuousSamples(0);
    clips_.push_back(std::move(clip));
  }
  for (const auto& descParam : desc.params().params()) {
    auto param = std::make_unique<Param>(descParam->name(), descParam->type(), &descParam->props());
    param->initFromDefault();
    scaleNormalisedDefault(*param);
    params_.params().push_back(std::move(param));
  }
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

EffectInstance::~EffectInstance() {
  if (created_) action(kOfxActionDestroyInstance, nullptr, nullptr);
}

OfxStatus EffectInstance::action(const char* name, PropertySet* in, PropertySet* out) {
  return plugin_.call(name, handle(), in ? in->handle() : nullptr, out ? out->handle() : nullptr);
}

void EffectInstance::create() {
  OfxStatus s = action(kOfxActionCreateInstance, nullptr, nullptr);
  if (!ok(s)) throw std::runtime_error("create instance failed: " + std::string(ofxStatusToString(s)));
  created_ = true;
}

void EffectInstance::connectInput(std::string_view clipName, std::shared_ptr<ImageBuffer> image) {
  Clip* c = clip(clipName);
  if (!c) throw std::runtime_error("no clip named " + std::string(clipName));
  if (c->isOutput()) throw std::runtime_error("cannot connect an image to the output clip");
  c->buffer = std::move(image);
  c->props().set(kOfxImageClipPropConnected, 0, 1);
}

void EffectInstance::setParam(std::string_view name, std::string_view value) {
  Param* p = params_.find(name);
  if (!p) throw std::runtime_error("no parameter named " + std::string(name));
  if (!p->parse(value))
    throw std::runtime_error("cannot parse \"" + std::string(value) + "\" for " + p->type() + " parameter " + p->name());
  if (p->type() == kOfxParamTypeStrChoice) {
    // The spec leaves a value outside the declared enums undefined and recommends
    // the host substitute the default, as it would for a removed option in a project.
    auto enums = p->props().getStrings(kOfxParamPropChoiceEnum);
    if (std::find(enums.begin(), enums.end(), p->str) == enums.end()) {
      std::string fallback = p->props().getString(kOfxParamPropDefault, 0, enums.empty() ? "" : enums.front());
      log::warn("{}: \"{}\" is not one of the declared enums; using \"{}\"", p->name(), p->str, fallback);
      p->str = fallback;
    }
  }

  PropertySet begin = PropertySet::forAction(kOfxActionBeginInstanceChanged, "inArgs");
  begin.set(kOfxPropChangeReason, 0, kOfxChangeUserEdited);
  action(kOfxActionBeginInstanceChanged, &begin, nullptr);

  PropertySet changed = PropertySet::forAction(kOfxActionInstanceChanged, "inArgs");
  auto acc = access(changed);
  openfx::host::propsets::ActionInstanceChanged_InArgs args(acc);
  args.setType(kOfxTypeParameter).setName(p->name().c_str()).setChangeReason(kOfxChangeUserEdited).setTime(0.0).setRenderScale({1.0, 1.0});
  action(kOfxActionInstanceChanged, &changed, nullptr);

  PropertySet end = PropertySet::forAction(kOfxActionEndInstanceChanged, "inArgs");
  end.set(kOfxPropChangeReason, 0, kOfxChangeUserEdited);
  action(kOfxActionEndInstanceChanged, &end, nullptr);
}

void EffectInstance::updateClipPreferences() {
  PropertySet out = PropertySet::forAction(kOfxImageEffectActionGetClipPreferences, "outArgs");
  out.set(kOfxImageEffectPropFrameRate, 0, project_.frameRate);
  out.set(kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone);
  out.set(kOfxImageClipPropContinuousSamples, 0, 0);
  out.set(kOfxImageEffectFrameVarying, 0, 0);
  Clip* output = clip(kOfxImageEffectOutputClipName);
  out.set(kOfxImageEffectPropPreMultiplication, 0, output ? premultFor(output->components()) : kOfxImageOpaque);
  for (const auto& c : clips_) {  // per-clip preferences are named by clip, so not in the metadata
    std::string comps = "OfxImageClipPropComponents_" + c->name(), depth = "OfxImageClipPropDepth_" + c->name(),
                par = "OfxImageClipPropPAR_" + c->name();
    out.define(comps, PropertySet::Type::String, 1);
    out.define(depth, PropertySet::Type::String, 1);
    out.define(par, PropertySet::Type::Double, 1);
    out.set(comps, 0, componentsName(c->components()));
    out.set(depth, 0, depthName(c->depth()));
    out.set(par, 0, 1.0);
  }
  OfxStatus s = action(kOfxImageEffectActionGetClipPreferences, nullptr, &out);
  if (s != kOfxStatOK) return;  // default reply: keep what we offered
  for (const auto& c : clips_) {
    Components comps = c->components();
    Depth depth = c->depth();
    componentsFromName(out.getString("OfxImageClipPropComponents_" + c->name()), &comps);
    depthFromName(out.getString("OfxImageClipPropDepth_" + c->name()), &depth);
    if (comps != c->components() || depth != c->depth()) {
      log::debug("clip {}: plugin prefers {} {}", c->name(), componentsName(comps), depthName(depth));
      c->props().set(kOfxImageEffectPropComponents, 0, componentsName(comps));
      c->props().set(kOfxImageEffectPropPixelDepth, 0, depthName(depth));
      c->props().set(kOfxImageEffectPropPreMultiplication, 0, premultFor(comps));
    }
  }
}

OfxRectD EffectInstance::regionOfDefinition(double time) {
  PropertySet in = PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "inArgs");
  auto acc = access(in);
  openfx::host::propsets::ImageEffectActionGetRegionOfDefinition_InArgs args(acc);
  args.setTime(time).setRenderScale({1.0, 1.0});
  PropertySet out = PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "outArgs");
  if (action(kOfxImageEffectActionGetRegionOfDefinition, &in, &out) == kOfxStatOK) {
    OfxRectD rod;
    rod.x1 = out.getDouble(kOfxImageEffectPropRegionOfDefinition, 0);
    rod.y1 = out.getDouble(kOfxImageEffectPropRegionOfDefinition, 1);
    rod.x2 = out.getDouble(kOfxImageEffectPropRegionOfDefinition, 2);
    rod.y2 = out.getDouble(kOfxImageEffectPropRegionOfDefinition, 3);
    return rod;
  }
  // Default: the union of the connected inputs, else the project.
  OfxRectD rod{0, 0, 0, 0};
  bool any = false;
  for (const auto& c : clips_) {
    if (c->isOutput() || !c->buffer) continue;
    const OfxRectI& b = c->buffer->bounds();
    if (!any) rod = {double(b.x1), double(b.y1), double(b.x2), double(b.y2)};
    else rod = {std::min(rod.x1, double(b.x1)), std::min(rod.y1, double(b.y1)), std::max(rod.x2, double(b.x2)), std::max(rod.y2, double(b.y2))};
    any = true;
  }
  OfxRectI pr = projectRect();
  return any ? rod : OfxRectD{double(pr.x1), double(pr.y1), double(pr.x2), double(pr.y2)};
}

OfxRectI EffectInstance::projectRect() const {
  return {project_.originX, project_.originY, project_.originX + project_.width, project_.originY + project_.height};
}

bool EffectInstance::isIdentity(double time, const OfxRectI& window, std::string* identityClip) {
  PropertySet in = PropertySet::forAction(kOfxImageEffectActionIsIdentity, "inArgs");
  auto acc = access(in);
  openfx::host::propsets::ImageEffectActionIsIdentity_InArgs args(acc);
  args.setTime(time).setFieldToRender(kOfxImageFieldNone).setRenderWindow({window.x1, window.y1, window.x2, window.y2}).setRenderScale({1.0, 1.0});
  PropertySet out = PropertySet::forAction(kOfxImageEffectActionIsIdentity, "outArgs");
  out.set(kOfxPropTime, 0, time);
  if (action(kOfxImageEffectActionIsIdentity, &in, &out) != kOfxStatOK) return false;
  *identityClip = out.getString(kOfxPropName);
  return !identityClip->empty();
}

std::shared_ptr<ImageBuffer> EffectInstance::render(double time) {
  Clip* output = clip(kOfxImageEffectOutputClipName);
  if (!output) throw std::runtime_error("effect has no output clip");
  for (const auto& c : clips_)
    if (!c->isOutput() && !c->buffer && !c->props().getInt(kOfxImageClipPropOptional))
      log::warn("input clip {} is not connected", c->name());

  // Render the effect's region of definition clipped to the project: a
  // generator may declare an infinite region, and a host only asks for what it needs.
  OfxRectD rod = regionOfDefinition(time);
  OfxRectI pr = projectRect();
  OfxRectI window{int(std::floor(std::max(rod.x1, double(pr.x1)))), int(std::floor(std::max(rod.y1, double(pr.y1)))),
                  int(std::ceil(std::min(rod.x2, double(pr.x2)))), int(std::ceil(std::min(rod.y2, double(pr.y2))))};
  int padding = 0;
  for (const auto& c : clips_)
    if (!c->isOutput() && c->buffer) padding = std::max(padding, c->buffer->rowBytes() - c->buffer->width() * c->buffer->channels() * c->buffer->bytesPerChannel());
  if (window.x2 <= window.x1 || window.y2 <= window.y1) {
    // Nothing of the effect falls inside the project: the frame is empty, and
    // the plugin must not be asked to render outside its region of definition.
    log::info("region of definition ({},{})-({},{}) is outside the project; rendering nothing", rod.x1, rod.y1, rod.x2, rod.y2);
    output_ = ImageBuffer::create(pr, output->components(), output->depth(), padding);
    output->buffer = output_;
    return output_;
  }
  output_ = ImageBuffer::create(window, output->components(), output->depth(), padding);
  output->buffer = output_;

  std::string identityClip;
  if (isIdentity(time, window, &identityClip)) {
    log::info("plugin reports identity from clip {}", identityClip);
    if (Clip* src = clip(identityClip); src && src->buffer) {
      for (int y = window.y1; y < window.y2; ++y)
        for (int x = window.x1; x < window.x2; ++x) output_->setPixel(x, y, src->buffer->pixel(x, y));
    }
    return output_;
  }

  PropertySet seq = PropertySet::forAction(kOfxImageEffectActionBeginSequenceRender, "inArgs");
  {
    auto acc = access(seq);
    openfx::host::propsets::ImageEffectActionBeginSequenceRender_InArgs args(acc);
    args.setFrameRange({time, time}).setFrameStep(1.0).setIsInteractive(0).setRenderScale({1.0, 1.0})
        .setSequentialRenderStatus(0).setInteractiveRenderStatus(0).setOpenGLEnabled(0);
  }
  action(kOfxImageEffectActionBeginSequenceRender, &seq, nullptr);

  PropertySet in = PropertySet::forAction(kOfxImageEffectActionRender, "inArgs");
  {
    auto acc = access(in);
    openfx::host::propsets::ImageEffectActionRender_InArgs args(acc);
    args.setTime(time)
        .setRenderWindow({window.x1, window.y1, window.x2, window.y2})
        .setRenderScale({1.0, 1.0})
        .setFieldToRender(kOfxImageFieldNone)
        .setSequentialRenderStatus(0)
        .setInteractiveRenderStatus(0)
        .setRenderQualityDraft(0)
        .setOpenGLEnabled(0);
  }
  OfxStatus s = action(kOfxImageEffectActionRender, &in, nullptr);
  action(kOfxImageEffectActionEndSequenceRender, &seq, nullptr);
  if (s != kOfxStatOK) throw std::runtime_error("render failed: " + std::string(ofxStatusToString(s)));

  for (auto& c : clips_) {
    if (!c->liveImages.empty()) {
      log::warn("plugin left {} image(s) of clip {} unreleased", c->liveImages.size(), c->name());
      c->liveImages.clear();
    }
    if (c->buffer)
      if (std::string where = c->buffer->checkGuards(); !where.empty())
        log::warn("plugin wrote outside the bounds of the {} image ({} the pixel data)", c->name(), where);
  }
  if (size_t bad = output_->nonFiniteCount()) log::warn("output has {} non-finite channel values", bad);
  return output_;
}

Image* EffectInstance::fetchImage(Clip& clip, double time) {
  std::shared_ptr<ImageBuffer> buffer = clip.buffer;
  if (!buffer) return nullptr;
  if (buffer->components() != clip.components() || buffer->depth() != clip.depth()) {
    clip.buffer = buffer = buffer->converted(clip.components(), clip.depth());  // cache the negotiated format
  }
  auto image = std::make_unique<Image>();
  image->buffer = buffer;
  image->clip = &clip;
  const OfxRectI& b = buffer->bounds();
  std::string id = clip.name() + "@" + std::to_string(time);
  auto acc = access(*image);
  openfx::host::propsets::Image props(acc);
  props.setType(kOfxTypeImage)
      .setPixelDepth(depthName(buffer->depth()))
      .setComponents(componentsName(buffer->components()))
      .setPreMultiplication(premultFor(buffer->components()))
      .setRenderScale({1.0, 1.0})
      .setPixelAspectRatio(1.0)
      .setData(buffer->data())
      .setBounds({b.x1, b.y1, b.x2, b.y2})
      .setRegionOfDefinition({b.x1, b.y1, b.x2, b.y2})
      .setRowBytes(buffer->rowBytes())
      .setField(kOfxImageFieldNone)
      .setUniqueIdentifier(id.c_str());
  clip.liveImages.push_back(std::move(image));
  log::debug("clipGetImage {} -> {} ({} live)", clip.name(), id, clip.liveImages.size());
  return clip.liveImages.back().get();
}

void EffectInstance::releaseImage(Image* image) {
  auto& live = image->clip->liveImages;
  log::debug("clipReleaseImage {} ({} live)", image->clip->name(), live.size());
  live.erase(std::remove_if(live.begin(), live.end(), [&](auto& p) { return p.get() == image; }), live.end());
}

// ---------------------------------------------------------------------------
// OfxImageEffectSuiteV1
// ---------------------------------------------------------------------------

namespace {

struct MemoryBlock {
  std::vector<std::byte> data;
};

OfxStatus getPropertySet(OfxImageEffectHandle effect, OfxPropertySetHandle* out) {
  if (!effect) return kOfxStatErrBadHandle;
  *out = EffectBase::from(effect)->props().handle();
  return kOfxStatOK;
}

OfxStatus getParamSet(OfxImageEffectHandle effect, OfxParamSetHandle* out) {
  if (!effect) return kOfxStatErrBadHandle;
  *out = EffectBase::from(effect)->params().handle();
  return kOfxStatOK;
}

OfxStatus clipDefine(OfxImageEffectHandle effect, const char* name, OfxPropertySetHandle* props) {
  auto* e = EffectBase::from(effect);
  if (!e || !name) return kOfxStatErrBadHandle;
  if (e->isInstance()) return kOfxStatErrBadHandle;  // clips are defined in DescribeInContext only
  auto* desc = static_cast<EffectDescriptor*>(e);
  Clip* c = desc->clip(name);
  if (!c) c = desc->defineClip(name);
  if (props) *props = c->props().handle();
  return kOfxStatOK;
}

OfxStatus clipGetHandle(OfxImageEffectHandle effect, const char* name, OfxImageClipHandle* clip, OfxPropertySetHandle* props) {
  auto* e = EffectBase::from(effect);
  if (!e || !name) return kOfxStatErrBadHandle;
  Clip* c = e->clip(name);
  if (!c) return kOfxStatErrUnknown;
  if (clip) *clip = c->handle();
  if (props) *props = c->props().handle();
  return kOfxStatOK;
}

OfxStatus clipGetPropertySet(OfxImageClipHandle clip, OfxPropertySetHandle* props) {
  if (!clip) return kOfxStatErrBadHandle;
  *props = Clip::from(clip)->props().handle();
  return kOfxStatOK;
}

OfxStatus clipGetImage(OfxImageClipHandle clip, OfxTime time, const OfxRectD*, OfxPropertySetHandle* image) {
  Clip* c = Clip::from(clip);
  if (!c || !c->owner) return kOfxStatErrBadHandle;
  Image* img = c->owner->fetchImage(*c, time);
  if (!img) {
    log::debug("clipGetImage on unconnected clip {}", c->name());
    return kOfxStatFailed;
  }
  *image = img->handle();
  return kOfxStatOK;
}

OfxStatus clipReleaseImage(OfxPropertySetHandle imageHandle) {
  if (!imageHandle) return kOfxStatErrBadHandle;
  Image* image = Image::from(imageHandle);
  if (!image->clip || !image->clip->owner) return kOfxStatErrBadHandle;
  image->clip->owner->releaseImage(image);
  return kOfxStatOK;
}

OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle clip, OfxTime time, OfxRectD* bounds) {
  Clip* c = Clip::from(clip);
  if (!c || !c->owner) return kOfxStatErrBadHandle;
  if (c->buffer) {
    const OfxRectI& b = c->buffer->bounds();
    *bounds = {double(b.x1), double(b.y1), double(b.x2), double(b.y2)};
    return kOfxStatOK;
  }
  if (c->isOutput()) {
    *bounds = c->owner->regionOfDefinition(time);
    return kOfxStatOK;
  }
  return kOfxStatFailed;
}

int abort(OfxImageEffectHandle) { return 0; }

OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t nBytes, OfxImageMemoryHandle* handle) {
  auto* block = new MemoryBlock{std::vector<std::byte>(nBytes ? nBytes : 1)};
  *handle = reinterpret_cast<OfxImageMemoryHandle>(block);
  return kOfxStatOK;
}

OfxStatus imageMemoryFree(OfxImageMemoryHandle handle) {
  delete reinterpret_cast<MemoryBlock*>(handle);
  return kOfxStatOK;
}

OfxStatus imageMemoryLock(OfxImageMemoryHandle handle, void** ptr) {
  if (!handle) return kOfxStatErrBadHandle;
  *ptr = reinterpret_cast<MemoryBlock*>(handle)->data.data();
  return kOfxStatOK;
}

OfxStatus imageMemoryUnlock(OfxImageMemoryHandle) { return kOfxStatOK; }

const OfxImageEffectSuiteV1 kEffectSuite = {
    getPropertySet, getParamSet, clipDefine, clipGetHandle, clipGetPropertySet, clipGetImage, clipReleaseImage,
    clipGetRegionOfDefinition, abort, imageMemoryAlloc, imageMemoryFree, imageMemoryLock, imageMemoryUnlock,
};

// ---------------------------------------------------------------------------
// OfxParameterSuiteV1
// ---------------------------------------------------------------------------

OfxStatus paramDefine(OfxParamSetHandle set, const char* type, const char* name, OfxPropertySetHandle* props) {
  auto* ps = ParamSet::from(set);
  if (!ps || !type || !name) return kOfxStatErrBadHandle;
  if (ps->owner()->isInstance()) return kOfxStatErrBadHandle;
  if (ps->find(name)) return kOfxStatErrExists;
  try {
    Param* p = static_cast<EffectDescriptor*>(ps->owner())->defineParam(type, name);
    if (props) *props = p->props().handle();
  } catch (const std::exception& e) {
    log::warn("paramDefine {}: {}", name, e.what());
    return kOfxStatErrUnsupported;
  }
  return kOfxStatOK;
}

OfxStatus paramGetHandle(OfxParamSetHandle set, const char* name, OfxParamHandle* param, OfxPropertySetHandle* props) {
  auto* ps = ParamSet::from(set);
  if (!ps || !name) return kOfxStatErrBadHandle;
  Param* p = ps->find(name);
  if (!p) return kOfxStatErrUnknown;
  if (param) *param = p->handle();
  if (props) *props = p->props().handle();
  return kOfxStatOK;
}

OfxStatus paramSetGetPropertySet(OfxParamSetHandle set, OfxPropertySetHandle* props) {
  if (!set) return kOfxStatErrBadHandle;
  *props = ParamSet::from(set)->props().handle();
  return kOfxStatOK;
}

OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle* props) {
  if (!param) return kOfxStatErrBadHandle;
  *props = Param::from(param)->props().handle();
  return kOfxStatOK;
}

// Reads the varargs as pointers of the param's value type and fills them.
OfxStatus readValues(Param* p, va_list args, double scale = 1.0) {
  switch (p->kind()) {
    case Param::Kind::Double:
      for (double v : p->doubles) *va_arg(args, double*) = v * scale;
      break;
    case Param::Kind::Int:
      for (int v : p->ints) *va_arg(args, int*) = static_cast<int>(v * scale);
      break;
    case Param::Kind::String:
      *va_arg(args, char**) = const_cast<char*>(p->str.c_str());
      break;
    case Param::Kind::None:
      return kOfxStatErrBadHandle;
  }
  return kOfxStatOK;
}

OfxStatus writeValues(Param* p, va_list args) {
  switch (p->kind()) {
    case Param::Kind::Double:
      for (double& v : p->doubles) v = va_arg(args, double);
      break;
    case Param::Kind::Int:
      for (int& v : p->ints) v = va_arg(args, int);
      break;
    case Param::Kind::String: {
      const char* s = va_arg(args, const char*);
      p->str = s ? s : "";
      break;
    }
    case Param::Kind::None:
      return kOfxStatErrBadHandle;
  }
  return kOfxStatOK;
}

OfxStatus paramGetValue(OfxParamHandle param, ...) {
  if (!param) return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, param);
  OfxStatus s = readValues(Param::from(param), args);
  va_end(args);
  return s;
}

OfxStatus paramGetValueAtTime(OfxParamHandle param, OfxTime time, ...) {
  if (!param) return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, time);
  OfxStatus s = readValues(Param::from(param), args);
  va_end(args);
  return s;
}

OfxStatus paramGetDerivative(OfxParamHandle param, OfxTime time, ...) {
  if (!param) return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, time);
  OfxStatus s = readValues(Param::from(param), args, 0.0);  // nothing animates
  va_end(args);
  return s;
}

OfxStatus paramGetIntegral(OfxParamHandle param, OfxTime t1, OfxTime t2, ...) {
  if (!param) return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, t2);
  OfxStatus s = readValues(Param::from(param), args, t2 - t1);
  va_end(args);
  return s;
}

OfxStatus paramSetValue(OfxParamHandle param, ...) {
  if (!param) return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, param);
  OfxStatus s = writeValues(Param::from(param), args);
  va_end(args);
  return s;
}

OfxStatus paramSetValueAtTime(OfxParamHandle param, OfxTime time, ...) {
  if (!param) return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, time);
  OfxStatus s = writeValues(Param::from(param), args);
  va_end(args);
  return s;
}

OfxStatus paramGetNumKeys(OfxParamHandle param, unsigned int* n) {
  if (!param) return kOfxStatErrBadHandle;
  *n = 0;
  return kOfxStatOK;
}
OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, OfxTime*) { return kOfxStatErrBadIndex; }
OfxStatus paramGetKeyIndex(OfxParamHandle, OfxTime, int, int*) { return kOfxStatFailed; }
OfxStatus paramDeleteKey(OfxParamHandle, OfxTime) { return kOfxStatErrBadIndex; }
OfxStatus paramDeleteAllKeys(OfxParamHandle param) { return param ? kOfxStatOK : kOfxStatErrBadHandle; }

OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime, const OfxRangeD*) {
  if (!to || !from) return kOfxStatErrBadHandle;
  Param *dst = Param::from(to), *src = Param::from(from);
  if (dst->kind() != src->kind() || dst->arity() != src->arity()) return kOfxStatErrValue;
  dst->doubles = src->doubles;
  dst->ints = src->ints;
  dst->str = src->str;
  return kOfxStatOK;
}

OfxStatus paramEditBegin(OfxParamSetHandle set, const char*) { return set ? kOfxStatOK : kOfxStatErrBadHandle; }
OfxStatus paramEditEnd(OfxParamSetHandle set) { return set ? kOfxStatOK : kOfxStatErrBadHandle; }

const OfxParameterSuiteV1 kParamSuite = {
    paramDefine, paramGetHandle, paramSetGetPropertySet, paramGetPropertySet, paramGetValue, paramGetValueAtTime,
    paramGetDerivative, paramGetIntegral, paramSetValue, paramSetValueAtTime, paramGetNumKeys, paramGetKeyTime,
    paramGetKeyIndex, paramDeleteKey, paramDeleteAllKeys, paramCopy, paramEditBegin, paramEditEnd,
};

}  // namespace

const OfxImageEffectSuiteV1* effectSuite() { return &kEffectSuite; }
const OfxParameterSuiteV1* paramSuite() { return &kParamSuite; }

}  // namespace testhost
