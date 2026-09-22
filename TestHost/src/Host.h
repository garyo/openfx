// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <openfx/host/ofxHost.h>
#include <openfx/host/ofxPlugin.h>
#include <openfx/host/ofxPluginBinary.h>

namespace testhost {

using openfx::host::Plugin;
using openfx::host::PluginBinary;

// The test host itself: its identity, its capabilities and the suites it
// provides, built once on first use.
openfx::host::Host& host();

}  // namespace testhost
