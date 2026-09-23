// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The plugin-side wrappers against what the C API says each call may return:
// a status the specification gives a meaning other than failure, a host that
// says kOfxStatOK but hands back no handle, and an error whose status must
// reach the host unchanged. Where the test host never gives the answer under
// test, a copy of its suite with one entry replaced gives `answer` instead.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxInteract.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>
#include <openfx/ofxPropsAccess.h>
#include <openfx/plugin/ofxClip.h>
#include <openfx/plugin/ofxEffect.h>
#include <openfx/plugin/ofxImage.h>
#include <openfx/plugin/ofxInteract.h>
#include <openfx/plugin/ofxPluginBase.h>

#include <memory>
#include <string>
#include <vector>

#include "fixture.h"
#include "harness.h"

namespace host = openfx::host;
namespace plugin = openfx::plugin;

namespace {

// What every replaced suite entry below answers; each test sets it.
OfxStatus answer = kOfxStatOK;

// What codeThrownBy() returns when nothing was thrown: no OfxStatus is negative.
constexpr OfxStatus kNothingThrown = -1;

// The code of the Exception `f` throws, or kNothingThrown. Any other exception
// escapes, and the harness reports it as the test's failure.
template <class Exception = openfx::OfxException, class F>
OfxStatus codeThrownBy(F&& f) {
  try {
    f();
  } catch (const Exception& e) {
    return e.code();
  }
  return kNothingThrown;
}

// A filter instance with its Source and Output clips and three parameters --
// an RGBA "gain", a double "scale" and an integer "count" -- as a plugin sees
// it in an instance action.
struct Filter {
  Filter() {
    plugin::ImageEffect descriptor(effect.handle(), effect.suites);
    descriptor.defineClip(kOfxImageEffectSimpleSourceClipName);
    descriptor.defineClip(kOfxImageEffectOutputClipName);
    plugin::ParamSet params = descriptor.params();
    params.defineRGBA("gain");
    params.defineDouble("scale");
    params.defineInt("count");
    instance = std::make_unique<tests::Instance>(*effect.contextDescriptor);
    instance->create();
  }

  plugin::ImageEffect wrapped() const {
    return plugin::ImageEffect(instance->handle(), effect.suites);
  }

  OfxImageClipHandle source() const {
    return instance->clip(kOfxImageEffectSimpleSourceClipName)->handle();
  }

  tests::Effect effect;
  std::unique_ptr<tests::Instance> instance;
};

}  // namespace

// ---------------------------------------------------------------------------
// clipGetImage: kOfxStatFailed means there is no image, not that it failed
// ---------------------------------------------------------------------------

TEST_CASE(a_clip_with_no_image_at_a_time_gives_an_empty_image) {
  Filter filter;
  filter.instance->unconnected = kOfxImageEffectSimpleSourceClipName;
  plugin::Clip source = filter.wrapped().clip(kOfxImageEffectSimpleSourceClipName);

  // clipGetImage answers kOfxStatFailed: "the plugin should continue
  // operation, but assume the image was black and transparent".
  const plugin::Image image = source.getImage(0);
  CHECK(!image);
  CHECK(image.empty());
  CHECK(filter.instance->fetchCount() == 1);
  CHECK(filter.instance->liveImages() == 0);

  const plugin::Image direct(host::effectSuite(), host::PropertySet::suite(),
                             filter.source(), 0);
  CHECK(!direct);
}

TEST_CASE(an_image_the_host_could_not_fetch_is_still_an_error) {
  Filter filter;
  OfxImageEffectSuiteV1 stub = *host::effectSuite();
  stub.clipGetImage = [](OfxImageClipHandle, OfxTime, const OfxRectD*,
                         OfxPropertySetHandle*) { return answer; };
  plugin::Clip source(&stub, host::PropertySet::suite(), filter.source());

  answer = kOfxStatErrMemory;
  CHECK(codeThrownBy<openfx::ImageNotFoundException>([&] { source.getImage(0); }) ==
        kOfxStatErrMemory);
  answer = kOfxStatErrBadHandle;
  CHECK(codeThrownBy<openfx::ImageNotFoundException>([&] { source.getImage(0); }) ==
        kOfxStatErrBadHandle);
  answer = kOfxStatFailed;
  CHECK(codeThrownBy([&] { source.getImage(0); }) == kNothingThrown);
}

// ---------------------------------------------------------------------------
// clipGetHandle: kOfxStatOK with no clip is no clip
// ---------------------------------------------------------------------------

namespace {

// The test host's image effect suite, but with a clipGetHandle that answers
// `answer` and hands back no clip.
OfxImageEffectSuiteV1 noClipSuite() {
  OfxImageEffectSuiteV1 suite = *host::effectSuite();
  suite.clipGetHandle = [](OfxImageEffectHandle, const char*, OfxImageClipHandle* clip,
                           OfxPropertySetHandle* props) {
    *clip = nullptr;
    if (props)
      *props = nullptr;
    return answer;
  };
  return suite;
}

// A plugin whose render only looks up its source clip.
class ClipLookup : public plugin::ImageEffectPlugin {
 protected:
  OfxStatus render(plugin::ImageEffect& effect, plugin::ActionArgs&) override {
    const plugin::Clip source = effect.clip(kOfxImageEffectSimpleSourceClipName);
    return kOfxStatOK;
  }
};

}  // namespace

TEST_CASE(a_clip_lookup_that_finds_no_clip_is_a_bad_handle) {
  Filter filter;
  const OfxImageEffectSuiteV1 stub = noClipSuite();
  openfx::SuiteContainer suites = filter.effect.suites;
  suites.add(kOfxImageEffectSuite, 1, &stub);
  auto lookUp = [&] {
    const plugin::Clip clip(&stub, host::PropertySet::suite(), filter.instance->handle(),
                            kOfxImageEffectSimpleSourceClipName);
  };
  auto lookUpInContainer = [&] {
    const plugin::Clip clip(filter.instance->handle(),
                            kOfxImageEffectSimpleSourceClipName, suites);
  };

  answer = kOfxStatOK;
  CHECK(codeThrownBy<openfx::ClipNotFoundException>(lookUp) == kOfxStatErrBadHandle);
  CHECK(codeThrownBy<openfx::ClipNotFoundException>(lookUpInContainer) ==
        kOfxStatErrBadHandle);

  // A failure the host reports keeps its own status.
  answer = kOfxStatErrUnknown;
  CHECK(codeThrownBy<openfx::ClipNotFoundException>(lookUp) == kOfxStatErrUnknown);
  CHECK(codeThrownBy<openfx::ClipNotFoundException>(lookUpInContainer) ==
        kOfxStatErrUnknown);
}

TEST_CASE(a_clip_lookup_that_finds_no_clip_fails_the_action) {
  Filter filter;
  ClipLookup lookup;
  lookup.setHost(filter.effect.host.ofx());
  CHECK(lookup.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr) ==
        kOfxStatReplyDefault);
  CHECK(lookup.dispatch(kOfxImageEffectActionRender, filter.instance->handle(), nullptr,
                        nullptr) == kOfxStatOK);

  const OfxImageEffectSuiteV1 stub = noClipSuite();
  lookup.suites.add(kOfxImageEffectSuite, 1, &stub);
  answer = kOfxStatOK;
  CHECK(lookup.dispatch(kOfxImageEffectActionRender, filter.instance->handle(), nullptr,
                        nullptr) == kOfxStatErrBadHandle);
}

// ---------------------------------------------------------------------------
// A typed parameter wraps only a parameter of its own type
// ---------------------------------------------------------------------------

TEST_CASE(a_typed_parameter_refuses_a_parameter_of_another_type) {
  Filter filter;
  const plugin::ParamSet params = filter.wrapped().params();
  CHECK(std::string(params.get<plugin::RGBAParam>("gain").type()) == kOfxParamTypeRGBA);
  CHECK(std::string(params.get<plugin::DoubleParam>("scale").type()) ==
        kOfxParamTypeDouble);

  // Read as a double, an RGBA parameter would have the host write four
  // doubles where there is room for one.
  try {
    params.get<plugin::DoubleParam>("gain");
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrValue);
    const std::string what = e.what();
    CHECK(what.find("gain") != std::string::npos);
    CHECK(what.find(kOfxParamTypeRGBA) != std::string::npos);
    CHECK(what.find(kOfxParamTypeDouble) != std::string::npos);
  }

  // The same by name or by handle, from suite pointers or a container.
  const OfxParamHandle gain = params.get<plugin::RGBAParam>("gain").handle();
  CHECK(codeThrownBy([&] {
          const plugin::DoubleParam p(params.handle(), "gain", host::paramSuite(),
                                      host::PropertySet::suite());
        }) == kOfxStatErrValue);
  CHECK(codeThrownBy([&] {
          const plugin::DoubleParam p(params.handle(), "gain", filter.effect.suites);
        }) == kOfxStatErrValue);
  CHECK(codeThrownBy([&] {
          const plugin::RGBParam p(gain, host::paramSuite(), host::PropertySet::suite());
        }) == kOfxStatErrValue);
  CHECK(codeThrownBy([&] { const plugin::DoubleParam p(gain, filter.effect.suites); }) ==
        kOfxStatErrValue);

  // Types whose property sets have the same accessor are told apart too.
  CHECK(codeThrownBy([&] { params.get<plugin::BooleanParam>("count"); }) ==
        kOfxStatErrValue);
  CHECK(codeThrownBy([&] { params.get<plugin::IntParam>("count"); }) == kNothingThrown);
}

// ---------------------------------------------------------------------------
// getPropertySet and interactGetPropertySet: no property set is an error
// ---------------------------------------------------------------------------

TEST_CASE(an_effect_whose_property_set_the_host_cannot_give_throws) {
  Filter filter;
  OfxImageEffectSuiteV1 stub = *host::effectSuite();
  stub.getPropertySet = [](OfxImageEffectHandle, OfxPropertySetHandle* props) {
    *props = nullptr;
    return answer;
  };
  openfx::SuiteContainer suites = filter.effect.suites;
  suites.add(kOfxImageEffectSuite, 1, &stub);
  const OfxImageEffectHandle effect = filter.instance->handle();
  auto fromPointers = [&] {
    const openfx::PropertyAccessor props(effect, &stub, host::PropertySet::suite());
  };
  auto fromContainer = [&] { const openfx::PropertyAccessor props(effect, suites); };

  answer = kOfxStatErrBadHandle;
  CHECK(codeThrownBy(fromPointers) == kOfxStatErrBadHandle);
  CHECK(codeThrownBy(fromContainer) == kOfxStatErrBadHandle);
  answer = kOfxStatErrUnknown;
  CHECK(codeThrownBy(fromPointers) == kOfxStatErrUnknown);
  try {
    fromContainer();
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrUnknown);
    CHECK(std::string(e.what()).find("getPropertySet") != std::string::npos);
  }

  // kOfxStatOK with no property set is no property set.
  answer = kOfxStatOK;
  CHECK(codeThrownBy(fromPointers) == kOfxStatErrBadHandle);
  CHECK(codeThrownBy(fromContainer) == kOfxStatErrBadHandle);

  // So an action on the effect fails, rather than going on without one.
  ClipLookup lookup;
  lookup.setHost(filter.effect.host.ofx());
  lookup.dispatch(kOfxActionLoad, nullptr, nullptr, nullptr);
  lookup.suites.add(kOfxImageEffectSuite, 1, &stub);
  CHECK(lookup.dispatch(kOfxImageEffectActionRender, effect, nullptr, nullptr) ==
        kOfxStatErrBadHandle);
}

TEST_CASE(an_interact_whose_property_set_the_host_cannot_give_throws) {
  Filter filter;
  host::InteractDescriptor descriptor(filter.effect.plugin,
                                      tests::stubOfxPlugin()->mainEntry, true);
  OfxInteractSuiteV1 stub = *host::interactSuite();
  stub.interactGetPropertySet = [](OfxInteractHandle, OfxPropertySetHandle* props) {
    *props = nullptr;
    return answer;
  };
  openfx::SuiteContainer suites = filter.effect.suites;
  suites.add(kOfxInteractSuite, 1, &stub);
  const OfxInteractHandle interact = descriptor.handle();
  auto fromPointers = [&] {
    const openfx::PropertyAccessor props(interact, &stub, host::PropertySet::suite());
  };
  auto fromContainer = [&] { const openfx::PropertyAccessor props(interact, suites); };
  auto wrapped = [&] { const plugin::Interact wrapper(interact, suites); };

  answer = kOfxStatErrBadHandle;
  CHECK(codeThrownBy(fromPointers) == kOfxStatErrBadHandle);
  CHECK(codeThrownBy(fromContainer) == kOfxStatErrBadHandle);
  CHECK(codeThrownBy(wrapped) == kOfxStatErrBadHandle);
  try {
    fromPointers();
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(std::string(e.what()).find("interactGetPropertySet") != std::string::npos);
  }

  answer = kOfxStatOK;
  CHECK(codeThrownBy(fromPointers) == kOfxStatErrBadHandle);
  CHECK(codeThrownBy(fromContainer) == kOfxStatErrBadHandle);
  CHECK(codeThrownBy(wrapped) == kOfxStatErrBadHandle);
}

// ---------------------------------------------------------------------------
// kOfxInteractPropSlaveToParam: one value per parameter
// ---------------------------------------------------------------------------

TEST_CASE(slaving_an_interact_to_parameters_adds_each_one) {
  Filter filter;
  host::InteractDescriptor descriptor(filter.effect.plugin,
                                      tests::stubOfxPlugin()->mainEntry, true);
  host::InteractInstance overlay(descriptor, *filter.instance);
  openfx::SuiteContainer suites = filter.effect.suites;
  suites.add(kOfxInteractSuite, 1, host::interactSuite());
  plugin::Interact interact(overlay.handle(), suites);

  // "The interact can be slaved to multiple parameters (setting index 0, then
  // index 1 etc...)".
  interact.slaveToParam("gain");
  interact.slaveToParam("scale");
  CHECK(overlay.slaveToParams() == std::vector<std::string>{"gain", "scale"});

  interact.slaveToParams({"count", "centre"});
  CHECK(overlay.slaveToParams() ==
        std::vector<std::string>{"gain", "scale", "count", "centre"});
}
