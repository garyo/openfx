// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>
#include "ofxExceptions.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace openfx {

// The GetClipPreferences out-args carry three properties per clip whose names
// are the clip's name appended to a fixed prefix (see
// kOfxImageEffectActionGetClipPreferences).
inline std::string clipPrefComponentsProp(std::string_view clipName) {
  return "OfxImageClipPropComponents_" + std::string(clipName);
}
inline std::string clipPrefDepthProp(std::string_view clipName) {
  return "OfxImageClipPropDepth_" + std::string(clipName);
}
inline std::string clipPrefPARProp(std::string_view clipName) {
  return "OfxImageClipPropPAR_" + std::string(clipName);
}
// A fourth, from OFX 1.5: the colourspaces the plugin would prefer this clip in.
inline std::string clipPrefColourspacesProp(std::string_view clipName) {
  return "OfxImageClipPropPreferredColourspaces_" + std::string(clipName);
}

// The GetRegionsOfInterest and GetFramesNeeded out-args are named the same way:
// one property per clip, the region the plugin needs of it and the frame ranges
// it needs from it (see those actions).
inline std::string clipRoIProp(std::string_view clipName) {
  return "OfxImageClipPropRoI_" + std::string(clipName);
}
inline std::string clipFrameRangeProp(std::string_view clipName) {
  return "OfxImageClipPropFrameRange_" + std::string(clipName);
}

// A colourspace may cross-reference another clip's as "OfxColourspace_<clip>"
// (see kOfxImageClipPropColourspace): the reference, and the clip it names.
inline std::string clipColourspaceRef(std::string_view clipName) {
  return "OfxColourspace_" + std::string(clipName);
}
inline std::optional<std::string_view> clipColourspaceRefTarget(std::string_view value) {
  constexpr std::string_view kPrefix = "OfxColourspace_";
  if (value.substr(0, kPrefix.size()) != kPrefix)
    return std::nullopt;
  return value.substr(kPrefix.size());
}

namespace detail {
// Element count traits
template <typename T>
inline constexpr size_t ofx_elem_count = 0;
template <>
inline constexpr size_t ofx_elem_count<OfxRectI> = 4;
template <>
inline constexpr size_t ofx_elem_count<OfxRectD> = 4;
template <>
inline constexpr size_t ofx_elem_count<OfxPointI> = 2;
template <>
inline constexpr size_t ofx_elem_count<OfxPointD> = 2;

// Generic conversion implementation
template <typename Result, typename Container>
Result toOfx(const Container& container) {
  if (container.size() < ofx_elem_count<Result>)
    throw OfxException(kOfxStatErrValue, "Container has too few elements");

  Result result;
  if constexpr (ofx_elem_count<Result> == 4) {
    result.x1 = container[0];
    result.y1 = container[1];
    result.x2 = container[2];
    result.y2 = container[3];
  } else {
    result.x = container[0];
    result.y = container[1];
  }
  return result;
}
}  // namespace detail

// Integer type functions - use std::remove_reference_t to handle references properly
template <typename Container>
auto toOfxRectI(const Container& c)
    -> std::enable_if_t<std::is_integral_v<std::remove_reference_t<decltype(c[0])>>,
                        OfxRectI> {
  return detail::toOfx<OfxRectI>(c);
}

template <typename Container>
auto toOfxPointI(const Container& c)
    -> std::enable_if_t<std::is_integral_v<std::remove_reference_t<decltype(c[0])>>,
                        OfxPointI> {
  return detail::toOfx<OfxPointI>(c);
}

// Floating-point type functions
template <typename Container>
auto toOfxRectD(const Container& c)
    -> std::enable_if_t<std::is_floating_point_v<std::remove_reference_t<decltype(c[0])>>,
                        OfxRectD> {
  return detail::toOfx<OfxRectD>(c);
}

template <typename Container>
auto toOfxPointD(const Container& c)
    -> std::enable_if_t<std::is_floating_point_v<std::remove_reference_t<decltype(c[0])>>,
                        OfxPointD> {
  return detail::toOfx<OfxPointD>(c);
}
}  // namespace openfx
