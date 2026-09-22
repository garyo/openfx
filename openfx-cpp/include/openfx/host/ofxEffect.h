// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// The generic image effect model a host works in: the objects behind the
// handles a plugin sees (effect descriptors and instances, clips, images,
// parameters), the image effect and parameter suites over them, and the action
// sequences the specification fixes. Everything a host decides for itself --
// where pixels live, which formats to negotiate, what a project is -- is left
// to a class derived from EffectInstance.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openfx/host/ofxPlugin.h"
#include "openfx/host/ofxPropSetAccessors.h"
#include "openfx/host/ofxPropertySet.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxMisc.h"
#include "openfx/ofxPixels.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/ofxStatusStrings.h"

namespace openfx::host {

class EffectBase;
class EffectInstance;

// The premultiplication state that follows from a component layout: only RGBA
// carries an alpha channel for the colour channels to be premultiplied by.
inline const char* premultiplicationFor(PixelComponents components) {
  return components == PixelComponents::RGBA ? kOfxImagePreMultiplied : kOfxImageOpaque;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

// One parameter of an effect descriptor or of an instance.
//
// The value storage below is the framework's built-in parameter store: one
// value per parameter and no animation, so a value is the same at every time.
// A host with keyframes keeps its own store and replaces the parameter suite.
class Param {
 public:
  enum class Kind { Double, Int, String, None };

  // With a parent, this is an instance of that descriptor parameter.
  Param(std::string name, std::string type, const PropertySet* parent);

  const std::string& name() const { return name_; }
  const std::string& type() const { return type_; }
  Kind kind() const { return kind_; }
  int arity() const { return arity_; }
  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }

  OfxParamHandle handle() { return reinterpret_cast<OfxParamHandle>(this); }
  static Param* from(OfxParamHandle h) { return reinterpret_cast<Param*>(h); }

  // Value storage: doubles, ints or a string depending on kind().
  std::vector<double> doubles;
  std::vector<int> ints;
  std::string str;

  void initFromDefault() {
    switch (kind_) {
      case Kind::Double:
        doubles.assign(arity_, 0.0);
        for (int i = 0; i < arity_; ++i)
          doubles[i] = props_.getDouble(kOfxParamPropDefault, i);
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

 private:
  std::string name_, type_;
  Kind kind_ = Kind::None;
  int arity_ = 0;
  PropertySet props_;
};

namespace detail {

// Which value kind, how many values, and which property set of the metadata
// each parameter type uses. The generated tables are keyed by property set
// name, not by parameter type, so the mapping lives here.
struct ParamKindInfo {
  const char* type;
  Param::Kind kind;
  int arity;
  const char* propSet;
};

inline constexpr ParamKindInfo kParamKinds[] = {
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

inline const ParamKindInfo* paramKind(std::string_view type) {
  for (const auto& k : kParamKinds)
    if (type == k.type)
      return &k;
  return nullptr;
}

// A PropertyAccessor over a host-owned property set, for the generated
// accessor classes. The result must outlive the accessor class built on it.
inline PropertyAccessor access(PropertySet& set) {
  return PropertyAccessor(set.handle(), PropertySet::suite());
}

}  // namespace detail

inline Param::Param(std::string name, std::string type, const PropertySet* parent)
    : name_(std::move(name)), type_(std::move(type)) {
  const detail::ParamKindInfo* info = detail::paramKind(type_);
  if (!info)
    throw std::runtime_error("unknown parameter type " + type_);
  kind_ = info->kind;
  arity_ = info->arity;
  props_ = PropertySet(info->propSet, parent);
  if (parent) {  // instance: the host-written animation state
    props_.set(kOfxParamPropIsAnimating, 0, 0);
    props_.set(kOfxParamPropIsAutoKeying, 0, 0);
    return;
  }
  // Descriptor: the defaults the metadata cannot express, being derived from
  // the parameter's name or its value type. The rest come from the metadata.
  props_.set(kOfxPropType, 0, kOfxTypeParameter);
  props_.set(kOfxPropName, 0, name_.c_str());
  props_.set(kOfxPropLabel, 0, name_.c_str());
  props_.set(kOfxPropShortLabel, 0, name_.c_str());
  props_.set(kOfxPropLongLabel, 0, name_.c_str());
  props_.set(kOfxParamPropType, 0, type_.c_str());
  props_.set(kOfxParamPropScriptName, 0, name_.c_str());
  props_.set(kOfxParamPropAnimates, 0,
             kind_ == Kind::Double || type_ == kOfxParamTypeInteger ? 1 : 0);
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
}

// The parameters of one effect, in definition order.
class ParamSet {
 public:
  explicit ParamSet(EffectBase* owner) : props_("ParameterSet"), owner_(owner) {}

  PropertySet& props() { return props_; }
  std::vector<std::unique_ptr<Param>>& params() { return params_; }
  const std::vector<std::unique_ptr<Param>>& params() const { return params_; }
  EffectBase* owner() { return owner_; }

  Param* find(std::string_view name) {
    for (auto& p : params_)
      if (p->name() == name)
        return p.get();
    return nullptr;
  }

  OfxParamSetHandle handle() { return reinterpret_cast<OfxParamSetHandle>(this); }
  static ParamSet* from(OfxParamSetHandle h) { return reinterpret_cast<ParamSet*>(h); }

 private:
  PropertySet props_;
  std::vector<std::unique_ptr<Param>> params_;
  EffectBase* owner_;
};

// ---------------------------------------------------------------------------
// Clips and images
// ---------------------------------------------------------------------------

// One clip of an effect descriptor or of an instance. A host derives from this
// to attach its own pixel storage to an instance's clips; see
// EffectInstance::makeClip.
class Clip {
 public:
  // propSet is "ClipDescriptor" or "ClipInstance"; an instance clip is
  // parented to its descriptor clip's property set.
  Clip(std::string name, std::string_view propSet, const PropertySet* parent)
      : name_(std::move(name)), props_(propSet, parent) {}
  virtual ~Clip() = default;

  Clip(const Clip&) = delete;
  Clip& operator=(const Clip&) = delete;

  const std::string& name() const { return name_; }
  bool isOutput() const { return name_ == kOfxImageEffectOutputClipName; }
  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }

  // As negotiated on a clip instance.
  PixelComponents components() const {
    return pixelComponentsFromName(props_.getString(kOfxImageEffectPropComponents))
        .value_or(PixelComponents::RGBA);
  }
  PixelDepth depth() const {
    return pixelDepthFromName(props_.getString(kOfxImageEffectPropPixelDepth))
        .value_or(PixelDepth::Float);
  }

  OfxImageClipHandle handle() { return reinterpret_cast<OfxImageClipHandle>(this); }
  static Clip* from(OfxImageClipHandle h) { return reinterpret_cast<Clip*>(h); }

  // The instance this clip belongs to, or null on a descriptor clip.
  EffectInstance* owner = nullptr;

 private:
  std::string name_;
  PropertySet props_;
};

// An image handle: the "Image" property set *is* the image, so the handle a
// plugin holds recovers it with a downcast rather than a side table. A host
// derives from this to attach the pixels.
class Image : public PropertySet {
 public:
  Image() : PropertySet("Image") {}
  // PropertySet has no virtual destructor, so this adds one; from() uses a
  // static_cast, which adjusts for that and keeps the handle round trip exact.
  virtual ~Image() = default;

  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  static Image* from(OfxPropertySetHandle h) {
    return static_cast<Image*>(PropertySet::from(h));
  }

  // The clip this image was fetched from.
  Clip* clip = nullptr;
};

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

// What an OfxImageEffectHandle points to: a descriptor or an instance.
class EffectBase {
 public:
  explicit EffectBase(Plugin& plugin) : plugin_(plugin), params_(this) {}
  virtual ~EffectBase() = default;

  EffectBase(const EffectBase&) = delete;
  EffectBase& operator=(const EffectBase&) = delete;

  virtual bool isInstance() const = 0;

  Plugin& plugin() const { return plugin_; }
  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }
  ParamSet& params() { return params_; }
  const ParamSet& params() const { return params_; }
  std::vector<std::unique_ptr<Clip>>& clips() { return clips_; }
  const std::vector<std::unique_ptr<Clip>>& clips() const { return clips_; }

  Clip* clip(std::string_view name) {
    for (auto& c : clips_)
      if (c->name() == name)
        return c.get();
    return nullptr;
  }

  OfxImageEffectHandle handle() { return reinterpret_cast<OfxImageEffectHandle>(this); }
  static EffectBase* from(OfxImageEffectHandle h) {
    return reinterpret_cast<EffectBase*>(h);
  }

 protected:
  Plugin& plugin_;
  PropertySet props_;
  ParamSet params_;
  std::vector<std::unique_ptr<Clip>> clips_;
};

// The result of Describe (no context) or of DescribeInContext, which is where
// the plugin defines its clips and parameters.
class EffectDescriptor : public EffectBase {
 public:
  EffectDescriptor(Plugin& plugin, const EffectDescriptor* global, std::string context)
      : EffectBase(plugin), context_(std::move(context)) {
    props_ = PropertySet("EffectDescriptor", global ? &global->props() : nullptr);
    if (global)
      return;
    // The host-written descriptor properties; the spec defaults come from the metadata.
    props_.set(kOfxPropType, 0, kOfxTypeImageEffect);
    props_.set(kOfxPluginPropFilePath, 0, plugin.bundlePath().string().c_str());
  }

  bool isInstance() const override { return false; }

  const std::string& context() const { return context_; }

  std::vector<std::string> supportedContexts() const {
    return props_.getStrings(kOfxImageEffectPropSupportedContexts);
  }

  std::vector<PixelDepth> supportedDepths() const {
    std::vector<PixelDepth> out;
    for (const auto& name : props_.getStrings(kOfxImageEffectPropSupportedPixelDepths))
      if (auto d = pixelDepthFromName(name))
        out.push_back(*d);
    return out;
  }

  Clip* defineClip(const std::string& name) {
    auto clip = std::make_unique<Clip>(name, "ClipDescriptor", nullptr);
    PropertySet& p = clip->props();
    // The name-derived defaults; the rest come from the metadata.
    p.set(kOfxPropType, 0, kOfxTypeClip);
    p.set(kOfxPropName, 0, name.c_str());
    p.set(kOfxPropLabel, 0, name.c_str());
    p.set(kOfxPropShortLabel, 0, name.c_str());
    p.set(kOfxPropLongLabel, 0, name.c_str());
    clips_.push_back(std::move(clip));
    return clips_.back().get();
  }

  Param* defineParam(const std::string& type, const std::string& name) {
    params_.params().push_back(std::make_unique<Param>(name, type, nullptr));
    return params_.params().back().get();
  }

 private:
  std::string context_;
};

// The project an instance lives in, as the specification has the host write it
// onto the instance's property set. A host keeps its own, richer notion of a
// project and fills this in for each instance it creates.
struct InstanceProject {
  OfxPointD size{64, 64};    // the project window, in canonical pixels
  OfxPointD offset{0, 0};    // its bottom-left corner
  OfxPointD extent{64, 64};  // the project size measured from the origin
  double pixelAspectRatio = 1.0;
  double frameRate = 25.0;
  double duration = 1.0;
  bool sequentialRender = false;
  bool interactive = false;
};

// The per-clip values a host decides when it instantiates an effect: the pixel
// format it negotiates for the clip and the clip's temporal properties.
// Everything else on a clip instance follows from these or from the descriptor.
struct ClipProperties {
  PixelComponents components = PixelComponents::RGBA;
  PixelDepth depth = PixelDepth::Float;
  double pixelAspectRatio = 1.0;
  double frameRate = 25.0;
  OfxRangeD frameRange{0, 0};
  const char* fieldOrder = kOfxImageFieldNone;
  bool continuousSamples = false;
};

// What a host asks a plugin to render. The defaults are the single frame, full
// scale, no fields case; the sequence calls also use frameRange and frameStep.
struct RenderArgs {
  OfxTime time = 0;
  OfxRectI renderWindow{0, 0, 0, 0};
  OfxPointD renderScale{1.0, 1.0};
  const char* field = kOfxImageFieldNone;
  bool sequentialRender = false;
  bool interactiveRender = false;
  bool draft = false;
  bool openGL = false;
  OfxRangeD frameRange{0, 0};
  double frameStep = 1.0;
};

// A live effect: the object the plugin's actions run against.
//
// Construction and destruction have a contract, because the hooks below are
// virtual and so are unavailable while a base class runs. A derived class
// calls createClips() at the end of its own constructor, and destroyInstance()
// at the start of its own destructor; the base destructor still destroys an
// instance that was created, for a host that has nothing to tear down.
class EffectInstance : public EffectBase {
 public:
  EffectInstance(const EffectDescriptor& contextDescriptor,
                 const InstanceProject& project);
  ~EffectInstance() override { destroyInstance(); }

  bool isInstance() const override { return true; }

  const EffectDescriptor& descriptor() const { return desc_; }
  const InstanceProject& project() const { return project_; }

  // kOfxActionCreateInstance.
  void create() {
    OfxStatus s = action(kOfxActionCreateInstance, nullptr, nullptr);
    if (!actionSucceeded(s))
      throw std::runtime_error("create instance failed: " +
                               std::string(ofxStatusToString(s)));
    created_ = true;
  }

  // Any action, against this instance.
  OfxStatus action(const char* name, PropertySet* inArgs, PropertySet* outArgs) {
    return plugin_.call(name, handle(), inArgs ? inArgs->handle() : nullptr,
                        outArgs ? outArgs->handle() : nullptr);
  }

  // BeginInstanceChanged / InstanceChanged / EndInstanceChanged around one
  // parameter change, which is what a plugin that caches state expects.
  void paramChanged(Param& param, const char* reason, OfxTime time,
                    OfxPointD renderScale);

  // kOfxImageEffectActionGetClipPreferences: offers the host's own preferences,
  // applies any the plugin changed back onto the clip instances, and returns
  // whether anything changed.
  bool getClipPreferences();

  // The effect's region of definition: what the plugin says, else the union of
  // its connected inputs, else the project.
  OfxRectD regionOfDefinition(OfxTime time);

  // The clip the plugin says the output is identical to, if it claims identity.
  std::optional<std::string> isIdentity(OfxTime time, const OfxRectI& window,
                                        OfxPointD renderScale, const char* field);

  OfxStatus beginSequenceRender(const RenderArgs& args);
  OfxStatus render(const RenderArgs& args);
  OfxStatus endSequenceRender(const RenderArgs& args);

  // --- Hooks the image effect suite calls on the host ----------------------

  // An image of this clip at this time, over region if the plugin asked for
  // one. Null means the clip has nothing to give (kOfxStatFailed).
  virtual Image* fetchImage(Clip& clip, OfxTime time, const OfxRectD* region) = 0;
  virtual void releaseImage(Image& image) = 0;
  // One clip's region of definition; false if it has none.
  virtual bool clipRegionOfDefinition(Clip& clip, OfxTime time, OfxRectD& out) {
    if (!clip.isOutput())
      return false;
    out = regionOfDefinition(time);
    return true;
  }
  // Whether the host wants the render in flight abandoned.
  virtual bool abort() { return false; }

 protected:
  // Creates the clip instances. Call from the derived constructor: makeClip()
  // and clipProperties() are virtual and reach the derived class only once its
  // own members exist.
  void createClips();

  // kOfxActionDestroyInstance, once. Call from the derived destructor if the
  // plugin may still use the host during it. Destructors run this, so it
  // swallows everything the action or the logging could throw.
  void destroyInstance() noexcept {
    if (!created_)
      return;
    created_ = false;
    try {
      action(kOfxActionDestroyInstance, nullptr, nullptr);
    } catch (...) {  // no logger here either: formatting a message can throw
      std::fprintf(stderr, "  ! %s: destroy instance failed\n",
                   plugin_.ofxPlugin()->pluginIdentifier);
    }
  }

  // One clip instance of this descriptor clip. The default is a plain Clip; a
  // host that attaches storage to its clips returns its own subclass, built
  // with Clip(descriptorClip.name(), "ClipInstance", &descriptorClip.props()).
  virtual std::unique_ptr<Clip> makeClip(const Clip& descriptorClip) {
    return std::make_unique<Clip>(descriptorClip.name(), "ClipInstance",
                                  &descriptorClip.props());
  }

  // The format and timing this host negotiates for one clip.
  virtual ClipProperties clipProperties(const Clip& descriptorClip) const = 0;

 private:
  const EffectDescriptor& desc_;
  InstanceProject project_;
  bool created_ = false;
};

inline EffectInstance::EffectInstance(const EffectDescriptor& contextDescriptor,
                                      const InstanceProject& project)
    : EffectBase(contextDescriptor.plugin()), desc_(contextDescriptor),
      project_(project) {
  props_ = PropertySet("EffectInstance", &contextDescriptor.props());
  PropertyAccessor acc = detail::access(props_);
  propsets::EffectInstance inst(acc);
  inst.setType(kOfxTypeImageEffectInstance)
      .setContext(contextDescriptor.context().c_str())
      .setPluginHandle(contextDescriptor.plugin().ofxPlugin())
      .setInstanceData(nullptr)
      .setProjectSize({project.size.x, project.size.y})
      .setProjectOffset({project.offset.x, project.offset.y})
      .setProjectExtent({project.extent.x, project.extent.y})
      .setPixelAspectRatio(project.pixelAspectRatio)
      .setEffectDuration(project.duration)
      .setSequentialRender(project.sequentialRender)
      .setFrameRate(project.frameRate)
      .setIsInteractive(project.interactive);

  for (const auto& descParam : contextDescriptor.params().params()) {
    auto param = std::make_unique<Param>(descParam->name(), descParam->type(),
                                         &descParam->props());
    param->initFromDefault();
    params_.params().push_back(std::move(param));
  }
}

inline void EffectInstance::createClips() {
  for (const auto& descClip : desc_.clips()) {
    std::unique_ptr<Clip> clip = makeClip(*descClip);
    clip->owner = this;
    const ClipProperties cp = clipProperties(*descClip);
    PropertyAccessor acc = detail::access(clip->props());
    propsets::ClipInstance ci(acc);
    ci.setType(kOfxTypeClip)
        .setName(descClip->name().c_str())
        .setPixelDepth(pixelDepthName(cp.depth))
        .setComponents(pixelComponentsName(cp.components))
        .setUnmappedPixelDepth(pixelDepthName(cp.depth))
        .setUnmappedComponents(pixelComponentsName(cp.components))
        .setPreMultiplication(premultiplicationFor(cp.components))
        .setPixelAspectRatio(cp.pixelAspectRatio)
        .setFrameRate(cp.frameRate)
        .setFrameRange({cp.frameRange.min, cp.frameRange.max})
        .setUnmappedFrameRate(cp.frameRate)
        .setUnmappedFrameRange({cp.frameRange.min, cp.frameRange.max})
        .setFieldOrder(cp.fieldOrder)
        .setConnected(clip->isOutput())
        .setContinuousSamples(cp.continuousSamples);
    clips_.push_back(std::move(clip));
  }
}

inline void EffectInstance::paramChanged(Param& param, const char* reason, OfxTime time,
                                         OfxPointD renderScale) {
  PropertySet begin = PropertySet::forAction(kOfxActionBeginInstanceChanged, "inArgs");
  begin.set(kOfxPropChangeReason, 0, reason);
  action(kOfxActionBeginInstanceChanged, &begin, nullptr);

  PropertySet changed = PropertySet::forAction(kOfxActionInstanceChanged, "inArgs");
  PropertyAccessor acc = detail::access(changed);
  propsets::ActionInstanceChanged_InArgs args(acc);
  args.setType(kOfxTypeParameter)
      .setName(param.name().c_str())
      .setChangeReason(reason)
      .setTime(time)
      .setRenderScale({renderScale.x, renderScale.y});
  action(kOfxActionInstanceChanged, &changed, nullptr);

  PropertySet end = PropertySet::forAction(kOfxActionEndInstanceChanged, "inArgs");
  end.set(kOfxPropChangeReason, 0, reason);
  action(kOfxActionEndInstanceChanged, &end, nullptr);
}

inline bool EffectInstance::getClipPreferences() {
  PropertySet out =
      PropertySet::forAction(kOfxImageEffectActionGetClipPreferences, "outArgs");
  Clip* output = clip(kOfxImageEffectOutputClipName);
  out.set(kOfxImageEffectPropFrameRate, 0, project_.frameRate);
  out.set(kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone);
  out.set(kOfxImageClipPropContinuousSamples, 0, 0);
  out.set(kOfxImageEffectFrameVarying, 0, 0);
  out.set(kOfxImageEffectPropPreMultiplication, 0,
          output ? premultiplicationFor(output->components()) : kOfxImageOpaque);
  for (const auto& c :
       clips_) {  // per-clip preferences are named by clip, so not in the metadata
    std::string comps = clipPrefComponentsProp(c->name()),
                depth = clipPrefDepthProp(c->name()), par = clipPrefPARProp(c->name());
    out.define(comps, PropertySet::Type::String, 1);
    out.define(depth, PropertySet::Type::String, 1);
    out.define(par, PropertySet::Type::Double, 1);
    out.set(comps, 0, pixelComponentsName(c->components()));
    out.set(depth, 0, pixelDepthName(c->depth()));
    out.set(par, 0, c->props().getDouble(kOfxImagePropPixelAspectRatio, 0, 1.0));
  }
  if (action(kOfxImageEffectActionGetClipPreferences, nullptr, &out) != kOfxStatOK)
    return false;  // default reply: keep what we offered
  bool changed = false;
  for (const auto& c : clips_) {
    PixelComponents comps = c->components();
    PixelDepth depth = c->depth();
    if (auto c2 =
            pixelComponentsFromName(out.getString(clipPrefComponentsProp(c->name()))))
      comps = *c2;
    if (auto d2 = pixelDepthFromName(out.getString(clipPrefDepthProp(c->name()))))
      depth = *d2;
    if (comps != c->components() || depth != c->depth()) {
      Logger::debug("clip {}: plugin prefers {} {}", c->name(),
                    pixelComponentsName(comps), pixelDepthName(depth));
      c->props().set(kOfxImageEffectPropComponents, 0, pixelComponentsName(comps));
      c->props().set(kOfxImageEffectPropPixelDepth, 0, pixelDepthName(depth));
      c->props().set(kOfxImageEffectPropPreMultiplication, 0,
                     premultiplicationFor(comps));
      changed = true;
    }
  }
  return changed;
}

inline OfxRectD EffectInstance::regionOfDefinition(OfxTime time) {
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "inArgs");
  PropertyAccessor acc = detail::access(in);
  propsets::ImageEffectActionGetRegionOfDefinition_InArgs args(acc);
  args.setTime(time).setRenderScale({1.0, 1.0});
  PropertySet out =
      PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "outArgs");
  if (action(kOfxImageEffectActionGetRegionOfDefinition, &in, &out) == kOfxStatOK) {
    return {out.getDouble(kOfxImageEffectPropRegionOfDefinition, 0),
            out.getDouble(kOfxImageEffectPropRegionOfDefinition, 1),
            out.getDouble(kOfxImageEffectPropRegionOfDefinition, 2),
            out.getDouble(kOfxImageEffectPropRegionOfDefinition, 3)};
  }
  // Default: the union of the connected inputs, else the project window.
  OfxRectD rod{0, 0, 0, 0};
  bool any = false;
  for (const auto& c : clips_) {
    OfxRectD b{0, 0, 0, 0};
    if (c->isOutput() || !clipRegionOfDefinition(*c, time, b))
      continue;
    rod = any ? OfxRectD{std::min(rod.x1, b.x1), std::min(rod.y1, b.y1),
                         std::max(rod.x2, b.x2), std::max(rod.y2, b.y2)}
              : b;
    any = true;
  }
  if (any)
    return rod;
  return {project_.offset.x, project_.offset.y, project_.offset.x + project_.size.x,
          project_.offset.y + project_.size.y};
}

inline std::optional<std::string> EffectInstance::isIdentity(OfxTime time,
                                                             const OfxRectI& window,
                                                             OfxPointD renderScale,
                                                             const char* field) {
  PropertySet in = PropertySet::forAction(kOfxImageEffectActionIsIdentity, "inArgs");
  PropertyAccessor acc = detail::access(in);
  propsets::ImageEffectActionIsIdentity_InArgs args(acc);
  args.setTime(time)
      .setFieldToRender(field)
      .setRenderWindow({window.x1, window.y1, window.x2, window.y2})
      .setRenderScale({renderScale.x, renderScale.y});
  PropertySet out = PropertySet::forAction(kOfxImageEffectActionIsIdentity, "outArgs");
  out.set(kOfxPropTime, 0, time);
  if (action(kOfxImageEffectActionIsIdentity, &in, &out) != kOfxStatOK)
    return std::nullopt;
  std::string name = out.getString(kOfxPropName);
  if (name.empty())
    return std::nullopt;
  return name;
}

inline OfxStatus EffectInstance::beginSequenceRender(const RenderArgs& a) {
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionBeginSequenceRender, "inArgs");
  PropertyAccessor acc = detail::access(in);
  propsets::ImageEffectActionBeginSequenceRender_InArgs args(acc);
  args.setFrameRange({a.frameRange.min, a.frameRange.max})
      .setFrameStep(a.frameStep)
      .setIsInteractive(a.interactiveRender)
      .setRenderScale({a.renderScale.x, a.renderScale.y})
      .setSequentialRenderStatus(a.sequentialRender)
      .setInteractiveRenderStatus(a.interactiveRender)
      .setOpenGLEnabled(a.openGL);
  return action(kOfxImageEffectActionBeginSequenceRender, &in, nullptr);
}

inline OfxStatus EffectInstance::endSequenceRender(const RenderArgs& a) {
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionEndSequenceRender, "inArgs");
  PropertyAccessor acc = detail::access(in);
  propsets::ImageEffectActionEndSequenceRender_InArgs args(acc);
  args.setFrameRange({a.frameRange.min, a.frameRange.max})
      .setFrameStep(a.frameStep)
      .setIsInteractive(a.interactiveRender)
      .setRenderScale({a.renderScale.x, a.renderScale.y})
      .setSequentialRenderStatus(a.sequentialRender)
      .setInteractiveRenderStatus(a.interactiveRender)
      .setOpenGLEnabled(a.openGL);
  return action(kOfxImageEffectActionEndSequenceRender, &in, nullptr);
}

inline OfxStatus EffectInstance::render(const RenderArgs& a) {
  PropertySet in = PropertySet::forAction(kOfxImageEffectActionRender, "inArgs");
  PropertyAccessor acc = detail::access(in);
  propsets::ImageEffectActionRender_InArgs args(acc);
  args.setTime(a.time)
      .setRenderWindow(
          {a.renderWindow.x1, a.renderWindow.y1, a.renderWindow.x2, a.renderWindow.y2})
      .setRenderScale({a.renderScale.x, a.renderScale.y})
      .setFieldToRender(a.field)
      .setSequentialRenderStatus(a.sequentialRender)
      .setInteractiveRenderStatus(a.interactiveRender)
      .setRenderQualityDraft(a.draft)
      .setOpenGLEnabled(a.openGL);
  return action(kOfxImageEffectActionRender, &in, nullptr);
}

// ---------------------------------------------------------------------------
// Plugin actions that produce a descriptor
// ---------------------------------------------------------------------------

inline std::unique_ptr<EffectDescriptor> Plugin::describe() {
  if (!loaded_)
    throw std::runtime_error(id() + ": describe before load");
  auto desc = std::make_unique<EffectDescriptor>(*this, nullptr, "");
  OfxStatus s = call(kOfxActionDescribe, desc->handle(), nullptr, nullptr);
  if (!actionSucceeded(s))
    throw std::runtime_error(id() + ": describe failed: " + ofxStatusToString(s));
  return desc;
}

inline std::unique_ptr<EffectDescriptor> Plugin::describeInContext(
    const EffectDescriptor& global, const std::string& context) {
  auto desc = std::make_unique<EffectDescriptor>(*this, &global, context);
  PropertySet inArgs =
      PropertySet::forAction(kOfxImageEffectActionDescribeInContext, "inArgs");
  inArgs.set(kOfxImageEffectPropContext, 0, context.c_str());
  OfxStatus s = call(kOfxImageEffectActionDescribeInContext, desc->handle(),
                     inArgs.handle(), nullptr);
  if (!actionSucceeded(s))
    throw std::runtime_error(id() + ": describe in context " + context +
                             " failed: " + ofxStatusToString(s));
  return desc;
}

// ---------------------------------------------------------------------------
// OfxImageEffectSuiteV1
// ---------------------------------------------------------------------------

namespace detail {

// Image memory a plugin allocates for its own scratch use: a plain heap block,
// with the handle pointing at it.
struct MemoryBlock {
  std::vector<std::byte> data;
};

inline OfxStatus getPropertySet(OfxImageEffectHandle effect, OfxPropertySetHandle* out) {
  if (!effect)
    return kOfxStatErrBadHandle;
  *out = EffectBase::from(effect)->props().handle();
  return kOfxStatOK;
}

inline OfxStatus getParamSet(OfxImageEffectHandle effect, OfxParamSetHandle* out) {
  if (!effect)
    return kOfxStatErrBadHandle;
  *out = EffectBase::from(effect)->params().handle();
  return kOfxStatOK;
}

inline OfxStatus clipDefine(OfxImageEffectHandle effect, const char* name,
                            OfxPropertySetHandle* props) {
  auto* e = EffectBase::from(effect);
  if (!e || !name)
    return kOfxStatErrBadHandle;
  if (e->isInstance())
    return kOfxStatErrBadHandle;  // clips are defined in DescribeInContext only
  auto* desc = static_cast<EffectDescriptor*>(e);
  Clip* c = desc->clip(name);
  if (!c)
    c = desc->defineClip(name);
  if (props)
    *props = c->props().handle();
  return kOfxStatOK;
}

inline OfxStatus clipGetHandle(OfxImageEffectHandle effect, const char* name,
                               OfxImageClipHandle* clip, OfxPropertySetHandle* props) {
  auto* e = EffectBase::from(effect);
  if (!e || !name)
    return kOfxStatErrBadHandle;
  Clip* c = e->clip(name);
  if (!c)
    return kOfxStatErrUnknown;
  if (clip)
    *clip = c->handle();
  if (props)
    *props = c->props().handle();
  return kOfxStatOK;
}

inline OfxStatus clipGetPropertySet(OfxImageClipHandle clip,
                                    OfxPropertySetHandle* props) {
  if (!clip)
    return kOfxStatErrBadHandle;
  *props = Clip::from(clip)->props().handle();
  return kOfxStatOK;
}

inline OfxStatus clipGetImage(OfxImageClipHandle clip, OfxTime time,
                              const OfxRectD* region, OfxPropertySetHandle* image) {
  Clip* c = Clip::from(clip);
  if (!c || !c->owner)
    return kOfxStatErrBadHandle;
  Image* img = c->owner->fetchImage(*c, time, region);
  if (!img) {
    Logger::debug("clipGetImage on unconnected clip {}", c->name());
    return kOfxStatFailed;
  }
  *image = img->handle();
  return kOfxStatOK;
}

inline OfxStatus clipReleaseImage(OfxPropertySetHandle imageHandle) {
  if (!imageHandle)
    return kOfxStatErrBadHandle;
  Image* image = Image::from(imageHandle);
  if (!image->clip || !image->clip->owner)
    return kOfxStatErrBadHandle;
  image->clip->owner->releaseImage(*image);
  return kOfxStatOK;
}

inline OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle clip, OfxTime time,
                                           OfxRectD* bounds) {
  Clip* c = Clip::from(clip);
  if (!c || !c->owner || !bounds)
    return kOfxStatErrBadHandle;
  return c->owner->clipRegionOfDefinition(*c, time, *bounds) ? kOfxStatOK
                                                             : kOfxStatFailed;
}

inline int abortRequested(OfxImageEffectHandle effect) {
  auto* e = EffectBase::from(effect);
  return e && e->isInstance() && static_cast<EffectInstance*>(e)->abort() ? 1 : 0;
}

inline OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t nBytes,
                                  OfxImageMemoryHandle* handle) {
  auto* block = new MemoryBlock{std::vector<std::byte>(nBytes ? nBytes : 1)};
  *handle = reinterpret_cast<OfxImageMemoryHandle>(block);
  return kOfxStatOK;
}

inline OfxStatus imageMemoryFree(OfxImageMemoryHandle handle) {
  delete reinterpret_cast<MemoryBlock*>(handle);
  return kOfxStatOK;
}

inline OfxStatus imageMemoryLock(OfxImageMemoryHandle handle, void** ptr) {
  if (!handle)
    return kOfxStatErrBadHandle;
  *ptr = reinterpret_cast<MemoryBlock*>(handle)->data.data();
  return kOfxStatOK;
}

inline OfxStatus imageMemoryUnlock(OfxImageMemoryHandle) { return kOfxStatOK; }

// ---------------------------------------------------------------------------
// OfxParameterSuiteV1
// ---------------------------------------------------------------------------

inline OfxStatus paramDefine(OfxParamSetHandle set, const char* type, const char* name,
                             OfxPropertySetHandle* props) {
  auto* ps = ParamSet::from(set);
  if (!ps || !type || !name)
    return kOfxStatErrBadHandle;
  if (ps->owner()->isInstance())
    return kOfxStatErrBadHandle;
  if (ps->find(name))
    return kOfxStatErrExists;
  try {
    Param* p = static_cast<EffectDescriptor*>(ps->owner())->defineParam(type, name);
    if (props)
      *props = p->props().handle();
  } catch (const std::exception& e) {
    Logger::warn("paramDefine {}: {}", name, e.what());
    return kOfxStatErrUnsupported;
  }
  return kOfxStatOK;
}

inline OfxStatus paramGetHandle(OfxParamSetHandle set, const char* name,
                                OfxParamHandle* param, OfxPropertySetHandle* props) {
  auto* ps = ParamSet::from(set);
  if (!ps || !name)
    return kOfxStatErrBadHandle;
  Param* p = ps->find(name);
  if (!p)
    return kOfxStatErrUnknown;
  if (param)
    *param = p->handle();
  if (props)
    *props = p->props().handle();
  return kOfxStatOK;
}

inline OfxStatus paramSetGetPropertySet(OfxParamSetHandle set,
                                        OfxPropertySetHandle* props) {
  if (!set)
    return kOfxStatErrBadHandle;
  *props = ParamSet::from(set)->props().handle();
  return kOfxStatOK;
}

inline OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle* props) {
  if (!param)
    return kOfxStatErrBadHandle;
  *props = Param::from(param)->props().handle();
  return kOfxStatOK;
}

// Reads the varargs as pointers of the param's value type and fills them.
inline OfxStatus readValues(Param* p, va_list args, double scale = 1.0) {
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

inline OfxStatus writeValues(Param* p, va_list args) {
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

inline OfxStatus paramGetValue(OfxParamHandle param, ...) {
  if (!param)
    return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, param);
  OfxStatus s = readValues(Param::from(param), args);
  va_end(args);
  return s;
}

inline OfxStatus paramGetValueAtTime(OfxParamHandle param, OfxTime time, ...) {
  if (!param)
    return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, time);
  OfxStatus s = readValues(Param::from(param), args);
  va_end(args);
  return s;
}

inline OfxStatus paramGetDerivative(OfxParamHandle param, OfxTime time, ...) {
  if (!param)
    return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, time);
  OfxStatus s = readValues(Param::from(param), args, 0.0);  // nothing animates
  va_end(args);
  return s;
}

inline OfxStatus paramGetIntegral(OfxParamHandle param, OfxTime t1, OfxTime t2, ...) {
  if (!param)
    return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, t2);
  OfxStatus s = readValues(Param::from(param), args, t2 - t1);
  va_end(args);
  return s;
}

inline OfxStatus paramSetValue(OfxParamHandle param, ...) {
  if (!param)
    return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, param);
  OfxStatus s = writeValues(Param::from(param), args);
  va_end(args);
  return s;
}

inline OfxStatus paramSetValueAtTime(OfxParamHandle param, OfxTime time, ...) {
  if (!param)
    return kOfxStatErrBadHandle;
  va_list args;
  va_start(args, time);
  OfxStatus s = writeValues(Param::from(param), args);
  va_end(args);
  return s;
}

inline OfxStatus paramGetNumKeys(OfxParamHandle param, unsigned int* n) {
  if (!param)
    return kOfxStatErrBadHandle;
  *n = 0;
  return kOfxStatOK;
}
inline OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, OfxTime*) {
  return kOfxStatErrBadIndex;
}
inline OfxStatus paramGetKeyIndex(OfxParamHandle, OfxTime, int, int*) {
  return kOfxStatFailed;
}
inline OfxStatus paramDeleteKey(OfxParamHandle, OfxTime) { return kOfxStatErrBadIndex; }
inline OfxStatus paramDeleteAllKeys(OfxParamHandle param) {
  return param ? kOfxStatOK : kOfxStatErrBadHandle;
}

inline OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime,
                           const OfxRangeD*) {
  if (!to || !from)
    return kOfxStatErrBadHandle;
  Param *dst = Param::from(to), *src = Param::from(from);
  if (dst->kind() != src->kind() || dst->arity() != src->arity())
    return kOfxStatErrValue;
  dst->doubles = src->doubles;
  dst->ints = src->ints;
  dst->str = src->str;
  return kOfxStatOK;
}

inline OfxStatus paramEditBegin(OfxParamSetHandle set, const char*) {
  return set ? kOfxStatOK : kOfxStatErrBadHandle;
}
inline OfxStatus paramEditEnd(OfxParamSetHandle set) {
  return set ? kOfxStatOK : kOfxStatErrBadHandle;
}

}  // namespace detail

// The image effect suite over the model above: the describe-time entries act on
// the descriptor, the instance-time ones dispatch through EffectInstance's hooks.
inline const OfxImageEffectSuiteV1* effectSuite() {
  static const OfxImageEffectSuiteV1 suite = {
      detail::getPropertySet,     detail::getParamSet,
      detail::clipDefine,         detail::clipGetHandle,
      detail::clipGetPropertySet, detail::clipGetImage,
      detail::clipReleaseImage,   detail::clipGetRegionOfDefinition,
      detail::abortRequested,     detail::imageMemoryAlloc,
      detail::imageMemoryFree,    detail::imageMemoryLock,
      detail::imageMemoryUnlock,
  };
  return &suite;
}

// The parameter suite over Param's value store: no animation, so a value is
// the same at every time, its derivative zero and its integral the value
// scaled by the interval.
inline const OfxParameterSuiteV1* paramSuite() {
  static const OfxParameterSuiteV1 suite = {
      detail::paramDefine,
      detail::paramGetHandle,
      detail::paramSetGetPropertySet,
      detail::paramGetPropertySet,
      detail::paramGetValue,
      detail::paramGetValueAtTime,
      detail::paramGetDerivative,
      detail::paramGetIntegral,
      detail::paramSetValue,
      detail::paramSetValueAtTime,
      detail::paramGetNumKeys,
      detail::paramGetKeyTime,
      detail::paramGetKeyIndex,
      detail::paramDeleteKey,
      detail::paramDeleteAllKeys,
      detail::paramCopy,
      detail::paramEditBegin,
      detail::paramEditEnd,
  };
  return &suite;
}

}  // namespace openfx::host
