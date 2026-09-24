// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrappers over OfxTimeLineSuiteV1. The effect instance the
// timeline belongs to is optional because hosts with a single timeline ignore
// it, but pass it when there could be more than one.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxTimeLine.h>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxSuites.h"

namespace openfx::plugin {

namespace detail {

inline const OfxTimeLineSuiteV1* requireTimeLineSuite(const SuiteContainer& suites) {
  const auto* suite = suites.get<OfxTimeLineSuiteV1>();
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxTimeLineSuite);
  return suite;
}

}  // namespace detail

// The frame the host's timeline is currently showing.
inline double currentTime(const SuiteContainer& suites,
                          OfxImageEffectHandle effect = nullptr) {
  double time = 0;
  OfxStatus status = detail::requireTimeLineSuite(suites)->getTime(effect, &time);
  if (status != kOfxStatOK)
    throw OfxException(status, "getTime");
  return time;
}

// Move the host's timeline to a frame.
inline void gotoTime(const SuiteContainer& suites, double time,
                     OfxImageEffectHandle effect = nullptr) {
  OfxStatus status = detail::requireTimeLineSuite(suites)->gotoTime(effect, time);
  if (status != kOfxStatOK)
    throw OfxException(status, "gotoTime");
}

// The first and last frames of the host's timeline.
inline OfxRangeD timeBounds(const SuiteContainer& suites,
                            OfxImageEffectHandle effect = nullptr) {
  OfxRangeD range{0, 0};
  OfxStatus status =
      detail::requireTimeLineSuite(suites)->getTimeBounds(effect, &range.min, &range.max);
  if (status != kOfxStatOK)
    throw OfxException(status, "getTimeBounds");
  return range;
}

}  // namespace openfx::plugin
