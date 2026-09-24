// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

/** @file minimal.cpp A minimal openfx-cpp plugin: a brightness filter on
    float RGBA images. Examples/CppGain is a fuller version, with every pixel
    depth, tiles, progress and colour management.
*/

#include <ofxImageEffect.h>

#include <cstddef>
#include <string_view>

#include "openfx/ofxMisc.h"
#include "openfx/plugin/ofxEffect.h"
#include "openfx/plugin/ofxPluginBase.h"

using namespace openfx;
using namespace openfx::plugin;

namespace {

// The row of `img` holding scanline y, as floats.
float* rowOf(const Image& img, const OfxRectI& bounds, int y) {
  return reinterpret_cast<float*>(
      static_cast<std::byte*>(img.data()) +
      static_cast<std::ptrdiff_t>(y - bounds.y1) * img.rowBytes());
}

class MinimalPlugin : public ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.example.minimal";

 protected:
  // What the plugin is and what it can do, once per binary.
  OfxStatus describe(ImageEffect& effect) override {
    effect.descriptor()
        .setLabel("Minimal")
        .setGrouping("OFX Examples")
        .setSupportedContexts({kOfxImageEffectContextFilter})
        .setSupportedPixelDepths({kOfxBitDepthFloat});
    return kOfxStatOK;
  }

  // The clips and parameters, once per context the host asks about.
  OfxStatus describeInContext(ImageEffect& effect, std::string_view) override {
    effect.defineClip(kOfxImageEffectOutputClipName)
        .setSupportedComponents({kOfxImageComponentRGBA});
    effect.defineClip(kOfxImageEffectSimpleSourceClipName)
        .setSupportedComponents({kOfxImageComponentRGBA});
    effect.params()
        .defineDouble("brightness")
        .setLabel("Brightness")
        .setDefaultValue<double>(1.0)
        .setMin<double>(0.0)
        .setMax<double>(10.0);
    return kOfxStatOK;
  }

  // One render window, at one time, into the output clip's image.
  OfxStatus render(ImageEffect& effect, ActionArgs& args) override {
    auto in = args.as<propsets::ImageEffectActionRender_InArgs>();
    const OfxTime time = in.time();
    const OfxRectI window = toOfxRectI(in.renderWindow());
    const double brightness =
        effect.params().get<DoubleParam>("brightness").getValueAtTime(time);

    Image src = effect.clip(kOfxImageEffectSimpleSourceClipName).getImage(time);
    Image dst = effect.clip(kOfxImageEffectOutputClipName).getImage(time);
    if (!dst)
      return kOfxStatFailed;
    // An empty src means the source has no image at this time, which reads as
    // transparent black, as everything outside its bounds does.
    const OfxRectI srcBounds = src ? src.bounds() : OfxRectI{0, 0, 0, 0};
    const OfxRectI dstBounds = dst.bounds();

    for (int y = window.y1; y < window.y2; ++y) {
      const float* s =
          (src && y >= srcBounds.y1 && y < srcBounds.y2) ? rowOf(src, srcBounds, y) : nullptr;
      float* d = rowOf(dst, dstBounds, y);
      for (int x = window.x1; x < window.x2; ++x) {
        const bool inSrc = s && x >= srcBounds.x1 && x < srcBounds.x2;
        for (int c = 0; c < 4; ++c)
          d[(x - dstBounds.x1) * 4 + c] =
              inSrc ? static_cast<float>(s[(x - srcBounds.x1) * 4 + c] * brightness) : 0.0f;
      }
    }
    return kOfxStatOK;
  }
};

using Entry = PluginEntry<MinimalPlugin>;

}  // namespace

int OfxGetNumberOfPlugins(void) { return Entry::numberOfPlugins(); }

OfxPlugin* OfxGetPlugin(int nth) { return Entry::get(nth); }
