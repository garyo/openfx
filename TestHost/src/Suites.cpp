// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Suites.h"

#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <openfx/ofxLog.h>
#include <openfx/ofxSuites.h>

#include "Effect.h"

namespace testhost::suites {

namespace {

// The host's own suites plus the framework's default suites, built once on
// first use.
const openfx::SuiteContainer& container() {
  static openfx::SuiteContainer instance = [] {
    openfx::SuiteContainer c;
    c.add(kOfxPropertySuite, 1, PropertySet::suite());
    c.add(kOfxImageEffectSuite, 1, effectSuite());
    c.add(kOfxParameterSuite, 1, paramSuite());
    openfx::host::addDefaultSuites(c);
    return c;
  }();
  return instance;
}

}  // namespace

openfx::host::Timeline& timeline() { return openfx::host::timeline(); }

const void* fetch(const char* name, int version) {
  const void* suite = container().find(name ? name : "", version);
  if (!suite) openfx::Logger::debug("suite not provided: {} v{}", name ? name : "", version);
  return suite;
}

}  // namespace testhost::suites
