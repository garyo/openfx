// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrappers over OfxMessageSuiteV1 and V2. The text is passed
// already formatted (use openfx::format) and handed to the suite through a
// plain "%s", so a percent sign in it cannot be taken as a conversion.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMessage.h>

#include <string>

#include "openfx/ofxSuites.h"

namespace openfx::plugin {

// Post a message. Returns the suite's status: kOfxStatReplyYes or
// kOfxStatReplyNo answer a kOfxMessageQuestion, and
// kOfxStatErrMissingHostFeature means the host has no message suite.
inline OfxStatus message(const SuiteContainer& suites, OfxImageEffectHandle effect,
                         const char* type, const char* id, const std::string& text) {
  if (const auto* v2 = suites.get<OfxMessageSuiteV2>())
    return v2->message(effect, type, id, "%s", text.c_str());
  if (const auto* v1 = suites.get<OfxMessageSuiteV1>())
    return v1->message(effect, type, id, "%s", text.c_str());
  return kOfxStatErrMissingHostFeature;
}

// Post a message that stays on the effect until cleared. V2 only.
inline OfxStatus setPersistentMessage(const SuiteContainer& suites,
                                      OfxImageEffectHandle effect, const char* type,
                                      const char* id, const std::string& text) {
  if (const auto* v2 = suites.get<OfxMessageSuiteV2>())
    return v2->setPersistentMessage(effect, type, id, "%s", text.c_str());
  return kOfxStatErrMissingHostFeature;
}

// Clear the effect's persistent message and any error state with it. V2 only.
inline OfxStatus clearPersistentMessage(const SuiteContainer& suites,
                                        OfxImageEffectHandle effect) {
  if (const auto* v2 = suites.get<OfxMessageSuiteV2>())
    return v2->clearPersistentMessage(effect);
  return kOfxStatErrMissingHostFeature;
}

}  // namespace openfx::plugin
