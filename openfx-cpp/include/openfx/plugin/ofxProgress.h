// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side RAII wrapper over OfxProgressSuiteV1 and V2. Progress reporting
// is optional, so a host with neither suite gives a Progress that does
// nothing rather than an exception.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxProgress.h>

#include <string>

#include "openfx/ofxSuites.h"

namespace openfx::plugin {

class Progress {
 public:
  // Starts the progress display. `messageId` identifies the message for
  // resource overrides and is used only by the V2 suite.
  Progress(const SuiteContainer& suites, OfxImageEffectHandle effect, const std::string& label,
           const std::string& messageId = std::string())
      : mV2(suites.get<OfxProgressSuiteV2>()),
        mV1(suites.get<OfxProgressSuiteV1>()),
        mEffect(effect) {
    if (mV2)
      mStarted = mV2->progressStart(mEffect, label.c_str(), messageId.c_str()) == kOfxStatOK;
    else if (mV1)
      mStarted = mV1->progressStart(mEffect, label.c_str()) == kOfxStatOK;
  }

  ~Progress() {
    if (!mStarted)
      return;
    if (mV2)
      mV2->progressEnd(mEffect);
    else if (mV1)
      mV1->progressEnd(mEffect);
  }

  Progress(const Progress&) = delete;
  Progress& operator=(const Progress&) = delete;

  Progress(Progress&& other) noexcept
      : mV2(other.mV2), mV1(other.mV1), mEffect(other.mEffect), mStarted(other.mStarted) {
    other.mStarted = false;
  }
  Progress& operator=(Progress&&) = delete;

  // Report how far along the task is, from 0 to 1. Returns false if the host
  // asked for the task to be abandoned.
  bool update(double fraction) {
    if (!mStarted)
      return true;
    OfxStatus status = mV2 ? mV2->progressUpdate(mEffect, fraction)
                           : mV1->progressUpdate(mEffect, fraction);
    return status != kOfxStatReplyNo;
  }

  // False if the host offered no progress suite, or refused to start.
  bool active() const { return mStarted; }

 private:
  const OfxProgressSuiteV2* mV2;
  const OfxProgressSuiteV1* mV1;
  OfxImageEffectHandle mEffect;
  bool mStarted{false};
};

}  // namespace openfx::plugin
