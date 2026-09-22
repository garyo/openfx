// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Host.h"

#include <ofxColour.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <openfx/host/ofxDefaultSuites.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxPropertySet.h>

namespace testhost {

namespace {

openfx::ColourManagementStyle gColourManagementStyle =
    openfx::ColourManagementStyle::None;

// The test host's identity, capabilities and suites. Everything else a host
// needs of the OfxHost struct is in openfx::host::Host.
struct TestHost : openfx::host::Host {
  TestHost() {
    accessor()
        .setType(kOfxTypeImageEffectHost)
        .setName("org.openeffects.testhost")
        .setLabel("OpenFX Test Host")
        .setVersion({1, 0, 0})
        .setVersionLabel("1.0")
        .setApiVersion({1, 5})  // the headers carry no numeric API version
        .setIsBackground(1)
        .setSupportsOverlays(0)
        .setSupportsMultiResolution(1)
        .setSupportsTiles(1)
        .setTemporalClipAccess(1)
        .setSupportedComponents(
            {kOfxImageComponentRGBA, kOfxImageComponentRGB, kOfxImageComponentAlpha})
        .setSupportedContexts({kOfxImageEffectContextFilter,
                               kOfxImageEffectContextGeneral,
                               kOfxImageEffectContextGenerator})
        .setMultipleClipDepths(0)
        .setSupportsMultipleClipPARs(0)
        .setSetableFrameRate(0)
        .setSetableFielding(0)
        .setSequentialRender(1)  // --frames renders strictly first to last
        .setSupportsStringAnimation(0)
        .setSupportsCustomInteract(0)
        .setSupportsChoiceAnimation(0)
        .setSupportsStrChoice(1)
        .setSupportsStrChoiceAnimation(0)
        .setSupportsBooleanAnimation(0)
        .setSupportsCustomAnimation(0)
        .setSupportsParametricAnimation(0)
        .setMaxParameters(-1)
        .setMaxPages(0)
        .setPageRowColumnCount({0, 0})
        .setHostOSHandle(nullptr)
        .setNativeOrigin(kOfxHostNativeOriginBottomLeft)
        .setRenderQualityDraft(0)
        .setSupportedPixelDepths({kOfxBitDepthByte, kOfxBitDepthShort, kOfxBitDepthFloat})
        .setOpenGLRenderSupported("false")
        .setOpenCLSupported("false")
        .setOpenCLRenderSupported("false")
        .setCudaRenderSupported("false")
        .setCudaStreamSupported("false")
        .setMetalRenderSupported("false")
        .setCpuRenderSupported("true")
        .setColourManagementStyle(
            openfx::colourManagementStyleName(gColourManagementStyle));
    // The native config whose colourspaces the host and plugin name; the host
    // supports the one the headers define, and no OCIO config at all.
    if (gColourManagementStyle != openfx::ColourManagementStyle::None)
      accessor().setColourManagementAvailableConfigs({kOfxConfigIdentifier});

    suites().add(kOfxPropertySuite, 1, openfx::host::PropertySet::suite());
    suites().add(kOfxImageEffectSuite, 1, openfx::host::effectSuite());
    suites().add(kOfxParameterSuite, 1, openfx::host::paramSuite());
    openfx::host::addDefaultSuites(suites());
  }
};

}  // namespace

void setColourManagementStyle(openfx::ColourManagementStyle style) {
  gColourManagementStyle = style;
}

openfx::ColourManagementStyle colourManagementStyle() { return gColourManagementStyle; }

openfx::host::Host& host() {
  static TestHost instance;
  return instance;
}

}  // namespace testhost
