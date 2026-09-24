// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxColour.h>
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/ofxColourspaces.h>
#include <openfx/ofxPixels.h>

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace testhost {

// openfx-cpp's effect model, which this host derives its own from.
using openfx::host::Clip;
using openfx::host::EffectDescriptor;
using openfx::host::Image;
using openfx::host::Param;
using openfx::host::ParamSet;
using openfx::host::ParamValue;
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

  // First byte of the pixel at absolute (x, y): where a view rooted there starts.
  std::byte* pixelData(int x, int y);
  const std::byte* pixelData(int x, int y) const;

  std::shared_ptr<ImageBuffer> converted(Components components, Depth depth) const;
  // The same pixels with the bounds moved to start at origin, and a row padding.
  std::shared_ptr<ImageBuffer> reframed(OfxPointI origin, int rowPadding) const;
  // The same image at a proxy render scale, by nearest neighbour: the bounds
  // move from canonical to pixel coordinates, taking the pixel aspect ratio in x.
  std::shared_ptr<ImageBuffer> resampled(OfxPointD scale, double par) const;
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
  std::shared_ptr<ImageBuffer> scaled;  // buffer at the render scale, when below 1
  std::vector<std::unique_ptr<TestImage>> liveImages;

  // The pixels the plugin sees for the render in flight.
  const std::shared_ptr<ImageBuffer>& renderBuffer() const {
    return scaled ? scaled : buffer;
  }
};

// ---------------------------------------------------------------------------
// The project an effect renders in
// ---------------------------------------------------------------------------

struct Project {
  int width = 64;
  int height = 64;
  double frameRate = 24.0;
  double firstFrame = 0;
  int frames = 1;
  // Whether this run renders a sequence in frame order, which is what the
  // instance tells a plugin through kOfxImageEffectInstancePropSequentialRender.
  bool sequential = false;
  // Bottom-left of the project sub-window (kOfxImageEffectPropProjectOffset).
  int originX = 0;
  int originY = 0;
  // Component type and pixel depth to negotiate when the plugin supports them;
  // otherwise the first type each clip lists, and float, byte, short in that
  // order. Hosts differ here, so they are knobs.
  std::optional<Components> preferredComponents;
  std::optional<Depth> preferredDepth;
  // Colour management (OFX 1.5): the style the host advertises, and the
  // colourspace it supplies its input images in. An empty colourspace means
  // the host's default for the style.
  openfx::ColourManagementStyle colourManagement = openfx::ColourManagementStyle::None;
  std::string colourspace;
};

// ---------------------------------------------------------------------------
// How a frame is rendered
// ---------------------------------------------------------------------------

// In one Render action or several, and at what proxy scale. Both are subject to
// the plugin declaring support: tiles need kOfxImageEffectPropSupportsTiles and
// a scale below 1 needs kOfxImageEffectPropSupportsMultiResolution.
struct RenderOptions {
  int tiles = 1;                      // tiles x tiles over the render window
  int tileWidth = 0, tileHeight = 0;  // a fixed tile size instead, if non-zero
  bool checkTiles = false;            // also render whole, and compare
  OfxPointD renderScale{1.0, 1.0};

  bool tiled() const { return tiles > 1 || tileWidth > 0 || tileHeight > 0; }
  bool scaled() const { return renderScale.x != 1.0 || renderScale.y != 1.0; }
};

// ---------------------------------------------------------------------------
// The effect instance
// ---------------------------------------------------------------------------

class EffectInstance : public openfx::host::EffectInstance {
 public:
  EffectInstance(const EffectDescriptor& contextDescriptor, const Project& project);
  ~EffectInstance() override;

  void connectInput(std::string_view clipName, std::shared_ptr<ImageBuffer> image);
  // Sets a parameter's value, or a keyframe at time, with the InstanceChanged
  // actions around the change.
  void setParam(std::string_view name, std::string_view value,
                std::optional<OfxTime> time = std::nullopt);
  // GetClipPreferences, then GetOutputColourspace: the specification has the
  // output colourspace recomputed whenever anything colour-related changes, and
  // the plugin's colourspace preferences arrive with the clip preferences.
  void updateClipPreferences();
  void setRenderOptions(const RenderOptions& options) { options_ = options; }
  std::shared_ptr<ImageBuffer> renderFrame(double time);

  // A sequence of frames: one BeginSequenceRender for the whole range, then a
  // renderFrame() per frame, then one EndSequenceRender. The host renders the
  // frames in order, so the renders carry the sequential render status.
  void beginSequence(OfxRangeD range, double frameStep);
  void endSequence();

 protected:
  std::unique_ptr<Clip> makeClip(const Clip& descriptorClip) override;
  openfx::host::ClipProperties clipProperties(const Clip& descriptorClip) const override;

  Image* fetchImage(Clip& clip, OfxTime time, const OfxRectD* region) override;
  void releaseImage(Image& image) override;
  bool clipRegionOfDefinition(Clip& clip, OfxTime time, OfxRectD& out) override;

 private:
  // The range a beginSequence() opened, while it is open.
  struct Sequence {
    OfxRangeD range;
    double step;
  };

  static TestClip& pixels(Clip& clip) { return static_cast<TestClip&>(clip); }
  OfxRectI projectRect() const;  // the project sub-window, in canonical coordinates
  void scaleNormalisedDefault(Param& p);
  double pixelAspectRatio(const Clip& clip) const;

  // The scale to render at: the requested one if the plugin supports multiple
  // resolutions, else 1, with an info line saying so.
  OfxPointD effectiveRenderScale();
  // Resample every connected input to the render scale, for the render in flight.
  void scaleInputs();
  // The render window split into the tiles to render it in, in pixel coordinates;
  // the whole window unless tiling is on and the plugin supports tiles.
  std::vector<OfxRectI> tilesOf(const OfxRectI& window);
  // One Render action per tile, each seeing only its own part of the output.
  OfxStatus renderTiles(openfx::host::RenderArgs& args,
                        const std::vector<OfxRectI>& tiles);
  // Render the frame whole as well, and warn about the first pixel that differs:
  // an assembled tiled render must equal an untiled one. Leaves the tiled frame
  // as the result, so it stands on its own.
  void compareWithWholeFrame(openfx::host::RenderArgs args, const OfxRectI& window,
                             int rowPadding);
  // The output of an effect that claimed identity, instead of a render.
  void copyIdentity(const openfx::host::Identity& identity, OfxTime time,
                    const OfxRectI& window);

  // Colour management. The style and the input colourspaces are settled before
  // the instance is created, because a plugin reads them from its first action
  // onwards; the output colourspace is negotiated after the clip preferences.
  void setUpColourManagement();
  void negotiateOutputColourspace();
  std::vector<std::string> preferredColourspaces() const;

  // The plugin declared what it needs of each input before the render; a fetch
  // outside that would come back empty from a host that supplies only what was
  // asked for, so this host says so instead.
  void checkDeclaredNeeds(const Clip& clip, OfxTime time) const;
  // The host declares no support for multiple clip depths, so a plugin that
  // answers its clip preferences with clips at different depths is warned.
  void checkClipDepths(const openfx::host::ClipPreferences& answer) const;
  openfx::host::RenderArgs sequenceArgs(double time) const;

  Project project_;
  Depth depth_;  // the pixel depth negotiated for every clip of this instance
  RenderOptions options_;
  OfxPointD renderScale_{1.0, 1.0};  // in force for the render in flight
  OfxRectI tileWindow_{};            // the tile the output image is a view of
  std::optional<Sequence> sequence_;
  std::shared_ptr<ImageBuffer> output_;
  openfx::ColourManagementStyle colourStyle_ = openfx::ColourManagementStyle::None;
  std::string inputColourspace_;
  // Per input clip, as of the frame being rendered.
  std::map<std::string, OfxRectD> regionsOfInterest_;
  std::map<std::string, std::vector<OfxRangeD>> framesNeeded_;
};

// ---------------------------------------------------------------------------
// Command-line parameter values and pretty printing
// ---------------------------------------------------------------------------

// "1.5", "0.2,0.4,0.6,1" or "true,false" for the numeric kinds; a choice or
// string otherwise. False if the text does not fit the parameter.
bool parseParam(const Param& p, std::string_view text, ParamValue& value);
// The parameter's value at a time, as parseParam would accept it, with strings
// quoted.
std::string paramValueString(const Param& p, OfxTime time);
// Human-readable summary of a descriptor's contexts, clips and params.
std::string describeEffect(const EffectDescriptor& desc);

}  // namespace testhost
