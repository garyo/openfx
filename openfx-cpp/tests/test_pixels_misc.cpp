// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The vocabulary shared by hosts and plugins: pixel formats, the rect and
// point converters, the per-clip property names, status strings, the colour
// management styles and colourspaces, and the logger.

#include <ofxColour.h>
#include <ofxCore.h>
#include <ofxGPURender.h>
#include <ofxImageEffect.h>
#include <openfx/host/ofxPlugin.h>
#include <openfx/ofxColourspaces.h>
#include <openfx/ofxLog.h>
#include <openfx/ofxMisc.h>
#include <openfx/ofxPixels.h>
#include <openfx/ofxStatusStrings.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "harness.h"

using openfx::PixelComponents;
using openfx::PixelDepth;

TEST_CASE(pixels_name_each_depth_and_round_trip) {
  CHECK(std::string_view(openfx::pixelDepthName(PixelDepth::Byte)) == kOfxBitDepthByte);
  CHECK(std::string_view(openfx::pixelDepthName(PixelDepth::Short)) == kOfxBitDepthShort);
  CHECK(std::string_view(openfx::pixelDepthName(PixelDepth::Float)) == kOfxBitDepthFloat);
  for (PixelDepth depth : {PixelDepth::Byte, PixelDepth::Short, PixelDepth::Float})
    CHECK(openfx::pixelDepthFromName(openfx::pixelDepthName(depth)) == depth);
  CHECK(!openfx::pixelDepthFromName(kOfxBitDepthHalf).has_value());
  CHECK(!openfx::pixelDepthFromName("").has_value());
}

TEST_CASE(pixels_name_each_component_layout_and_round_trip) {
  CHECK(std::string_view(openfx::pixelComponentsName(PixelComponents::RGBA)) ==
        kOfxImageComponentRGBA);
  CHECK(std::string_view(openfx::pixelComponentsName(PixelComponents::RGB)) ==
        kOfxImageComponentRGB);
  CHECK(std::string_view(openfx::pixelComponentsName(PixelComponents::Alpha)) ==
        kOfxImageComponentAlpha);
  for (PixelComponents components :
       {PixelComponents::RGBA, PixelComponents::RGB, PixelComponents::Alpha})
    CHECK(openfx::pixelComponentsFromName(openfx::pixelComponentsName(components)) ==
          components);
  CHECK(!openfx::pixelComponentsFromName(kOfxImageComponentNone).has_value());
}

TEST_CASE(pixels_size_follows_from_the_depth_and_the_layout) {
  CHECK(openfx::channelCount(PixelComponents::RGBA) == 4);
  CHECK(openfx::channelCount(PixelComponents::RGB) == 3);
  CHECK(openfx::channelCount(PixelComponents::Alpha) == 1);
  CHECK(openfx::bytesPerChannel(PixelDepth::Byte) == 1);
  CHECK(openfx::bytesPerChannel(PixelDepth::Short) == 2);
  CHECK(openfx::bytesPerChannel(PixelDepth::Float) == 4);
  CHECK(openfx::bytesPerPixel(PixelComponents::RGBA, PixelDepth::Float) == 16);
  CHECK(openfx::bytesPerPixel(PixelComponents::RGB, PixelDepth::Byte) == 3);
  CHECK(openfx::bytesPerPixel(PixelComponents::Alpha, PixelDepth::Short) == 2);
  static_assert(openfx::bytesPerPixel(PixelComponents::RGBA, PixelDepth::Byte) == 4);
}

TEST_CASE(misc_converts_a_container_to_a_rect_or_a_point) {
  const std::array<int, 4> ints{1, 2, 3, 4};
  const OfxRectI recti = openfx::toOfxRectI(ints);
  CHECK(recti.x1 == 1);
  CHECK(recti.y1 == 2);
  CHECK(recti.x2 == 3);
  CHECK(recti.y2 == 4);

  const std::vector<double> doubles{1.5, 2.5, 3.5, 4.5};
  const OfxRectD rectd = openfx::toOfxRectD(doubles);
  CHECK(rectd.x1 == 1.5);
  CHECK(rectd.y2 == 4.5);

  const std::array<int, 2> pair{7, 8};
  const OfxPointI pointi = openfx::toOfxPointI(pair);
  CHECK(pointi.x == 7);
  CHECK(pointi.y == 8);

  const std::array<double, 2> scale{0.5, 0.25};
  const OfxPointD pointd = openfx::toOfxPointD(scale);
  CHECK(pointd.x == 0.5);
  CHECK(pointd.y == 0.25);
}

TEST_CASE(misc_rejects_a_container_with_too_few_elements) {
  const std::vector<int> two{1, 2};
  CHECK_THROWS_AS(openfx::toOfxRectI(two), openfx::OfxException);
  const std::vector<double> one{1.0};
  CHECK_THROWS_AS(openfx::toOfxPointD(one), openfx::OfxException);
  try {
    openfx::toOfxRectD(one);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrValue);
  }
}

TEST_CASE(misc_names_the_per_clip_preference_properties) {
  CHECK(openfx::clipPrefComponentsProp("Source") == "OfxImageClipPropComponents_Source");
  CHECK(openfx::clipPrefDepthProp("Source") == "OfxImageClipPropDepth_Source");
  CHECK(openfx::clipPrefPARProp("Source") == "OfxImageClipPropPAR_Source");
  CHECK(openfx::clipPrefColourspacesProp("Source") ==
        "OfxImageClipPropPreferredColourspaces_Source");
  CHECK(openfx::clipPrefDepthProp("") == "OfxImageClipPropDepth_");
}

TEST_CASE(misc_names_the_per_clip_region_and_frame_range_properties) {
  CHECK(openfx::clipRoIProp("Mask") == "OfxImageClipPropRoI_Mask");
  CHECK(openfx::clipFrameRangeProp("Mask") == "OfxImageClipPropFrameRange_Mask");
}

TEST_CASE(misc_cross_references_another_clips_colourspace) {
  CHECK(openfx::clipColourspaceRef("Source") == "OfxColourspace_Source");
  const auto target = openfx::clipColourspaceRefTarget("OfxColourspace_Source");
  CHECK(target.has_value());
  CHECK(*target == "Source");
  CHECK(!openfx::clipColourspaceRefTarget(kOfxColourspaceACEScct).has_value());
  CHECK(!openfx::clipColourspaceRefTarget("").has_value());
  CHECK(openfx::clipColourspaceRefTarget(openfx::clipColourspaceRef("Mask")) == "Mask");
}

TEST_CASE(status_strings_name_the_status_codes) {
  CHECK(std::string_view(ofxStatusToString(kOfxStatOK)) == "kOfxStatOK");
  CHECK(std::string_view(ofxStatusToString(kOfxStatFailed)) == "kOfxStatFailed");
  CHECK(std::string_view(ofxStatusToString(kOfxStatErrBadIndex)) ==
        "kOfxStatErrBadIndex");
  CHECK(std::string_view(ofxStatusToString(kOfxStatReplyDefault)) ==
        "kOfxStatReplyDefault");
  CHECK(std::string_view(ofxStatusToString(kOfxStatErrImageFormat)) ==
        "kOfxStatErrImageFormat");
  CHECK(std::string_view(ofxStatusToString(kOfxStatGPUOutOfMemory)) ==
        "kOfxStatGPUOutOfMemory");
  CHECK(std::string_view(ofxStatusToString(-12345)) == "Unknown OFX Status");
}

TEST_CASE(an_action_succeeds_when_it_is_handled_or_declined) {
  CHECK(openfx::host::actionSucceeded(kOfxStatOK));
  CHECK(openfx::host::actionSucceeded(kOfxStatReplyDefault));
  CHECK(!openfx::host::actionSucceeded(kOfxStatFailed));
  CHECK(!openfx::host::actionSucceeded(kOfxStatReplyNo));
  CHECK(!openfx::host::actionSucceeded(kOfxStatErrMemory));
}

TEST_CASE(colour_management_styles_are_named_and_ordered) {
  using Style = openfx::ColourManagementStyle;
  CHECK(std::string_view(openfx::colourManagementStyleName(Style::None)) ==
        kOfxImageEffectColourManagementNone);
  CHECK(std::string_view(openfx::colourManagementStyleName(Style::Basic)) ==
        kOfxImageEffectColourManagementBasic);
  CHECK(std::string_view(openfx::colourManagementStyleName(Style::OCIO)) ==
        kOfxImageEffectColourManagementOCIO);
  // Increasing capability, which is what comparing two styles means.
  CHECK(Style::None < Style::Basic);
  CHECK(Style::Basic < Style::Core);
  CHECK(Style::Core < Style::Full);
  CHECK(Style::Full < Style::OCIO);
  for (Style style : {Style::None, Style::Basic, Style::Core, Style::Full, Style::OCIO})
    CHECK(openfx::colourManagementStyleFromName(
              openfx::colourManagementStyleName(style)) == style);
  CHECK(!openfx::colourManagementStyleFromName("OfxImageEffectColourManagementNever")
             .has_value());
}

TEST_CASE(colourspaces_are_found_by_name_with_the_style_they_belong_to) {
  using Style = openfx::ColourManagementStyle;
  const openfx::ColourspaceInfo* sceneLog =
      openfx::findColourspace(kOfxColourspaceOfxSceneLog);
  CHECK(sceneLog != nullptr);
  CHECK(sceneLog->isBasic);
  CHECK(sceneLog->style() == Style::Basic);
  CHECK(std::string_view(sceneLog->encoding) == "log");

  const openfx::ColourspaceInfo* acescct =
      openfx::findColourspace(kOfxColourspaceACEScct);
  CHECK(acescct != nullptr);
  CHECK(!acescct->isBasic);
  CHECK(acescct->isCore);
  CHECK(acescct->style() == Style::Core);

  // A role has no encoding of its own.
  const openfx::ColourspaceInfo* data = openfx::findColourspace(kOfxColourspaceRoleData);
  CHECK(data != nullptr);
  CHECK(std::string_view(data->encoding).empty());

  CHECK(openfx::findColourspace("no_such_colourspace") == nullptr);
}

TEST_CASE(a_colourspace_is_allowed_in_the_styles_that_reach_it) {
  using Style = openfx::ColourManagementStyle;
  CHECK(openfx::colourspaceAllowedIn(kOfxColourspaceOfxSceneLog, Style::Basic));
  CHECK(openfx::colourspaceAllowedIn(kOfxColourspaceOfxSceneLog, Style::Core));
  CHECK(!openfx::colourspaceAllowedIn(kOfxColourspaceACEScct, Style::Basic));
  CHECK(openfx::colourspaceAllowedIn(kOfxColourspaceACEScct, Style::Core));
  CHECK(openfx::colourspaceAllowedIn(kOfxColourspaceACEScct, Style::Full));
  // A name the config does not have belongs to the OCIO style alone.
  CHECK(!openfx::colourspaceAllowedIn("studio_colourspace", Style::Full));
  CHECK(openfx::colourspaceAllowedIn("studio_colourspace", Style::OCIO));
}

TEST_CASE(a_basic_colourspace_stands_for_one_with_the_same_encoding) {
  const char* basic = openfx::basicColourspaceFor(kOfxColourspaceACEScct);
  CHECK(basic != nullptr);
  CHECK(std::string_view(basic) == kOfxColourspaceOfxSceneLog);
  CHECK(std::string_view(openfx::findColourspace(basic)->encoding) ==
        std::string_view(openfx::findColourspace(kOfxColourspaceACEScct)->encoding));
  // A basic colourspace stands for itself.
  CHECK(std::string_view(openfx::basicColourspaceFor(kOfxColourspaceOfxSceneLog)) ==
        kOfxColourspaceOfxSceneLog);
  // A role has no encoding, and an unknown name is not in the config at all.
  CHECK(openfx::basicColourspaceFor(kOfxColourspaceRoleData) == nullptr);
  CHECK(openfx::basicColourspaceFor("no_such_colourspace") == nullptr);
}

TEST_CASE(every_colourspace_of_the_config_is_found_by_its_own_name) {
  bool allFound = true;
  bool anyRole = false;
  for (const openfx::ColourspaceInfo& info : openfx::kColourspaces) {
    allFound = allFound && openfx::findColourspace(info.name) != nullptr;
    anyRole = anyRole || std::string_view(info.encoding).empty();
  }
  CHECK(allFound);
  CHECK(anyRole);
  CHECK(std::size(openfx::kColourspaces) > 50);
}

// A log handler runs after the log mutex is released, so it may call back into
// the Logger. Were it called under the lock, the re-entrant getContext() here
// would deadlock on the non-recursive mutex: this test would not fail, it
// would hang, and the whole test binary with it.
TEST_CASE(log_handler_can_call_back_into_the_logger) {
  const openfx::Logger::Level level = openfx::Logger::getLevel();
  std::string message;
  std::string contextSeen;
  openfx::Logger::Level levelSeen = openfx::Logger::Level::Debug;
  openfx::Logger::setContext("reentrant");
  openfx::Logger::setLogHandler([&](openfx::Logger::Level,
                                    std::chrono::system_clock::time_point,
                                    const std::string& text) {
    contextSeen = openfx::Logger::getContext();
    levelSeen = openfx::Logger::getLevel();
    message = text;
  });

  openfx::Logger::error("handler sees this");

  CHECK(message == "[reentrant] handler sees this");
  CHECK(contextSeen == "reentrant");
  CHECK(levelSeen == level);

  openfx::Logger::setContext("");
  // Back to the run's silent handler, or to the default one when the log is on.
  openfx::Logger::setLogHandler(
      std::getenv("OPENFX_TEST_LOG")
          ? openfx::Logger::LogHandler()
          : openfx::Logger::LogHandler([](openfx::Logger::Level,
                                          std::chrono::system_clock::time_point,
                                          const std::string&) {}));
}
