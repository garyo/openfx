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
  Progress(const SuiteContainer& suites, OfxImageEffectHandle effect,
           const std::string& label, const std::string& messageId = std::string())
      : v2_(suites.get<OfxProgressSuiteV2>()), v1_(suites.get<OfxProgressSuiteV1>()),
        effect_(effect) {
    if (v2_)
      started_ =
          v2_->progressStart(effect_, label.c_str(), messageId.c_str()) == kOfxStatOK;
    else if (v1_)
      started_ = v1_->progressStart(effect_, label.c_str()) == kOfxStatOK;
  }

  ~Progress() {
    if (!started_)
      return;
    if (v2_)
      v2_->progressEnd(effect_);
    else if (v1_)
      v1_->progressEnd(effect_);
  }

  Progress(const Progress&) = delete;
  Progress& operator=(const Progress&) = delete;

  Progress(Progress&& other) noexcept
      : v2_(other.v2_), v1_(other.v1_), effect_(other.effect_), started_(other.started_) {
    other.started_ = false;
  }
  Progress& operator=(Progress&&) = delete;

  // Report how far along the task is, from 0 to 1. Returns false if the host
  // asked for the task to be abandoned.
  bool update(double fraction) {
    if (!started_)
      return true;
    OfxStatus status = v2_ ? v2_->progressUpdate(effect_, fraction)
                           : v1_->progressUpdate(effect_, fraction);
    return status != kOfxStatReplyNo;
  }

  // False if the host offered no progress suite, or refused to start.
  bool active() const { return started_; }

 private:
  const OfxProgressSuiteV2* v2_;
  const OfxProgressSuiteV1* v1_;
  OfxImageEffectHandle effect_;
  bool started_{false};
};

}  // namespace openfx::plugin
