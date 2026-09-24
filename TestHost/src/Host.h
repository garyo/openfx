// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <openfx/host/ofxHost.h>
#include <openfx/host/ofxPlugin.h>
#include <openfx/host/ofxPluginBinary.h>
#include <openfx/ofxColourspaces.h>

namespace testhost {

using openfx::host::Plugin;
using openfx::host::PluginBinary;

// The colour management style the host advertises. It becomes a property of
// the host's own property set, which is built once, so this must be set before
// the first host() call; --colour-management is the only thing that sets it.
void setColourManagementStyle(openfx::ColourManagementStyle style);
openfx::ColourManagementStyle colourManagementStyle();

// The test host itself: its identity, its capabilities and the suites it
// provides, built once on first use.
openfx::host::Host& host();

}  // namespace testhost
