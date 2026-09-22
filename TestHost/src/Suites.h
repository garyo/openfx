// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <openfx/host/ofxDefaultSuites.h>

namespace testhost {

// The suites a plugin can fetch through OfxHost::fetchSuite, other than the
// property, image effect and parameter suites which live with their objects.
namespace suites {

// Frame range the timeline suite reports and the current time it holds.
openfx::host::Timeline& timeline();

// Returns nullptr for suites this host does not provide.
const void* fetch(const char* suiteName, int suiteVersion);

}  // namespace suites
}  // namespace testhost
