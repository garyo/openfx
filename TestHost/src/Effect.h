// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "PropertySet.h"

namespace testhost {

class Plugin;

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------

enum class Depth { Byte, Short, Float };
enum class Components { RGBA, RGB, Alpha };

const char* depthName(Depth d);            // kOfxBitDepth*
const char* componentsName(Components c);  // kOfxImageComponent*
bool depthFromName(std::string_view name, Depth* out);
bool componentsFromName(std::string_view name, Components* out);

// Host-owned pixel storage: one image plane in an OFX layout (bottom-up rows).
class ImageBuffer {
 public:
  static std::shared_ptr<ImageBuffer> create(OfxRectI bounds, Components components, Depth depth);

  const OfxRectI& bounds() const { return bounds_; }
  int width() const { return bounds_.x2 - bounds_.x1; }
  int height() const { return bounds_.y2 - bounds_.y1; }
  Components components() const { return components_; }
  Depth depth() const { return depth_; }
  int channels() const;
  int bytesPerChannel() const;
  int rowBytes() const { return width() * channels() * bytesPerChannel(); }
  std::byte* data() { return data_.data(); }
  const std::byte* data() const { return data_.data(); }

  // Pixel access as RGBA floats in [0, 1], at absolute coordinates.
  std::array<float, 4> pixel(int x, int y) const;
  void setPixel(int x, int y, std::array<float, 4> rgba);

  std::shared_ptr<ImageBuffer> converted(Components components, Depth depth) const;

 private:
  ImageBuffer() = default;
  OfxRectI bounds_{};
  Components components_ = Components::RGBA;
  Depth depth_ = Depth::Float;
  std::vector<std::byte> data_;
};

// ---------------------------------------------------------------------------
// Objects behind the handles a plugin sees
// ---------------------------------------------------------------------------

class EffectInstance;

// An image handle: an "Image" property set describing a buffer.
struct Image : PropertySet {
  Image() : PropertySet("Image") {}
  static Image* from(OfxPropertySetHandle h) { return static_cast<Image*>(PropertySet::from(h)); }
  std::shared_ptr<ImageBuffer> buffer;
  class Clip* clip = nullptr;
};

class Clip {
 public:
  Clip(std::string name, std::string_view propSet, const PropertySet* parent);

  const std::string& name() const { return name_; }
  bool isOutput() const { return name_ == kOfxImageEffectOutputClipName; }
  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }

  OfxImageClipHandle handle() { return reinterpret_cast<OfxImageClipHandle>(this); }
  static Clip* from(OfxImageClipHandle h) { return reinterpret_cast<Clip*>(h); }

  // Instance-side state.
  EffectInstance* owner = nullptr;
  std::shared_ptr<ImageBuffer> buffer;             // connected input, or the rendered output
  std::vector<std::unique_ptr<Image>> liveImages;  // handles the plugin currently holds
  Components components() const;                   // as negotiated on the clip instance
  Depth depth() const;

 private:
  std::string name_;
  PropertySet props_;
};

class Param {
 public:
  enum class Kind { Double, Int, String, None };

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

  void initFromDefault();
  std::string valueString() const;
  // Parses "1.5", "0.2,0.4,0.6,1", "true", or a choice/string; false if unparseable.
  bool parse(std::string_view text);

 private:
  std::string name_, type_;
  Kind kind_ = Kind::None;
  int arity_ = 0;
  PropertySet props_;
};

class ParamSet {
 public:
  explicit ParamSet(class EffectBase* owner) : props_("ParameterSet"), owner_(owner) {}
  PropertySet& props() { return props_; }
  std::vector<std::unique_ptr<Param>>& params() { return params_; }
  const std::vector<std::unique_ptr<Param>>& params() const { return params_; }
  Param* find(std::string_view name);
  EffectBase* owner() { return owner_; }

  OfxParamSetHandle handle() { return reinterpret_cast<OfxParamSetHandle>(this); }
  static ParamSet* from(OfxParamSetHandle h) { return reinterpret_cast<ParamSet*>(h); }

 private:
  PropertySet props_;
  std::vector<std::unique_ptr<Param>> params_;
  EffectBase* owner_;
};

// What an OfxImageEffectHandle points to: a descriptor or an instance.
class EffectBase {
 public:
  explicit EffectBase(Plugin& plugin) : plugin_(plugin), params_(this) {}
  virtual ~EffectBase() = default;
  virtual bool isInstance() const = 0;

  Plugin& plugin() const { return plugin_; }
  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }
  ParamSet& params() { return params_; }
  const ParamSet& params() const { return params_; }
  std::vector<std::unique_ptr<Clip>>& clips() { return clips_; }
  const std::vector<std::unique_ptr<Clip>>& clips() const { return clips_; }
  Clip* clip(std::string_view name);

  OfxImageEffectHandle handle() { return reinterpret_cast<OfxImageEffectHandle>(this); }
  static EffectBase* from(OfxImageEffectHandle h) { return reinterpret_cast<EffectBase*>(h); }

 protected:
  Plugin& plugin_;
  PropertySet props_;
  ParamSet params_;
  std::vector<std::unique_ptr<Clip>> clips_;
};

// The result of Describe (no context) or DescribeInContext (clips and params defined).
class EffectDescriptor : public EffectBase {
 public:
  EffectDescriptor(Plugin& plugin, const EffectDescriptor* global, std::string context);
  bool isInstance() const override { return false; }

  const std::string& context() const { return context_; }
  std::vector<std::string> supportedContexts() const;
  std::vector<Depth> supportedDepths() const;
  std::string label() const;

  Clip* defineClip(const std::string& name);
  Param* defineParam(const std::string& type, const std::string& name);

  // Human-readable summary of contexts, clips and params.
  std::string describe() const;

 private:
  std::string context_;
};

struct Project {
  int width = 64;
  int height = 64;
  double frameRate = 24.0;
  int frames = 1;
};

class EffectInstance : public EffectBase {
 public:
  EffectInstance(const EffectDescriptor& contextDescriptor, const Project& project);
  ~EffectInstance() override;
  bool isInstance() const override { return true; }

  const EffectDescriptor& descriptor() const { return desc_; }
  const Project& project() const { return project_; }

  void create();  // kOfxActionCreateInstance
  void connectInput(std::string_view clipName, std::shared_ptr<ImageBuffer> image);
  void setParam(std::string_view name, std::string_view value);  // with the InstanceChanged actions
  void updateClipPreferences();                                   // kOfxImageEffectActionGetClipPreferences
  OfxRectD regionOfDefinition(double time);
  std::shared_ptr<ImageBuffer> render(double time);

  // Used by the suite: image handles for a clip at the clip's negotiated format.
  Image* fetchImage(Clip& clip, double time);
  void releaseImage(Image* image);

 private:
  OfxStatus action(const char* name, PropertySet* inArgs, PropertySet* outArgs);
  bool isIdentity(double time, const OfxRectI& window, std::string* identityClip);
  void scaleNormalisedDefault(Param& p);

  const EffectDescriptor& desc_;
  Project project_;
  bool created_ = false;
  std::shared_ptr<ImageBuffer> output_;
};

const OfxImageEffectSuiteV1* effectSuite();
const OfxParameterSuiteV1* paramSuite();

}  // namespace testhost
