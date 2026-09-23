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
#include <utility>

#include "openfx/ofxSuites.h"

namespace openfx::plugin {

// A progress display, ended with progressEnd when the Progress goes. It can
// take over a display C code started, and hand one back with release(). The
// display belongs to an effect, not to a handle of its own, so taking one over
// is a named function rather than a constructor.
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

  // Take over the display C code started on `effect` with progressStart: the
  // Progress updates and ends it from now on, through the suite given.
  [[nodiscard]] static Progress adoptStarted(OfxImageEffectHandle effect,
                                             const OfxProgressSuiteV2* suite) noexcept {
    return Progress(suite, nullptr, effect);
  }
  [[nodiscard]] static Progress adoptStarted(OfxImageEffectHandle effect,
                                             const OfxProgressSuiteV1* suite) noexcept {
    return Progress(nullptr, suite, effect);
  }
  [[nodiscard]] static Progress adoptStarted(OfxImageEffectHandle effect,
                                             const SuiteContainer& suites) {
    return Progress(suites.get<OfxProgressSuiteV2>(), suites.get<OfxProgressSuiteV1>(),
                    effect);
  }

  ~Progress() { reset(); }

  Progress(const Progress&) = delete;
  Progress& operator=(const Progress&) = delete;

  Progress(Progress&& other) noexcept
      : v2_(other.v2_), v1_(other.v1_), effect_(other.effect_),
        started_(std::exchange(other.started_, false)), lastStatus_(other.lastStatus_) {}

  Progress& operator=(Progress&& other) noexcept {
    if (this != &other) {
      reset();
      v2_ = other.v2_;
      v1_ = other.v1_;
      effect_ = other.effect_;
      started_ = std::exchange(other.started_, false);
      lastStatus_ = other.lastStatus_;
    }
    return *this;
  }

  // Report how far along the task is, from 0 to 1, and say whether to go on.
  // The host answers kOfxStatOK for "the task should continue" and
  // kOfxStatReplyNo for "the task should abort", which is how it passes on the
  // user's cancel; anything else is an error, such as kOfxStatErrBadHandle for
  // a display it does not know. Only kOfxStatOK and kOfxStatReplyYes mean go
  // on, and lastStatus() tells a cancel from an error. With no display there
  // is no one to ask, and the task goes on.
  bool update(double fraction) {
    if (!started_)
      return true;
    lastStatus_ = v2_ ? v2_->progressUpdate(effect_, fraction)
                      : v1_->progressUpdate(effect_, fraction);
    return lastStatus_ == kOfxStatOK || lastStatus_ == kOfxStatReplyYes;
  }

  // The host's answer to the last update(): kOfxStatReplyNo if the user
  // cancelled, an error status if the display failed, and kOfxStatOK before
  // the first update or while there is no display.
  OfxStatus lastStatus() const { return lastStatus_; }

  // False if the host offered no progress suite, or refused to start.
  bool active() const { return started_; }

  // End the display now, leaving this Progress inactive.
  void reset() noexcept {
    if (!std::exchange(started_, false))
      return;
    if (v2_)
      v2_->progressEnd(effect_);
    else
      v1_->progressEnd(effect_);
  }

  // Hand the display to C code, which must end it with progressEnd, leaving
  // this Progress inactive. Returns the effect it belongs to, or null if there
  // is no display to end.
  [[nodiscard]] OfxImageEffectHandle release() noexcept {
    return std::exchange(started_, false) ? effect_ : nullptr;
  }

 private:
  Progress(const OfxProgressSuiteV2* v2, const OfxProgressSuiteV1* v1,
           OfxImageEffectHandle effect) noexcept
      : v2_(v2), v1_(v1), effect_(effect), started_(v2 != nullptr || v1 != nullptr) {}

  const OfxProgressSuiteV2* v2_;
  const OfxProgressSuiteV1* v1_;
  OfxImageEffectHandle effect_;
  bool started_{false};
  OfxStatus lastStatus_{kOfxStatOK};
};

}  // namespace openfx::plugin
