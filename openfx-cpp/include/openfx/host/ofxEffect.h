// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// The generic image effect model a host works in: the objects behind the
// handles a plugin sees (effect descriptors and instances, clips, images,
// parameters), the image effect and parameter suites over them, and the action
// sequences the specification fixes. Everything a host decides for itself --
// where pixels live, which formats to negotiate, what a project is -- is left
// to a class derived from EffectInstance.

#include <ofxColour.h>
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openfx/host/ofxDefaultSuites.h"  // the timeline an effect's current time defaults to
#include "openfx/host/ofxPlugin.h"
#include "openfx/host/ofxPropSetAccessors.h"
#include "openfx/host/ofxPropertySet.h"
#include "openfx/ofxExceptions.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxMisc.h"
#include "openfx/ofxPixels.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/ofxStatusStrings.h"

namespace openfx::host {

class EffectBase;
class EffectInstance;
class ParamSet;

// The premultiplication state that follows from a component layout: only RGBA
// carries an alpha channel for the colour channels to be premultiplied by.
inline const char* premultiplicationFor(PixelComponents components) {
  return components == PixelComponents::RGBA ? kOfxImagePreMultiplied : kOfxImageOpaque;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

// One parameter value: the doubles, the ints or the string its kind uses.
struct ParamValue {
  std::vector<double> doubles;
  std::vector<int> ints;
  std::string str;
};

// One parameter of an effect descriptor or of an instance.
//
// The value store animates. A parameter holds a static value and, once keyed,
// its keys in increasing time order; value(t) interpolates them the way the
// parameter reference prescribes for the type: the numeric types interpolate
// (the integer ones rounding the result), the rest are held to the previous
// key. A host with a richer animation model -- curves with tangents,
// expressions -- keeps its own store and replaces the parameter suite.
//
// "Now", for a parameter, is its effect's EffectBase::currentTime(), which it
// reaches through the ParamSet that holds it; one in no set follows the
// default timeline.
class Param {
 public:
  enum class Kind { Double, Int, String, None };
  // How the value between two keys is found.
  enum class Interpolation { Linear, Step };

  // With a parent, this is an instance of that descriptor parameter.
  Param(std::string name, std::string type, const PropertySet* parent);

  const std::string& name() const { return name_; }
  const std::string& type() const { return type_; }
  Kind kind() const { return kind_; }
  int arity() const { return arity_; }
  Interpolation interpolation() const { return interpolation_; }
  // Whether keys may be set on it (kOfxParamPropAnimates): the per-type default
  // of the parameter reference, which a plugin may change while describing.
  bool animates() const { return props_.getInt(kOfxParamPropAnimates, 0, 0) != 0; }
  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }

  OfxParamHandle handle() { return reinterpret_cast<OfxParamHandle>(this); }
  static Param* from(OfxParamHandle h) { return reinterpret_cast<Param*>(h); }

  // --- Value ---------------------------------------------------------------

  // The value at a time: the static value while there are no keys, otherwise
  // the keys interpolated, held outside the range they cover.
  ParamValue value(OfxTime time) const;
  // The value now, which for a keyed parameter is the one at its effect's
  // current time.
  ParamValue value() const { return value(currentTime()); }
  // Sets the static value, or, once the parameter has keys, the value at its
  // effect's current time, as the specification has a host do.
  void setValue(const ParamValue& v);
  // Adds or replaces the key at this time. A parameter that does not animate
  // has no keys to add one to, so this sets its value instead.
  void setValueAtTime(OfxTime time, const ParamValue& v);
  // The slope of the curve at a time: zero where the value is held, outside
  // the keys, and with fewer than two keys.
  ParamValue derivative(OfxTime time) const;
  // The area under the curve between two times: trapezoids over the
  // interpolated pieces and rectangles over the held ones, so it is exact.
  ParamValue integral(OfxTime from, OfxTime to) const;

  // The string of a value, kept alive here until the next call so the C API
  // can hand a plugin a pointer to it.
  const char* holdString(const ParamValue& v) const {
    heldString_ = v.str;
    return heldString_.c_str();
  }

  // --- Keys ----------------------------------------------------------------

  unsigned numKeys() const { return static_cast<unsigned>(keys_.size()); }
  // The time of the nth key in time order; false if there is no such key.
  bool keyTime(unsigned index, OfxTime& time) const;
  // The index of the key at (direction 0), after (> 0) or before (< 0) a time,
  // which is the parameter suite's search; false if there is no such key.
  bool keyIndex(OfxTime time, int direction, int& index) const;
  bool deleteKey(OfxTime time);
  void deleteAllKeys();
  // Takes another parameter's value and keys, shifting the keys by offset and
  // keeping only those in range, if range is given and not empty.
  void copyFrom(const Param& other, OfxTime offset, const OfxRangeD* range);

  void initFromDefault();

 private:
  friend class ParamSet;  // which sets set_ when it takes the parameter

  struct Key {
    OfxTime time;
    ParamValue value;
  };

  // The suite searches for a key "at the indicated time (some small delta)".
  static constexpr double kSameTime = 1e-6;

  void putKey(OfxTime time, const ParamValue& v);
  void keysChanged() { props_.set(kOfxParamPropIsAnimating, 0, keys_.empty() ? 0 : 1); }
  // Defined once EffectBase is complete.
  OfxTime currentTime() const;

  std::string name_, type_;
  Kind kind_ = Kind::None;
  Interpolation interpolation_ = Interpolation::Step;
  int arity_ = 0;
  PropertySet props_;
  ParamValue value_;       // the value while there are no keys
  std::vector<Key> keys_;  // in increasing time order
  mutable std::string heldString_;
  const ParamSet* set_ = nullptr;  // the set holding it, if any
};

namespace detail {

// Which value kind, how many values, how it animates, and which property set
// of the metadata each parameter type uses. The generated tables are keyed by
// property set name, not by parameter type, so the mapping lives here.
//
// The animation column is the parameter reference's "Animation": the numeric
// types animate by default; the group, page and push button types cannot
// animate at all; the string, custom, boolean, choice and string-choice types
// animate only on a host that says it supports it, which this one does not, so
// they animate only if the plugin asks. None of those interpolate: a value
// between two keys is the one at the key before it.
struct ParamKindInfo {
  const char* type;
  Param::Kind kind;
  int arity;
  Param::Interpolation interpolation;
  bool animates;
  const char* propSet;
};

inline constexpr Param::Interpolation kLinear = Param::Interpolation::Linear;
inline constexpr Param::Interpolation kStep = Param::Interpolation::Step;

inline constexpr ParamKindInfo kParamKinds[] = {
    {kOfxParamTypeInteger, Param::Kind::Int, 1, kLinear, true, "ParamsByte"},
    {kOfxParamTypeInteger2D, Param::Kind::Int, 2, kLinear, true, "ParamsInt2D3D"},
    {kOfxParamTypeInteger3D, Param::Kind::Int, 3, kLinear, true, "ParamsInt2D3D"},
    {kOfxParamTypeBoolean, Param::Kind::Int, 1, kStep, false, "ParamsByte"},
    {kOfxParamTypeChoice, Param::Kind::Int, 1, kStep, false, "ParamsChoice"},
    {kOfxParamTypeStrChoice, Param::Kind::String, 1, kStep, false, "ParamsStrChoice"},
    {kOfxParamTypeDouble, Param::Kind::Double, 1, kLinear, true, "ParamsDouble1D"},
    {kOfxParamTypeDouble2D, Param::Kind::Double, 2, kLinear, true, "ParamsDouble2D3D"},
    {kOfxParamTypeDouble3D, Param::Kind::Double, 3, kLinear, true, "ParamsDouble2D3D"},
    {kOfxParamTypeRGB, Param::Kind::Double, 3, kLinear, true, "ParamsRGB"},
    {kOfxParamTypeRGBA, Param::Kind::Double, 4, kLinear, true, "ParamsRGBA"},
    {kOfxParamTypeString, Param::Kind::String, 1, kStep, false, "ParamsString"},
    {kOfxParamTypeCustom, Param::Kind::String, 1, kStep, false, "ParamsCustom"},
    {kOfxParamTypeGroup, Param::Kind::None, 0, kStep, false, "ParamsGroup"},
    {kOfxParamTypePage, Param::Kind::None, 0, kStep, false, "ParamsPage"},
    {kOfxParamTypePushButton, Param::Kind::None, 0, kStep, false, "ParamsByte"},
    {kOfxParamTypeParametric, Param::Kind::None, 0, kStep, false, "ParamsParametric"},
};

inline const ParamKindInfo* paramKind(std::string_view type) {
  for (const auto& k : kParamKinds)
    if (type == k.type)
      return &k;
  return nullptr;
}

// a and b interpolated a fraction f of the way from one to the other. Integers
// interpolate and round; a string has no arithmetic, so it is a's.
inline ParamValue mix(const ParamValue& a, const ParamValue& b, double f) {
  ParamValue out = a;
  for (size_t i = 0; i < out.doubles.size() && i < b.doubles.size(); ++i)
    out.doubles[i] = a.doubles[i] + (b.doubles[i] - a.doubles[i]) * f;
  for (size_t i = 0; i < out.ints.size() && i < b.ints.size(); ++i)
    out.ints[i] = static_cast<int>(std::lround(a.ints[i] + (b.ints[i] - a.ints[i]) * f));
  return out;
}

// The same numbers as v, all zero: what a rate of change or an area starts at.
inline ParamValue zeros(const ParamValue& v) {
  ParamValue out;
  out.doubles.assign(v.doubles.size(), 0.0);
  out.ints.assign(v.ints.size(), 0);
  return out;
}

}  // namespace detail

inline ParamValue Param::value(OfxTime time) const {
  if (keys_.empty())
    return value_;
  if (time <= keys_.front().time)
    return keys_.front().value;
  if (time >= keys_.back().time)
    return keys_.back().value;
  auto next = std::upper_bound(keys_.begin(), keys_.end(), time,
                               [](OfxTime t, const Key& k) { return t < k.time; });
  const Key& before = *(next - 1);
  if (interpolation_ == Interpolation::Step)
    return before.value;
  return detail::mix(before.value, next->value,
                     (time - before.time) / (next->time - before.time));
}

inline void Param::putKey(OfxTime time, const ParamValue& v) {
  auto at = std::lower_bound(keys_.begin(), keys_.end(), time,
                             [](const Key& k, OfxTime t) { return k.time < t; });
  if (at != keys_.end() && std::fabs(at->time - time) <= kSameTime)
    at->value = v;
  else
    keys_.insert(at, Key{time, v});
  keysChanged();
}

inline void Param::setValue(const ParamValue& v) {
  if (keys_.empty())
    value_ = v;
  else  // a keyed parameter has no value apart from its curve
    putKey(currentTime(), v);
}

inline void Param::setValueAtTime(OfxTime time, const ParamValue& v) {
  if (!animates()) {
    Logger::debug("{} does not animate: setting its value, not a key at {}", name_, time);
    setValue(v);
    return;
  }
  putKey(time, v);
}

inline ParamValue Param::derivative(OfxTime time) const {
  ParamValue out = detail::zeros(value(time));
  if (interpolation_ == Interpolation::Step || keys_.size() < 2 ||
      time < keys_.front().time || time >= keys_.back().time)
    return out;
  auto next = std::upper_bound(keys_.begin(), keys_.end(), time,
                               [](OfxTime t, const Key& k) { return t < k.time; });
  const Key& before = *(next - 1);
  const double dt = next->time - before.time;
  for (size_t i = 0; i < out.doubles.size() && i < next->value.doubles.size(); ++i)
    out.doubles[i] = (next->value.doubles[i] - before.value.doubles[i]) / dt;
  for (size_t i = 0; i < out.ints.size() && i < next->value.ints.size(); ++i)
    out.ints[i] =
        static_cast<int>(std::lround((next->value.ints[i] - before.value.ints[i]) / dt));
  return out;
}

inline ParamValue Param::integral(OfxTime from, OfxTime to) const {
  if (to < from) {
    ParamValue backwards = integral(to, from);
    for (double& d : backwards.doubles) d = -d;
    for (int& i : backwards.ints) i = -i;
    return backwards;
  }
  ParamValue total = detail::zeros(value(from));
  // Between two keys the curve is a straight line or a constant, so cutting
  // the range at every key inside it makes each piece exact.
  std::vector<OfxTime> cuts{from};
  for (const Key& k : keys_)
    if (k.time > from && k.time < to)
      cuts.push_back(k.time);
  cuts.push_back(to);
  for (size_t piece = 0; piece + 1 < cuts.size(); ++piece) {
    const double dt = cuts[piece + 1] - cuts[piece];
    const ParamValue a = value(cuts[piece]);
    const ParamValue b =
        interpolation_ == Interpolation::Step ? a : value(cuts[piece + 1]);
    for (size_t i = 0; i < total.doubles.size() && i < b.doubles.size(); ++i)
      total.doubles[i] += (a.doubles[i] + b.doubles[i]) * 0.5 * dt;
    for (size_t i = 0; i < total.ints.size() && i < b.ints.size(); ++i)
      total.ints[i] += static_cast<int>(std::lround((a.ints[i] + b.ints[i]) * 0.5 * dt));
  }
  return total;
}

inline bool Param::keyTime(unsigned index, OfxTime& time) const {
  if (index >= keys_.size())
    return false;
  time = keys_[index].time;
  return true;
}

inline bool Param::keyIndex(OfxTime time, int direction, int& index) const {
  index = -1;
  for (size_t i = 0; i < keys_.size(); ++i) {
    const OfxTime t = keys_[i].time;
    if (direction == 0 && std::fabs(t - time) <= kSameTime)
      index = static_cast<int>(i);
    else if (direction > 0 && t > time + kSameTime)
      index = static_cast<int>(i);  // the first key after the time
    else if (direction < 0 && t < time - kSameTime)
      index = static_cast<int>(i);  // the last key before it: keep looking
    if (index >= 0 && direction >= 0)
      break;
  }
  return index >= 0;
}

inline bool Param::deleteKey(OfxTime time) {
  int index = -1;
  if (!keyIndex(time, 0, index))
    return false;
  keys_.erase(keys_.begin() + index);
  keysChanged();
  return true;
}

inline void Param::deleteAllKeys() {
  keys_.clear();
  keysChanged();
}

inline void Param::copyFrom(const Param& other, OfxTime offset, const OfxRangeD* range) {
  // "To choose all animation in paramFrom set frameRange to [0, 0]".
  const bool whole = !range || (range->min == 0 && range->max == 0);
  std::vector<Key> copied;  // built first, so copying a parameter onto itself
  if (animates())           // shifts its keys rather than losing them
    for (const Key& k : other.keys_)
      if (whole || (k.time >= range->min && k.time <= range->max))
        copied.push_back(Key{k.time + offset, k.value});
  value_ = other.value_;
  keys_ = std::move(copied);
  keysChanged();
}

inline void Param::initFromDefault() {
  keys_.clear();
  keysChanged();
  value_ = ParamValue();
  switch (kind_) {
    case Kind::Double:
      value_.doubles.assign(arity_, 0.0);
      for (int i = 0; i < arity_; ++i)
        value_.doubles[i] = props_.getDouble(kOfxParamPropDefault, i);
      break;
    case Kind::Int:
      value_.ints.assign(arity_, 0);
      for (int i = 0; i < arity_; ++i)
        value_.ints[i] = props_.getInt(kOfxParamPropDefault, i);
      break;
    case Kind::String:
      value_.str = props_.getString(kOfxParamPropDefault);
      break;
    case Kind::None:
      break;
  }
}

inline Param::Param(std::string name, std::string type, const PropertySet* parent)
    : name_(std::move(name)), type_(std::move(type)) {
  const detail::ParamKindInfo* info = detail::paramKind(type_);
  if (!info)
    throw std::runtime_error("unknown parameter type " + type_);
  kind_ = info->kind;
  arity_ = info->arity;
  interpolation_ = info->interpolation;
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
  props_.set(kOfxParamPropAnimates, 0, info->animates ? 1 : 0);
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

  // Its parameters point back at it, and a plugin holds its handle.
  ParamSet(const ParamSet&) = delete;
  ParamSet& operator=(const ParamSet&) = delete;

  PropertySet& props() { return props_; }
  std::vector<std::unique_ptr<Param>>& params() { return params_; }
  const std::vector<std::unique_ptr<Param>>& params() const { return params_; }
  EffectBase* owner() { return owner_; }
  const EffectBase* owner() const { return owner_; }

  // Takes a parameter, which from then on reads the time from this set's
  // effect; one pushed straight onto params() follows the default timeline.
  Param* add(std::unique_ptr<Param> param) {
    param->set_ = this;
    params_.push_back(std::move(param));
    return params_.back().get();
  }

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

  // The time the parameter suite reads and sets a parameter's value at when
  // the plugin gives none (paramGetValue, paramSetValue): the default
  // timeline's current time. A host that keeps time per viewer or per
  // instance, behind a timeline suite of its own, says which time here.
  virtual OfxTime currentTime() const { return timeline().current; }

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

inline OfxTime Param::currentTime() const {
  const EffectBase* effect = set_ ? set_->owner() : nullptr;
  return effect ? effect->currentTime() : timeline().current;
}

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
    return params_.add(std::make_unique<Param>(name, type, nullptr));
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

  // Any action, against this instance. Every driver below sends its action
  // through here, between beforeAction() and afterAction(); plugin().call()
  // goes straight to the plugin's main entry and bypasses both.
  OfxStatus action(const char* name, PropertySet* inArgs, PropertySet* outArgs);

  // BeginInstanceChanged / InstanceChanged / EndInstanceChanged around one
  // parameter change, which is what a plugin that caches state expects.
  void paramChanged(Param& param, const char* reason, OfxTime time,
                    OfxPointD renderScale);

  // kOfxImageEffectActionGetClipPreferences: offers the host's own preferences,
  // applies any the plugin changed back onto the clip instances, and returns
  // whether anything changed.
  bool getClipPreferences();

  // kOfxImageEffectFrameVarying, as the last GetClipPreferences left it: the
  // effect produces a different image at every frame even if nothing changes.
  bool frameVarying() const { return frameVarying_; }

  // What the plugin asked for in Describe (kOfxImageEffectInstancePropSequentialRender):
  // 0 it does not care, 1 it must be rendered in frame order to be correct,
  // 2 it would rather be. The host writes its own answer onto the instance,
  // so the plugin's request is read back from the descriptor.
  int sequentialRenderRequest() const {
    return desc_.props().getInt(kOfxImageEffectInstancePropSequentialRender, 0, 0);
  }

  // The effect's region of definition: what the plugin says, else the union of
  // its connected inputs, else the project.
  OfxRectD regionOfDefinition(OfxTime time);

  // The clip the plugin says the output is identical to, if it claims identity.
  std::optional<std::string> isIdentity(OfxTime time, const OfxRectI& window,
                                        OfxPointD renderScale, const char* field);

  // kOfxImageEffectActionGetRegionsOfInterest: the region the plugin needs of
  // each input clip to render this one. Clips the plugin says nothing about
  // keep the requested region, as the specification's default has it.
  std::map<std::string, OfxRectD> getRegionsOfInterest(OfxTime time,
                                                       const OfxRectD& regionOfInterest,
                                                       OfxPointD renderScale);

  // kOfxImageEffectActionGetFramesNeeded: the frame ranges the plugin needs
  // from each input clip, defaulting to the single frame being rendered.
  std::map<std::string, std::vector<OfxRangeD>> getFramesNeeded(OfxTime time);

  // kOfxImageEffectActionGetTimeDomain: the frame range the plugin can produce
  // images over, if it answers.
  std::optional<OfxRangeD> getTimeDomain();

  // kOfxImageEffectActionGetOutputColourspace: the colourspace the plugin will
  // write its output in, given the ones the host would prefer. May be a
  // cross-reference to an input clip (see clipColourspaceRefTarget).
  std::optional<std::string> getOutputColourspace(
      const std::vector<std::string>& preferred);

  // The actions that carry no arguments at all.
  void purgeCaches() { action(kOfxActionPurgeCaches, nullptr, nullptr); }
  void syncPrivateData() { action(kOfxActionSyncPrivateData, nullptr, nullptr); }
  void beginInstanceEdit() { action(kOfxActionBeginInstanceEdit, nullptr, nullptr); }
  void endInstanceEdit() { action(kOfxActionEndInstanceEdit, nullptr, nullptr); }

  OfxStatus beginSequenceRender(const RenderArgs& args);
  OfxStatus render(const RenderArgs& args);
  OfxStatus endSequenceRender(const RenderArgs& args);

  // --- Hooks the image effect suite calls on the host ----------------------
  //
  // Each of these may throw: the suite entry that called it catches the
  // exception and hands the plugin a status instead -- the code of an
  // openfx::OfxException, kOfxStatErrMemory for std::bad_alloc, and
  // kOfxStatFailed for anything else -- or, from abort(), 0.

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

  // --- Hooks around every action ------------------------------------------
  //
  // A driver fills in the arguments it knows about and discards the rest.
  // These see every action action() sends, with the argument sets the driver
  // built, or null where the action has none: beforeAction() may add or change
  // a property -- one of the host's own, or one from a newer specification
  // than the driver's, such as kOfxImageEffectPropCudaStream on Render -- and
  // afterAction() may read back any the plugin wrote that the driver ignores.
  // Either may throw, to the driver's caller; from beforeAction() that means
  // the action is not sent. A DestroyInstance sent from the base destructor
  // reaches only these no-ops, the derived class being gone by then.
  virtual void beforeAction(const char* /*action*/, PropertySet* /*inArgs*/,
                            PropertySet* /*outArgs*/) {}
  virtual void afterAction(const char* /*action*/, PropertySet* /*inArgs*/,
                           PropertySet* /*outArgs*/, OfxStatus /*status*/) {}

 private:
  const EffectDescriptor& desc_;
  InstanceProject project_;
  bool created_ = false;
  bool frameVarying_ = false;
};

inline EffectInstance::EffectInstance(const EffectDescriptor& contextDescriptor,
                                      const InstanceProject& project)
    : EffectBase(contextDescriptor.plugin()), desc_(contextDescriptor),
      project_(project) {
  props_ = PropertySet("EffectInstance", &contextDescriptor.props());
  propsets::EffectInstance inst(props_.handle(), PropertySet::suite());
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
    params_.add(std::move(param));
  }
}

inline void EffectInstance::createClips() {
  for (const auto& descClip : desc_.clips()) {
    std::unique_ptr<Clip> clip = makeClip(*descClip);
    clip->owner = this;
    const ClipProperties cp = clipProperties(*descClip);
    propsets::ClipInstance ci(clip->props().handle(), PropertySet::suite());
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

inline OfxStatus EffectInstance::action(const char* name, PropertySet* inArgs,
                                        PropertySet* outArgs) {
  beforeAction(name, inArgs, outArgs);
  const OfxStatus status =
      plugin_.call(name, handle(), inArgs ? inArgs->handle() : nullptr,
                   outArgs ? outArgs->handle() : nullptr);
  afterAction(name, inArgs, outArgs, status);
  return status;
}

inline void EffectInstance::paramChanged(Param& param, const char* reason, OfxTime time,
                                         OfxPointD renderScale) {
  PropertySet begin = PropertySet::forAction(kOfxActionBeginInstanceChanged, "inArgs");
  begin.set(kOfxPropChangeReason, 0, reason);
  action(kOfxActionBeginInstanceChanged, &begin, nullptr);

  PropertySet changed = PropertySet::forAction(kOfxActionInstanceChanged, "inArgs");
  propsets::ActionInstanceChanged_InArgs args(changed.handle(), PropertySet::suite());
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
  // What the host offers. The plugin answers by writing over these, so a value
  // that comes back as it went out is no answer at all.
  const std::string offeredPremultiplication =
      output ? premultiplicationFor(output->components()) : kOfxImageOpaque;
  out.set(kOfxImageEffectPropFrameRate, 0, project_.frameRate);
  out.set(kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone);
  out.set(kOfxImageClipPropContinuousSamples, 0, 0);
  out.set(kOfxImageEffectFrameVarying, 0, 0);
  out.set(kOfxImageEffectPropPreMultiplication, 0, offeredPremultiplication.c_str());
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
    if (!c->isOutput())  // OFX 1.5: the colourspaces the plugin wants this input in
      out.define(clipPrefColourspacesProp(c->name()), PropertySet::Type::String, 0);
  }
  if (action(kOfxImageEffectActionGetClipPreferences, nullptr, &out) != kOfxStatOK)
    return false;  // default reply: keep what we offered
  frameVarying_ = out.getInt(kOfxImageEffectFrameVarying, 0, 0) != 0;

  bool changed = false;
  // Each answer goes onto the clip instance it is about, and is a change only
  // where it differs from what that clip already says.
  auto applyString = [&changed](Clip& c, std::string_view prop,
                                const std::string& value) {
    if (value.empty() || c.props().getString(prop) == value)
      return;
    c.props().set(prop, 0, value.c_str());
    changed = true;
  };
  auto applyDouble = [&changed](Clip& c, std::string_view prop, double value) {
    if (c.props().getDouble(prop) == value)
      return;
    c.props().set(prop, 0, value);
    changed = true;
  };
  auto applyInt = [&changed](Clip& c, std::string_view prop, int value) {
    if (c.props().getInt(prop) == value)
      return;
    c.props().set(prop, 0, value);
    changed = true;
  };

  const std::string premultiplication =
      out.getString(kOfxImageEffectPropPreMultiplication);
  const bool premultiplicationAnswered = premultiplication != offeredPremultiplication;
  for (const auto& c : clips_) {
    // A plugin's colourspace preferences live on the clip instance it asked
    // about, which is where it and the host read them back from.
    const std::vector<std::string> wanted =
        out.getStrings(clipPrefColourspacesProp(c->name()));
    for (size_t i = 0; i < wanted.size(); ++i)
      c->props().set(kOfxImageClipPropPreferredColourspaces, static_cast<int>(i),
                     wanted[i].c_str());
    applyDouble(
        *c, kOfxImagePropPixelAspectRatio,
        out.getDouble(clipPrefPARProp(c->name()), 0,
                      c->props().getDouble(kOfxImagePropPixelAspectRatio, 0, 1.0)));
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
      changed = true;
    }
    // The premultiplication the plugin may set is the output clip's; without
    // one, a clip's follows its components.
    applyString(*c, kOfxImageEffectPropPreMultiplication,
                c->isOutput() && premultiplicationAnswered ? premultiplication
                                                           : premultiplicationFor(comps));
  }
  // The frame rate, fielding and continuous sampling the plugin answers are
  // the output clip's too.
  if (output) {
    applyDouble(*output, kOfxImageEffectPropFrameRate,
                out.getDouble(kOfxImageEffectPropFrameRate, 0, project_.frameRate));
    applyString(*output, kOfxImageClipPropFieldOrder,
                out.getString(kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone));
    applyInt(*output, kOfxImageClipPropContinuousSamples,
             out.getInt(kOfxImageClipPropContinuousSamples, 0, 0));
  }
  return changed;
}

inline OfxRectD EffectInstance::regionOfDefinition(OfxTime time) {
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionGetRegionOfDefinition, "inArgs");
  propsets::ImageEffectActionGetRegionOfDefinition_InArgs args(in.handle(),
                                                               PropertySet::suite());
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
  propsets::ImageEffectActionIsIdentity_InArgs args(in.handle(), PropertySet::suite());
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

inline std::map<std::string, OfxRectD> EffectInstance::getRegionsOfInterest(
    OfxTime time, const OfxRectD& regionOfInterest, OfxPointD renderScale) {
  const std::array<double, 4> requested{regionOfInterest.x1, regionOfInterest.y1,
                                        regionOfInterest.x2, regionOfInterest.y2};
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionGetRegionsOfInterest, "inArgs");
  propsets::ImageEffectActionGetRegionsOfInterest_InArgs args(in.handle(),
                                                              PropertySet::suite());
  args.setTime(time)
      .setRenderScale({renderScale.x, renderScale.y})
      .setRegionOfInterest(requested)
      .setThumbnailRender("false");

  // The per-clip regions are named by clip, so not in the metadata; the host
  // must initialise every one to the requested region before the action.
  PropertySet out =
      PropertySet::forAction(kOfxImageEffectActionGetRegionsOfInterest, "outArgs");
  std::map<std::string, OfxRectD> regions;
  for (const auto& c : clips_) {
    if (c->isOutput())
      continue;
    const std::string name = clipRoIProp(c->name());
    out.define(name, PropertySet::Type::Double, 4);
    for (int i = 0; i < 4; ++i) out.set(name, i, requested[static_cast<size_t>(i)]);
    regions.emplace(c->name(), regionOfInterest);
  }
  if (action(kOfxImageEffectActionGetRegionsOfInterest, &in, &out) != kOfxStatOK)
    return regions;  // default reply: every clip keeps the requested region
  for (auto& [clipName, region] : regions) {
    const std::string name = clipRoIProp(clipName);
    region = {out.getDouble(name, 0), out.getDouble(name, 1), out.getDouble(name, 2),
              out.getDouble(name, 3)};
  }
  return regions;
}

inline std::map<std::string, std::vector<OfxRangeD>> EffectInstance::getFramesNeeded(
    OfxTime time) {
  PropertySet in = PropertySet::forAction(kOfxImageEffectActionGetFramesNeeded, "inArgs");
  propsets::ImageEffectActionGetFramesNeeded_InArgs args(in.handle(),
                                                         PropertySet::suite());
  args.setTime(time).setThumbnailRender("false");

  // One 2N-dimensional property per clip, named by clip; the host initialises
  // each to the single frame being rendered.
  PropertySet out =
      PropertySet::forAction(kOfxImageEffectActionGetFramesNeeded, "outArgs");
  std::map<std::string, std::vector<OfxRangeD>> needed;
  for (const auto& c : clips_) {
    if (c->isOutput())
      continue;
    const std::string name = clipFrameRangeProp(c->name());
    out.define(name, PropertySet::Type::Double, 0);  // 2N: the plugin sets the length
    out.set(name, 0, time);
    out.set(name, 1, time);
    needed.emplace(c->name(), std::vector<OfxRangeD>{{time, time}});
  }
  if (action(kOfxImageEffectActionGetFramesNeeded, &in, &out) != kOfxStatOK)
    return needed;  // default reply: the single frame from every clip
  for (auto& [clipName, ranges] : needed) {
    const std::string name = clipFrameRangeProp(clipName);
    int n = 0;
    if (out.dimension(name, &n) != kOfxStatOK || n < 2)
      continue;
    if (n % 2 != 0) {
      Logger::warn(
          "clip {}: {} frame range values, which is not a whole number of "
          "ranges; ignoring the last",
          clipName, n);
      --n;
    }
    ranges.clear();
    for (int i = 0; i + 1 < n; i += 2)
      ranges.push_back({out.getDouble(name, i), out.getDouble(name, i + 1)});
  }
  return needed;
}

inline std::optional<OfxRangeD> EffectInstance::getTimeDomain() {
  PropertySet out = PropertySet::forAction(kOfxImageEffectActionGetTimeDomain, "outArgs");
  if (action(kOfxImageEffectActionGetTimeDomain, nullptr, &out) != kOfxStatOK)
    return std::nullopt;
  propsets::ImageEffectActionGetTimeDomain_OutArgs args(out.handle(),
                                                        PropertySet::suite());
  const std::array<double, 2> range = args.frameRange();
  return OfxRangeD{range[0], range[1]};
}

inline std::optional<std::string> EffectInstance::getOutputColourspace(
    const std::vector<std::string>& preferred) {
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionGetOutputColourspace, "inArgs");
  propsets::ImageEffectActionGetOutputColourspace_InArgs args(in.handle(),
                                                              PropertySet::suite());
  for (size_t i = 0; i < preferred.size(); ++i)
    args.setPreferredColourspaces(preferred[i].c_str(), static_cast<int>(i));
  PropertySet out =
      PropertySet::forAction(kOfxImageEffectActionGetOutputColourspace, "outArgs");
  if (action(kOfxImageEffectActionGetOutputColourspace, &in, &out) != kOfxStatOK)
    return std::nullopt;  // default reply: the colourspace of the first input clip
  std::string space = out.getString(kOfxImageClipPropColourspace);
  if (space.empty())
    return std::nullopt;
  return space;
}

inline OfxStatus EffectInstance::beginSequenceRender(const RenderArgs& a) {
  PropertySet in =
      PropertySet::forAction(kOfxImageEffectActionBeginSequenceRender, "inArgs");
  propsets::ImageEffectActionBeginSequenceRender_InArgs args(in.handle(),
                                                             PropertySet::suite());
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
  propsets::ImageEffectActionEndSequenceRender_InArgs args(in.handle(),
                                                           PropertySet::suite());
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
  propsets::ImageEffectActionRender_InArgs args(in.handle(), PropertySet::suite());
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
  explicit MemoryBlock(size_t nBytes) : data(nBytes ? nBytes : 1) {}
  std::vector<std::byte> data;
};

// The entry points of this suite and the parameter suite are noexcept, and
// each that does any work runs it through callAtCBoundary: an exception from
// the host's own EffectInstance hooks, the logger or an allocation reaches the
// plugin as a status, never as an unwind.

inline OfxStatus getPropertySet(OfxImageEffectHandle effect,
                                OfxPropertySetHandle* out) noexcept {
  return callAtCBoundary([&] {
    if (!effect)
      return kOfxStatErrBadHandle;
    *out = EffectBase::from(effect)->props().handle();
    return kOfxStatOK;
  });
}

inline OfxStatus getParamSet(OfxImageEffectHandle effect,
                             OfxParamSetHandle* out) noexcept {
  return callAtCBoundary([&] {
    if (!effect)
      return kOfxStatErrBadHandle;
    *out = EffectBase::from(effect)->params().handle();
    return kOfxStatOK;
  });
}

inline OfxStatus clipDefine(OfxImageEffectHandle effect, const char* name,
                            OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary([&] {
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
  });
}

inline OfxStatus clipGetHandle(OfxImageEffectHandle effect, const char* name,
                               OfxImageClipHandle* clip,
                               OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary([&] {
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
  });
}

inline OfxStatus clipGetPropertySet(OfxImageClipHandle clip,
                                    OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary([&] {
    if (!clip)
      return kOfxStatErrBadHandle;
    *props = Clip::from(clip)->props().handle();
    return kOfxStatOK;
  });
}

inline OfxStatus clipGetImage(OfxImageClipHandle clip, OfxTime time,
                              const OfxRectD* region,
                              OfxPropertySetHandle* image) noexcept {
  return callAtCBoundary([&] {
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
  });
}

inline OfxStatus clipReleaseImage(OfxPropertySetHandle imageHandle) noexcept {
  return callAtCBoundary([&] {
    if (!imageHandle)
      return kOfxStatErrBadHandle;
    Image* image = Image::from(imageHandle);
    if (!image->clip || !image->clip->owner)
      return kOfxStatErrBadHandle;
    image->clip->owner->releaseImage(*image);
    return kOfxStatOK;
  });
}

inline OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle clip, OfxTime time,
                                           OfxRectD* bounds) noexcept {
  return callAtCBoundary([&] {
    Clip* c = Clip::from(clip);
    if (!c || !c->owner || !bounds)
      return kOfxStatErrBadHandle;
    return c->owner->clipRegionOfDefinition(*c, time, *bounds) ? kOfxStatOK
                                                               : kOfxStatFailed;
  });
}

// Not a status but a yes or no, so an exception is a no: carry on.
inline int abortRequested(OfxImageEffectHandle effect) noexcept {
  try {
    auto* e = EffectBase::from(effect);
    return e && e->isInstance() && static_cast<EffectInstance*>(e)->abort() ? 1 : 0;
  } catch (...) {
    logCurrentException("abort");
    return 0;
  }
}

// A request the host cannot meet leaves a null handle and kOfxStatErrMemory,
// whether the allocator refused it or the size was more than any can hold.
inline OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t nBytes,
                                  OfxImageMemoryHandle* handle) noexcept {
  *handle = nullptr;
  return callAtCBoundary(
      [&] {
        *handle = reinterpret_cast<OfxImageMemoryHandle>(
            std::make_unique<MemoryBlock>(nBytes).release());
        return kOfxStatOK;
      },
      kOfxStatErrMemory);
}

inline OfxStatus imageMemoryFree(OfxImageMemoryHandle handle) noexcept {
  return callAtCBoundary([&] {
    delete reinterpret_cast<MemoryBlock*>(handle);
    return kOfxStatOK;
  });
}

inline OfxStatus imageMemoryLock(OfxImageMemoryHandle handle, void** ptr) noexcept {
  return callAtCBoundary([&] {
    if (!handle)
      return kOfxStatErrBadHandle;
    *ptr = reinterpret_cast<MemoryBlock*>(handle)->data.data();
    return kOfxStatOK;
  });
}

inline OfxStatus imageMemoryUnlock(OfxImageMemoryHandle) noexcept { return kOfxStatOK; }

// ---------------------------------------------------------------------------
// OfxParameterSuiteV1
// ---------------------------------------------------------------------------

inline OfxStatus paramDefine(OfxParamSetHandle set, const char* type, const char* name,
                             OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary(
      [&] {
        auto* ps = ParamSet::from(set);
        if (!ps || !type || !name)
          return kOfxStatErrBadHandle;
        if (ps->owner()->isInstance())
          return kOfxStatErrBadHandle;
        if (ps->find(name))
          return kOfxStatErrExists;
        if (!paramKind(type)) {
          Logger::warn("paramDefine {}: unknown parameter type {}", name, type);
          return kOfxStatErrUnsupported;
        }
        Param* p = static_cast<EffectDescriptor*>(ps->owner())->defineParam(type, name);
        if (props)
          *props = p->props().handle();
        return kOfxStatOK;
      },
      kOfxStatErrUnsupported);
}

inline OfxStatus paramGetHandle(OfxParamSetHandle set, const char* name,
                                OfxParamHandle* param,
                                OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary([&] {
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
  });
}

inline OfxStatus paramSetGetPropertySet(OfxParamSetHandle set,
                                        OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary([&] {
    if (!set)
      return kOfxStatErrBadHandle;
    *props = ParamSet::from(set)->props().handle();
    return kOfxStatOK;
  });
}

inline OfxStatus paramGetPropertySet(OfxParamHandle param,
                                     OfxPropertySetHandle* props) noexcept {
  return callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    *props = Param::from(param)->props().handle();
    return kOfxStatOK;
  });
}

// Fills the varargs, which are pointers of the param's value type, from v.
inline OfxStatus readValues(const Param* p, const ParamValue& v, va_list args) {
  switch (p->kind()) {
    case Param::Kind::Double:
      for (int i = 0; i < p->arity(); ++i)
        *va_arg(args, double*) =
            i < static_cast<int>(v.doubles.size()) ? v.doubles[i] : 0.0;
      break;
    case Param::Kind::Int:
      for (int i = 0; i < p->arity(); ++i)
        *va_arg(args, int*) = i < static_cast<int>(v.ints.size()) ? v.ints[i] : 0;
      break;
    case Param::Kind::String:
      *va_arg(args, char**) = const_cast<char*>(p->holdString(v));
      break;
    case Param::Kind::None:
      return kOfxStatErrBadHandle;
  }
  return kOfxStatOK;
}

// Reads the varargs, which are values of the param's value type, into v.
inline OfxStatus writeValues(const Param* p, va_list args, ParamValue& v) {
  switch (p->kind()) {
    case Param::Kind::Double:
      v.doubles.resize(p->arity());
      for (double& d : v.doubles) d = va_arg(args, double);
      break;
    case Param::Kind::Int:
      v.ints.resize(p->arity());
      for (int& i : v.ints) i = va_arg(args, int);
      break;
    case Param::Kind::String: {
      const char* s = va_arg(args, const char*);
      v.str = s ? s : "";
      break;
    }
    case Param::Kind::None:
      return kOfxStatErrBadHandle;
  }
  return kOfxStatOK;
}

// The variadic entry points start and end their argument lists outside the
// guarded body, so the list is ended however the body leaves.

inline OfxStatus paramGetValue(OfxParamHandle param, ...) noexcept {
  va_list args;
  va_start(args, param);
  const OfxStatus s = callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    Param* p = Param::from(param);
    return readValues(p, p->value(), args);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramGetValueAtTime(OfxParamHandle param, OfxTime time, ...) noexcept {
  va_list args;
  va_start(args, time);
  const OfxStatus s = callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    Param* p = Param::from(param);
    return readValues(p, p->value(time), args);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramGetDerivative(OfxParamHandle param, OfxTime time, ...) noexcept {
  va_list args;
  va_start(args, time);
  const OfxStatus s = callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    Param* p = Param::from(param);
    return readValues(p, p->derivative(time), args);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramGetIntegral(OfxParamHandle param, OfxTime t1, OfxTime t2,
                                  ...) noexcept {
  va_list args;
  va_start(args, t2);
  const OfxStatus s = callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    Param* p = Param::from(param);
    return readValues(p, p->integral(t1, t2), args);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramSetValue(OfxParamHandle param, ...) noexcept {
  va_list args;
  va_start(args, param);
  const OfxStatus s = callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    Param* p = Param::from(param);
    ParamValue v;
    const OfxStatus read = writeValues(p, args, v);
    if (read == kOfxStatOK)
      p->setValue(v);
    return read;
  });
  va_end(args);
  return s;
}

inline OfxStatus paramSetValueAtTime(OfxParamHandle param, OfxTime time, ...) noexcept {
  va_list args;
  va_start(args, time);
  const OfxStatus s = callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    Param* p = Param::from(param);
    ParamValue v;
    const OfxStatus read = writeValues(p, args, v);
    if (read == kOfxStatOK)
      p->setValueAtTime(time, v);
    return read;
  });
  va_end(args);
  return s;
}

inline OfxStatus paramGetNumKeys(OfxParamHandle param, unsigned int* n) noexcept {
  return callAtCBoundary([&] {
    if (!param || !n)
      return kOfxStatErrBadHandle;
    *n = Param::from(param)->numKeys();
    return kOfxStatOK;
  });
}

inline OfxStatus paramGetKeyTime(OfxParamHandle param, unsigned int nth,
                                 OfxTime* time) noexcept {
  return callAtCBoundary([&] {
    if (!param || !time)
      return kOfxStatErrBadHandle;
    return Param::from(param)->keyTime(nth, *time) ? kOfxStatOK : kOfxStatErrBadIndex;
  });
}

inline OfxStatus paramGetKeyIndex(OfxParamHandle param, OfxTime time, int direction,
                                  int* index) noexcept {
  return callAtCBoundary([&] {
    if (!param || !index)
      return kOfxStatErrBadHandle;
    return Param::from(param)->keyIndex(time, direction, *index) ? kOfxStatOK
                                                                 : kOfxStatFailed;
  });
}

inline OfxStatus paramDeleteKey(OfxParamHandle param, OfxTime time) noexcept {
  return callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    return Param::from(param)->deleteKey(time) ? kOfxStatOK : kOfxStatErrBadIndex;
  });
}

inline OfxStatus paramDeleteAllKeys(OfxParamHandle param) noexcept {
  return callAtCBoundary([&] {
    if (!param)
      return kOfxStatErrBadHandle;
    Param::from(param)->deleteAllKeys();
    return kOfxStatOK;
  });
}

inline OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime dstOffset,
                           const OfxRangeD* frameRange) noexcept {
  return callAtCBoundary([&] {
    if (!to || !from)
      return kOfxStatErrBadHandle;
    Param *dst = Param::from(to), *src = Param::from(from);
    if (dst->kind() != src->kind() || dst->arity() != src->arity())
      return kOfxStatErrValue;
    dst->copyFrom(*src, dstOffset, frameRange);
    return kOfxStatOK;
  });
}

inline OfxStatus paramEditBegin(OfxParamSetHandle set, const char*) noexcept {
  return set ? kOfxStatOK : kOfxStatErrBadHandle;
}
inline OfxStatus paramEditEnd(OfxParamSetHandle set) noexcept {
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

// The parameter suite over Param's value store, keys and all.
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
