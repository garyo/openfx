// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

/** @file cppgain.cpp A gain/offset filter written entirely on the openfx-cpp
    plugin bindings: no suite is called directly, only through the wrappers in
    openfx/plugin/.
*/

#include <ofxColour.h>
#include <ofxImageEffect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "openfx/ofxLog.h"
#include "openfx/ofxMisc.h"
#include "openfx/ofxPixels.h"
#include "openfx/plugin/ofxDraw.h"
#include "openfx/plugin/ofxEffect.h"
#include "openfx/plugin/ofxInteract.h"
#include "openfx/plugin/ofxMessage.h"
#include "openfx/plugin/ofxMultiThread.h"
#include "openfx/plugin/ofxPluginBase.h"
#include "openfx/plugin/ofxProgress.h"

using namespace openfx;
using namespace openfx::plugin;

namespace {

// The parameter the overlay below shows and drags.
constexpr const char* kCentreParam = "centre";

// Gain and offset for one pixel, in the order the components are stored.
struct Adjustment {
  std::array<double, 4> gain{1, 1, 1, 1};
  double offset{0};
};

// One band of rows. PIX is the component type and `scale` the value a 1.0
// channel holds in it, so float images pass through unscaled and unclamped.
// The offset lifts the colour channels only; alpha just gets its gain. An
// empty src -- the source has no image at this time -- reads as transparent
// black, as everything outside its bounds does.
template <typename PIX>
void gainRows(const Image& src, const Image& dst, const OfxRectI& window, int nComps,
              const Adjustment& adj, double scale, int y0, int y1, int yStep) {
  const OfxRectI srcBounds = src ? src.bounds() : OfxRectI{0, 0, 0, 0};
  const OfxRectI dstBounds = dst.bounds();
  constexpr bool isFloat = std::is_floating_point_v<PIX>;

  for (int y = y0; y < y1; y += yStep) {
    auto* dstRow = reinterpret_cast<PIX*>(static_cast<std::byte*>(dst.data()) +
                                          static_cast<std::ptrdiff_t>(y - dstBounds.y1) *
                                              dst.rowBytes());
    const PIX* srcRow = nullptr;
    if (src && y >= srcBounds.y1 && y < srcBounds.y2)
      srcRow = reinterpret_cast<const PIX*>(
          static_cast<const std::byte*>(src.data()) +
          static_cast<std::ptrdiff_t>(y - srcBounds.y1) * src.rowBytes());

    for (int x = window.x1; x < window.x2; ++x) {
      const bool inSrc = srcRow && x >= srcBounds.x1 && x < srcBounds.x2;
      PIX* out = dstRow + static_cast<std::ptrdiff_t>(x - dstBounds.x1) * nComps;
      for (int c = 0; c < nComps; ++c) {
        const auto offs = static_cast<std::ptrdiff_t>(x - srcBounds.x1) * nComps + c;
        double v = inSrc ? srcRow[offs] / scale : 0.0;
        v = v * adj.gain[c] + (c < 3 ? adj.offset : 0.0);
        out[c] = isFloat ? static_cast<PIX>(v)
                         : static_cast<PIX>(std::lround(std::clamp(v, 0.0, 1.0) * scale));
      }
    }
  }
}

// An overlay for the "centre" parameter: a crosshair in a box, drawn through
// the OFX 1.5 Draw suite, which the pen drags around.
//
// It shows the whole of openfx/plugin/ofxInteract.h: the actions as virtuals,
// the interact's own per-instance state, the slave-to-param link that has the
// host redraw when the parameter changes, and the effect behind the interact.
class CentreOverlay : public InteractPlugin<CentreOverlay> {
 public:
  // The crosshair's arms and the box around it, in screen pixels, and how
  // close the pen must come to grab it.
  static constexpr double kArm = 12;
  static constexpr double kBox = 5;
  static constexpr double kGrab = 8;

 protected:
  // What the overlay remembers between actions: whether the pen has it.
  struct State {
    bool grabbed = false;
  };

  OfxStatus createInstance(Interact& interact) override {
    interact.setInstanceData(new State);
    interact.slaveToParam(kCentreParam);  // a change to it means a redraw
    return kOfxStatOK;
  }

  OfxStatus destroyInstance(Interact& interact) override {
    delete state(interact);
    interact.setInstanceData(nullptr);
    return kOfxStatOK;
  }

  OfxStatus draw(Interact& interact, ActionArgs& in) override {
    const auto scale = in.as<propsets::InteractActionDraw_InArgs>()
                           .pixelScale();  // canonical per screen pixel
    const OfxPointD centre = centreOf(interact);
    Draw draw(in, suites());

    draw.setColour(draw.getColour(state(interact)->grabbed
                                      ? kOfxStandardColourOverlaySelected
                                      : kOfxStandardColourOverlayDeselected));
    const std::array<OfxPointD, 4> arms{{{centre.x - kArm * scale[0], centre.y},
                                         {centre.x + kArm * scale[0], centre.y},
                                         {centre.x, centre.y - kArm * scale[1]},
                                         {centre.x, centre.y + kArm * scale[1]}}};
    draw.drawLines(arms);
    draw.drawRectangle({centre.x - kBox * scale[0], centre.y - kBox * scale[1]},
                       {centre.x + kBox * scale[0], centre.y + kBox * scale[1]});
    return kOfxStatOK;
  }

  OfxStatus penDown(Interact& interact, ActionArgs& in) override {
    const auto args = in.as<propsets::InteractActionPenDown_InArgs>();
    const auto pen = args.penPosition();
    const auto scale = args.pixelScale();
    const OfxPointD centre = centreOf(interact);
    if (std::abs(pen[0] - centre.x) > kGrab * scale[0] ||
        std::abs(pen[1] - centre.y) > kGrab * scale[1])
      return kOfxStatReplyDefault;  // the pen is elsewhere; the host may have it
    state(interact)->grabbed = true;
    interact.redraw();
    return kOfxStatOK;
  }

  OfxStatus penMotion(Interact& interact, ActionArgs& in) override {
    if (!state(interact)->grabbed)
      return kOfxStatReplyDefault;
    const auto pen = in.as<propsets::InteractActionPenMotion_InArgs>().penPosition();
    centreParam(interact).setValue({pen[0], pen[1]});
    return kOfxStatOK;
  }

  OfxStatus penUp(Interact& interact, ActionArgs&) override {
    if (!state(interact)->grabbed)
      return kOfxStatReplyDefault;
    state(interact)->grabbed = false;
    interact.redraw();
    return kOfxStatOK;
  }

 private:
  static State* state(Interact& interact) {
    return static_cast<State*>(interact.instanceData());
  }
  static Double2DParam centreParam(Interact& interact) {
    return interact.effect().params().get<Double2DParam>(kCentreParam);
  }
  static OfxPointD centreOf(Interact& interact) {
    return centreParam(interact).getValue();
  }
};

class GainPlugin : public ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.example.cppgain";

 protected:
  OfxStatus describe(ImageEffect& effect) override {
    propsets::EffectDescriptor desc = effect.descriptor();
    desc.setLabel("C++ Gain")
        .setGrouping("OFX Examples")
        .setSupportedContexts(
            {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral})
        .setSupportedPixelDepths({kOfxBitDepthFloat, kOfxBitDepthShort, kOfxBitDepthByte})
        .setRenderThreadSafety(kOfxImageEffectRenderFullySafe)
        .setSupportsTiles(true)
        .setSupportsMultiResolution(true)
        // The overlay draws through the Draw suite, so it is a V2 interact.
        // entryPoint() is also where it is handed this plugin's suites.
        .setOverlayInteractV2(CentreOverlay::entryPoint(suites));
    // Properties an older host may not have.
    desc.soft()
        .setPluginDescription("Multiplies the source by an RGBA gain and adds an offset.")
        .setColourManagementStyle(kOfxImageEffectColourManagementBasic);
    return kOfxStatOK;
  }

  OfxStatus describeInContext(ImageEffect& effect,
                              std::string_view /*context*/) override {
    effect.defineClip(kOfxImageEffectOutputClipName)
        .setSupportedComponents({kOfxImageComponentRGBA, kOfxImageComponentRGB});
    effect.defineClip(kOfxImageEffectSimpleSourceClipName)
        .setSupportedComponents({kOfxImageComponentRGBA, kOfxImageComponentRGB});

    ParamSet params = effect.params();
    params.defineRGBA("gain")
        .setLabel("Gain")
        .setHint("Multiplies each channel of the source")
        .setDefaultValue<double>({1.0, 1.0, 1.0, 1.0})
        .setMin<double>({0.0, 0.0, 0.0, 0.0})
        .setMax<double>({10.0, 10.0, 10.0, 10.0})
        .setDisplayMax<double>({4.0, 4.0, 4.0, 4.0});
    params.defineDouble("offset")
        .setLabel("Offset")
        .setHint("Added to the colour channels after the gain")
        .setDefaultValue<double>(0.0)
        .setMin<double>(-1.0)
        .setMax<double>(1.0);
    // A spatial parameter the overlay shows: its default is in normalised
    // coordinates, so the host puts it at the centre of whatever the project
    // turns out to be.
    params.defineDouble2D(kCentreParam)
        .setLabel("Centre")
        .setHint("Where the overlay's crosshair sits; drag it with the pen")
        .setDoubleType(kOfxParamDoubleTypeXYAbsolute)
        .setDefaultValue<double>({0.5, 0.5})
        .setDefaultCoordinateSystem(kOfxParamCoordinatesNormalised);
    params.definePage("Main").setPageChild({"gain", "offset", kCentreParam});
    return kOfxStatOK;
  }

  // Gain is colourspace-agnostic, so the output is whatever the host would
  // most like; failing that, the source clip's own colourspace.
  OfxStatus getOutputColourspace(ImageEffect&, ActionArgs& in, ActionArgs& out) override {
    const std::vector<CStringView> preferred =
        in.props().soft().getAll<PropId::OfxImageClipPropPreferredColourspaces>();
    out.as<propsets::ImageEffectActionGetOutputColourspace_OutArgs>().setColourspace(
        !preferred.empty() && !preferred[0].empty()
            ? preferred[0].c_str()
            : clipColourspaceRef(kOfxImageEffectSimpleSourceClipName).c_str());
    return kOfxStatOK;
  }

  OfxStatus render(ImageEffect& effect, ActionArgs& args) override {
    auto in = args.as<propsets::ImageEffectActionRender_InArgs>();
    const OfxTime time = in.time();
    const OfxRectI window = toOfxRectI(in.renderWindow());

    ParamSet params = effect.params();
    const OfxRGBAColourD gain = params.get<RGBAParam>("gain").getValueAtTime(time);
    Adjustment adj;
    adj.gain = {gain.r, gain.g, gain.b, gain.a};
    adj.offset = params.get<DoubleParam>("offset").getValueAtTime(time);

    Clip srcClip = effect.clip(kOfxImageEffectSimpleSourceClipName);
    Clip dstClip = effect.clip(kOfxImageEffectOutputClipName);
    Image src = srcClip.getImage(time);
    Image dst = dstClip.getImage(time);
    if (!dst)
      return kOfxStatFailed;

    const auto depth = pixelDepthFromName(dst.pixelDepth());
    const auto components = pixelComponentsFromName(dst.components());
    if (!depth || !components || *components == PixelComponents::Alpha) {
      message(suites, effect.handle(), kOfxMessageError, "cppgain.format",
              format("cannot render {} {} images", dst.pixelDepth(), dst.components()));
      return kOfxStatErrImageFormat;
    }
    const int nComps = channelCount(*components);

    // Work through the render window in bands so progress and the host's abort
    // flag are checked on this thread, between multiThread calls.
    Progress progress(suites, effect.handle(), "C++ Gain", "cppgain.render");
    constexpr int kBands = 4;
    const int height = window.y2 - window.y1;
    for (int band = 0; band < kBands && height > 0; ++band) {
      const int y0 = window.y1 + band * height / kBands;
      const int y1 = window.y1 + (band + 1) * height / kBands;
      multiThread(suites, 0, [&](unsigned index, unsigned count) {
        const int first = y0 + static_cast<int>(index);
        const int step = static_cast<int>(count);
        switch (*depth) {
          case PixelDepth::Byte:
            gainRows<std::uint8_t>(src, dst, window, nComps, adj, 255.0, first, y1, step);
            break;
          case PixelDepth::Short:
            gainRows<std::uint16_t>(src, dst, window, nComps, adj, 65535.0, first, y1,
                                    step);
            break;
          case PixelDepth::Float:
            gainRows<float>(src, dst, window, nComps, adj, 1.0, first, y1, step);
            break;
        }
      });
      if (!progress.update(static_cast<double>(band + 1) / kBands) || effect.abort())
        return kOfxStatFailed;
    }
    return kOfxStatOK;
  }
};

using Entry = PluginEntry<GainPlugin>;

}  // namespace

int OfxGetNumberOfPlugins(void) { return Entry::numberOfPlugins(); }

OfxPlugin* OfxGetPlugin(int nth) { return Entry::get(nth); }
