// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/ofxPixels.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace testhost {

// The framework's effect model, which this host derives its own from.
using openfx::host::Clip;
using openfx::host::EffectDescriptor;
using openfx::host::Image;
using openfx::host::Param;
using openfx::host::ParamSet;
using openfx::host::PropertySet;

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------

using Depth = openfx::PixelDepth;
using Components = openfx::PixelComponents;

// Host-owned pixel storage: one image plane in an OFX layout (bottom-up rows).
class ImageBuffer {
 public:
  // rowPadding adds unused bytes to each row, as a host with aligned strides would.
  static std::shared_ptr<ImageBuffer> create(OfxRectI bounds, Components components,
                                             Depth depth, int rowPadding = 0);

  const OfxRectI& bounds() const { return bounds_; }
  int width() const { return bounds_.x2 - bounds_.x1; }
  int height() const { return bounds_.y2 - bounds_.y1; }
  Components components() const { return components_; }
  Depth depth() const { return depth_; }
  int channels() const;
  int bytesPerChannel() const;
  int rowBytes() const { return width() * channels() * bytesPerChannel() + rowPadding_; }
  std::byte* data() { return data_.data() + kGuardBytes; }
  const std::byte* data() const { return data_.data() + kGuardBytes; }
  // Pixel storage is surrounded by guard bytes; returns a description of any
  // the plugin overwrote ("before"/"after"), or empty.
  std::string checkGuards() const;

  // Pixel access as RGBA floats in [0, 1], at absolute coordinates.
  std::array<float, 4> pixel(int x, int y) const;
  void setPixel(int x, int y, std::array<float, 4> rgba);

  std::shared_ptr<ImageBuffer> converted(Components components, Depth depth) const;
  // The same pixels with the bounds moved to start at origin, and a row padding.
  std::shared_ptr<ImageBuffer> reframed(OfxPointI origin, int rowPadding) const;
  // Number of NaN or infinite channel values (always 0 for integer depths).
  size_t nonFiniteCount() const;

 private:
  static constexpr size_t kGuardBytes = 256;
  static constexpr std::byte kGuardPattern{0xA5};
  ImageBuffer() = default;
  OfxRectI bounds_{};
  int rowPadding_ = 0;
  Components components_ = Components::RGBA;
  Depth depth_ = Depth::Float;
  std::vector<std::byte> data_;
};

// ---------------------------------------------------------------------------
// The host's own clips and images
// ---------------------------------------------------------------------------

// An image handle with the buffer it describes behind it.
struct TestImage : Image {
  std::shared_ptr<ImageBuffer> buffer;
};

// A clip instance: the pixels attached to it and the image handles the plugin
// currently holds for it.
class TestClip : public Clip {
 public:
  using Clip::Clip;

  std::shared_ptr<ImageBuffer> buffer;  // connected input, or the rendered output
  std::vector<std::unique_ptr<TestImage>> liveImages;
};

// ---------------------------------------------------------------------------
// The project an effect renders in
// ---------------------------------------------------------------------------

struct Project {
  int width = 64;
  int height = 64;
  double frameRate = 24.0;
  int frames = 1;
  // Bottom-left of the project sub-window (kOfxImageEffectPropProjectOffset).
  int originX = 0;
  int originY = 0;
  // Component type and pixel depth to negotiate when the plugin supports them;
  // otherwise the first type each clip lists, and float, byte, short in that
  // order. Hosts differ here, so they are knobs.
  std::optional<Components> preferredComponents;
  std::optional<Depth> preferredDepth;
};

// ---------------------------------------------------------------------------
// The effect instance
// ---------------------------------------------------------------------------

class EffectInstance : public openfx::host::EffectInstance {
 public:
  EffectInstance(const EffectDescriptor& contextDescriptor, const Project& project);
  ~EffectInstance() override;

  void connectInput(std::string_view clipName, std::shared_ptr<ImageBuffer> image);
  void setParam(std::string_view name,
                std::string_view value);  // with the InstanceChanged actions
  void updateClipPreferences();           // kOfxImageEffectActionGetClipPreferences
  std::shared_ptr<ImageBuffer> renderFrame(double time);

 protected:
  std::unique_ptr<Clip> makeClip(const Clip& descriptorClip) override;
  openfx::host::ClipProperties clipProperties(const Clip& descriptorClip) const override;

  Image* fetchImage(Clip& clip, OfxTime time, const OfxRectD* region) override;
  void releaseImage(Image& image) override;
  bool clipRegionOfDefinition(Clip& clip, OfxTime time, OfxRectD& out) override;

 private:
  static TestClip& pixels(Clip& clip) { return static_cast<TestClip&>(clip); }
  OfxRectI projectRect() const;  // the project sub-window in pixels
  void scaleNormalisedDefault(Param& p);

  Project project_;
  Depth depth_;  // the pixel depth negotiated for every clip of this instance
  std::shared_ptr<ImageBuffer> output_;
};

// ---------------------------------------------------------------------------
// Command-line parameter values and pretty printing
// ---------------------------------------------------------------------------

// "1.5", "0.2,0.4,0.6,1" or "true,false" for the numeric kinds; a choice or
// string otherwise. False if the text does not fit the parameter.
bool parseParam(Param& p, std::string_view text);
// The value as parseParam would accept it, with strings quoted.
std::string paramValueString(const Param& p);
// Human-readable summary of a descriptor's contexts, clips and params.
std::string describeEffect(const EffectDescriptor& desc);

}  // namespace testhost
