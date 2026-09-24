// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// The pixel vocabulary shared by plugins and hosts: bit depth and component
// layout, their kOfx* property-value names, and the arithmetic that follows
// from them (channel counts, byte sizes). Covers the CPU formats every host
// supports; half float, YUV and custom component layouts are out of scope.

#include <ofxImageEffect.h>

#include <optional>
#include <string_view>

namespace openfx {

enum class PixelDepth { Byte, Short, Float };
enum class PixelComponents { RGBA, RGB, Alpha };

constexpr const char* pixelDepthName(PixelDepth d) {
  switch (d) {
    case PixelDepth::Byte:
      return kOfxBitDepthByte;
    case PixelDepth::Short:
      return kOfxBitDepthShort;
    case PixelDepth::Float:
      return kOfxBitDepthFloat;
  }
  return kOfxBitDepthNone;
}

constexpr const char* pixelComponentsName(PixelComponents c) {
  switch (c) {
    case PixelComponents::RGBA:
      return kOfxImageComponentRGBA;
    case PixelComponents::RGB:
      return kOfxImageComponentRGB;
    case PixelComponents::Alpha:
      return kOfxImageComponentAlpha;
  }
  return kOfxImageComponentNone;
}

inline std::optional<PixelDepth> pixelDepthFromName(std::string_view name) {
  for (PixelDepth d : {PixelDepth::Byte, PixelDepth::Short, PixelDepth::Float})
    if (name == pixelDepthName(d))
      return d;
  return std::nullopt;
}

inline std::optional<PixelComponents> pixelComponentsFromName(std::string_view name) {
  for (PixelComponents c :
       {PixelComponents::RGBA, PixelComponents::RGB, PixelComponents::Alpha})
    if (name == pixelComponentsName(c))
      return c;
  return std::nullopt;
}

constexpr int channelCount(PixelComponents c) {
  switch (c) {
    case PixelComponents::RGBA:
      return 4;
    case PixelComponents::RGB:
      return 3;
    case PixelComponents::Alpha:
      return 1;
  }
  return 0;
}

constexpr int bytesPerChannel(PixelDepth d) {
  switch (d) {
    case PixelDepth::Byte:
      return 1;
    case PixelDepth::Short:
      return 2;
    case PixelDepth::Float:
      return 4;
  }
  return 0;
}

constexpr int bytesPerPixel(PixelComponents c, PixelDepth d) {
  return channelCount(c) * bytesPerChannel(d);
}

}  // namespace openfx
