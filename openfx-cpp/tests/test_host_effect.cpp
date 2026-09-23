// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The plugin-side wrappers driven against the host-side model: clips, images,
// parameters and the suites a host provides, with no plugin binary involved.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMessage.h>
#include <ofxParam.h>
#include <openfx/host/ofxDefaultSuites.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/plugin/ofxClip.h>
#include <openfx/plugin/ofxEffect.h>
#include <openfx/plugin/ofxImage.h>
#include <openfx/plugin/ofxMemory.h>
#include <openfx/plugin/ofxMessage.h>
#include <openfx/plugin/ofxMultiThread.h>
#include <openfx/plugin/ofxParam.h>
#include <openfx/plugin/ofxProgress.h>
#include <openfx/plugin/ofxTimeLine.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fixture.h"
#include "harness.h"

namespace plugin = openfx::plugin;
namespace host = openfx::host;

namespace {

// The source and output clips of a filter, defined the way a plugin defines
// them in DescribeInContext.
void defineFilterClips(plugin::ImageEffect& effect) {
  effect.defineClip(kOfxImageEffectSimpleSourceClipName)
      .setSupportedComponents({kOfxImageComponentRGBA});
  effect.defineClip(kOfxImageEffectOutputClipName)
      .setSupportedComponents({kOfxImageComponentRGBA});
}

// A plugin that answers GetClipPreferences: the output clip is opaque, at a
// pixel aspect ratio of 2, 48fps, lower-field-first and continuously sampled.
OfxStatus clipPreferencesMainEntry(const char* action, const void*, OfxPropertySetHandle,
                                   OfxPropertySetHandle outArgs) {
  if (std::string_view(action) != kOfxImageEffectActionGetClipPreferences)
    return kOfxStatReplyDefault;
  host::PropertySet* out = host::PropertySet::from(outArgs);
  out->set(kOfxImageEffectPropPreMultiplication, 0, kOfxImageOpaque);
  out->set(kOfxImageEffectPropFrameRate, 0, 48.0);
  out->set(kOfxImageClipPropFieldOrder, 0, kOfxImageFieldLower);
  out->set(kOfxImageClipPropContinuousSamples, 0, 1);
  out->set(openfx::clipPrefPARProp(kOfxImageEffectOutputClipName), 0, 2.0);
  return kOfxStatOK;
}

}  // namespace

// ---------------------------------------------------------------------------
// Describing
// ---------------------------------------------------------------------------

TEST_CASE(host_describes_a_plugin_into_a_descriptor_per_context) {
  tests::Effect effect(kOfxImageEffectContextGeneral);
  CHECK(effect.plugin.id() == "org.openeffects.tests.stub");
  CHECK(effect.plugin.versionMajor() == 1);
  CHECK(effect.plugin.versionMinor() == 0);
  CHECK(effect.plugin.isImageEffect());
  CHECK(effect.plugin.isLoaded());
  CHECK(effect.global->context().empty());
  CHECK(effect.contextDescriptor->context() == kOfxImageEffectContextGeneral);
  CHECK(!effect.contextDescriptor->isInstance());
  // The context descriptor reads the global one's properties through its parent.
  CHECK(effect.global->props().getString(kOfxPluginPropFilePath) ==
        "/stub/Stub.ofx.bundle");
  CHECK(effect.contextDescriptor->props().getString(kOfxPluginPropFilePath) ==
        "/stub/Stub.ofx.bundle");
  CHECK(effect.contextDescriptor->props().getString(kOfxPropType) == kOfxTypeImageEffect);
}

TEST_CASE(a_plugin_defines_its_clips_on_the_descriptor) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  CHECK(effect.contextDescriptor->clips().size() == 2);
  host::Clip* source =
      effect.contextDescriptor->clip(kOfxImageEffectSimpleSourceClipName);
  CHECK(source != nullptr);
  CHECK(!source->isOutput());
  CHECK(source->props().getString(kOfxPropType) == kOfxTypeClip);
  CHECK(source->props().getString(kOfxPropName) == kOfxImageEffectSimpleSourceClipName);
  CHECK(source->props().getString(kOfxImageEffectPropSupportedComponents) ==
        kOfxImageComponentRGBA);
  CHECK(effect.contextDescriptor->clip(kOfxImageEffectOutputClipName)->isOutput());
  // Defining a clip twice hands back the one already there.
  wrapper.defineClip(kOfxImageEffectSimpleSourceClipName);
  CHECK(effect.contextDescriptor->clips().size() == 2);
  CHECK(effect.contextDescriptor->clip("NoSuchClip") == nullptr);
}

TEST_CASE(a_plugin_writes_its_own_descriptor_properties) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  wrapper.descriptor()
      .setLabel("Stub")
      .setPluginDescription("A plugin that has nothing to say")
      .setSupportedContexts({kOfxImageEffectContextFilter})
      .setSupportedPixelDepths({kOfxBitDepthFloat})
      .setSupportsTiles(true);
  CHECK(effect.contextDescriptor->props().getString(kOfxPropLabel) == "Stub");
  CHECK(effect.contextDescriptor->supportedContexts().size() == 1);
  CHECK(effect.contextDescriptor->supportedDepths().size() == 1);
  CHECK(effect.contextDescriptor->supportedDepths()[0] == openfx::PixelDepth::Float);
  CHECK(effect.contextDescriptor->props().getInt(kOfxImageEffectPropSupportsTiles) == 1);
  CHECK(wrapper.props().handle() == effect.contextDescriptor->props().handle());
  CHECK(wrapper.handle() == effect.handle());
  CHECK(&wrapper.suites() == &effect.suites);
}

TEST_CASE(a_plugin_defines_one_parameter_of_each_type) {
  tests::Effect effect;
  plugin::ParamSet params = plugin::ImageEffect(effect.handle(), effect.suites).params();
  params.defineDouble("scale");
  params.defineDouble2D("centre");
  params.defineDouble3D("rotation");
  params.defineInt("count");
  params.defineInt2D("origin");
  params.defineInt3D("cell");
  params.defineBoolean("invert");
  params.defineChoice("mode");
  params.defineStrChoice("preset");
  params.defineRGB("tint");
  params.defineRGBA("colour");
  params.defineString("caption");
  params.defineCustom("shape");
  params.definePushButton("reset");
  params.defineGroup("controls");
  params.definePage("main");

  const host::ParamSet& hostParams = effect.contextDescriptor->params();
  CHECK(hostParams.params().size() == 16);
  const auto* scale = effect.contextDescriptor->params().params()[0].get();
  CHECK(scale->name() == "scale");
  CHECK(scale->type() == kOfxParamTypeDouble);
  CHECK(scale->kind() == host::Param::Kind::Double);
  CHECK(scale->arity() == 1);
  CHECK(scale->interpolation() == host::Param::Interpolation::Linear);
  CHECK(scale->animates());

  host::Param* colour = effect.contextDescriptor->params().find("colour");
  CHECK(colour != nullptr);
  CHECK(colour->arity() == 4);
  CHECK(colour->props().getDouble(kOfxParamPropDisplayMax, 0) == 1.0);

  host::Param* invert = effect.contextDescriptor->params().find("invert");
  CHECK(invert->kind() == host::Param::Kind::Int);
  CHECK(invert->interpolation() == host::Param::Interpolation::Step);
  CHECK(!invert->animates());  // this host does not animate booleans

  CHECK(effect.contextDescriptor->params().find("controls")->kind() ==
        host::Param::Kind::None);
  CHECK(effect.contextDescriptor->params().find("nothing") == nullptr);
}

TEST_CASE(defining_the_same_parameter_twice_is_an_error) {
  tests::Effect effect;
  plugin::ParamSet params = plugin::ImageEffect(effect.handle(), effect.suites).params();
  params.defineDouble("scale");
  try {
    params.defineDouble("scale");
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrExists);
  }
  // A type the host has never heard of is unsupported, not fatal.
  OfxPropertySetHandle propSet = nullptr;
  CHECK(effect.suites.get<OfxParameterSuiteV1>()->paramDefine(
            plugin::ImageEffect(effect.handle(), effect.suites).params().handle(),
            "OfxParamTypeImaginary", "ghost", &propSet) == kOfxStatErrUnsupported);
}

TEST_CASE(a_plugin_writes_a_parameters_descriptor_properties) {
  tests::Effect effect;
  plugin::ParamSet params = plugin::ImageEffect(effect.handle(), effect.suites).params();
  params.defineDouble("scale")
      .setLabel("Scale")
      .setHint("How much")
      .setDefaultValue(2.0)
      .setMin(0.0)
      .setMax(10.0)
      .setDigits(3);
  host::Param* scale = effect.contextDescriptor->params().find("scale");
  CHECK(scale->props().getString(kOfxPropLabel) == "Scale");
  CHECK(scale->props().getString(kOfxParamPropHint) == "How much");
  CHECK(scale->props().getDouble(kOfxParamPropDefault) == 2.0);
  CHECK(scale->props().getDouble(kOfxParamPropMin) == 0.0);
  CHECK(scale->props().getInt(kOfxParamPropDigits) == 3);
}

// ---------------------------------------------------------------------------
// Instances
// ---------------------------------------------------------------------------

TEST_CASE(an_instance_copies_the_descriptors_clips_and_parameters) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  wrapper.params().defineDouble("scale").setDefaultValue(1.5);

  host::InstanceProject project;
  project.size = {32, 16};
  project.frameRate = 30.0;
  tests::Instance instance(*effect.contextDescriptor, project);
  instance.create();
  CHECK(instance.isInstance());
  CHECK(&instance.descriptor() == effect.contextDescriptor.get());
  CHECK(instance.project().frameRate == 30.0);
  CHECK(instance.clips().size() == 2);
  CHECK(instance.params().params().size() == 1);
  // The instance parameter starts at the descriptor's default.
  CHECK(instance.params().params()[0]->value().doubles[0] == 1.5);

  host::Clip* source = instance.clip(kOfxImageEffectSimpleSourceClipName);
  CHECK(source->owner == &instance);
  CHECK(source->components() == openfx::PixelComponents::RGBA);
  CHECK(source->depth() == openfx::PixelDepth::Float);
  CHECK(source->props().getString(kOfxPropType) == kOfxTypeClip);
  CHECK(source->props().getInt(kOfxImageClipPropConnected) == 0);
  CHECK(instance.clip(kOfxImageEffectOutputClipName)
            ->props()
            .getInt(kOfxImageClipPropConnected) == 1);
  CHECK(instance.props().getString(kOfxPropType) == kOfxTypeImageEffectInstance);
  CHECK(instance.props().getString(kOfxImageEffectPropContext) ==
        kOfxImageEffectContextFilter);
  CHECK(instance.props().getDouble(kOfxImageEffectPropProjectSize, 0) == 32.0);
}

TEST_CASE(an_instance_answers_the_actions_the_plugin_declines) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();

  // The plugin says nothing, so every answer is the specification's default.
  CHECK(!instance.getClipPreferences());
  CHECK(!instance.frameVarying());
  CHECK(instance.sequentialRenderRequest() == 0);
  const OfxRectD rod = instance.regionOfDefinition(0);
  CHECK(rod.x1 == 0.0);
  CHECK(rod.x2 == 64.0);  // the project window, there being no connected input
  CHECK(!instance.isIdentity(0, {0, 0, 8, 4}, {1.0, 1.0}, kOfxImageFieldNone));
  CHECK(!instance.getTimeDomain().has_value());
  CHECK(!instance.getOutputColourspace({"ofx_scene_linear"}).has_value());

  const auto regions = instance.getRegionsOfInterest(0, {0, 0, 8, 4}, {1.0, 1.0});
  CHECK(regions.size() == 1);  // one per input clip
  CHECK(regions.at(kOfxImageEffectSimpleSourceClipName).x2 == 8.0);

  const auto frames = instance.getFramesNeeded(3);
  CHECK(frames.size() == 1);
  CHECK(frames.at(kOfxImageEffectSimpleSourceClipName).size() == 1);
  CHECK(frames.at(kOfxImageEffectSimpleSourceClipName)[0].min == 3.0);
}

TEST_CASE(an_instance_applies_the_clip_preferences_the_plugin_answers) {
  tests::Host host;
  openfx::SuiteContainer suites = tests::fetchSuites(host);
  OfxPlugin ofxPlugin = {kOfxImageEffectPluginApi,
                         1,
                         "org.openeffects.tests.clipprefs",
                         1,
                         0,
                         [](OfxHost*) {},
                         clipPreferencesMainEntry};
  host::Plugin plugin(&ofxPlugin, "/stub/ClipPrefs.ofx.bundle");
  plugin.load(host);
  std::unique_ptr<host::EffectDescriptor> global = plugin.describe();
  std::unique_ptr<host::EffectDescriptor> descriptor =
      plugin.describeInContext(*global, kOfxImageEffectContextFilter);
  plugin::ImageEffect wrapper(descriptor->handle(), suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*descriptor);
  instance.create();

  CHECK(instance.getClipPreferences());
  host::Clip* output = instance.clip(kOfxImageEffectOutputClipName);
  CHECK(output != nullptr);
  // Every answer the plugin is allowed to give lands on the output clip.
  CHECK(output->props().getString(kOfxImageEffectPropPreMultiplication) ==
        kOfxImageOpaque);
  CHECK(output->props().getDouble(kOfxImagePropPixelAspectRatio) == 2.0);
  CHECK(output->props().getDouble(kOfxImageEffectPropFrameRate) == 48.0);
  CHECK(output->props().getString(kOfxImageClipPropFieldOrder) == kOfxImageFieldLower);
  CHECK(output->props().getInt(kOfxImageClipPropContinuousSamples) == 1);
}

TEST_CASE(an_instance_runs_the_render_and_editing_actions) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  wrapper.params().defineDouble("scale");
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();

  host::RenderArgs args;
  args.renderWindow = {0, 0, tests::Instance::kWidth, tests::Instance::kHeight};
  args.frameRange = {0, 2};
  CHECK(instance.beginSequenceRender(args) == kOfxStatReplyDefault);
  CHECK(instance.render(args) == kOfxStatReplyDefault);
  CHECK(instance.endSequenceRender(args) == kOfxStatReplyDefault);
  // The argument-less actions simply have to reach the plugin.
  instance.purgeCaches();
  instance.syncPrivateData();
  instance.beginInstanceEdit();
  instance.endInstanceEdit();
  instance.paramChanged(*instance.params().params()[0], kOfxChangeUserEdited, 0,
                        {1.0, 1.0});
  CHECK(!instance.abort());
}

// ---------------------------------------------------------------------------
// Clips and images
// ---------------------------------------------------------------------------

TEST_CASE(the_clip_wrapper_reads_the_clip_instances_properties) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();

  plugin::ImageEffect live(instance.handle(), effect.suites);
  plugin::Clip source = live.clip(kOfxImageEffectSimpleSourceClipName);
  CHECK(!source.empty());
  CHECK(static_cast<bool>(source));
  CHECK(std::string(source.name()) == kOfxImageEffectSimpleSourceClipName);
  CHECK(!source.connected());
  CHECK(std::string(source.pixelDepth()) == kOfxBitDepthFloat);
  CHECK(std::string(source.components()) == kOfxImageComponentRGBA);
  CHECK(std::string(source.preMultiplication()) == kOfxImagePreMultiplied);
  CHECK(source.frameRate() == 25.0);
  CHECK(source.pixelAspectRatio() == 1.0);
  CHECK(std::string(source.fieldOrder()) == kOfxImageFieldNone);
  CHECK(!source.isMask());
  CHECK(!source.optional());
  CHECK(source.handle() != nullptr);
  CHECK(source.propertySetHandle() ==
        instance.clip(kOfxImageEffectSimpleSourceClipName)->props().handle());
  CHECK(std::string(source.accessor().name()) == kOfxImageEffectSimpleSourceClipName);

  CHECK(live.clip(kOfxImageEffectOutputClipName).connected());
  CHECK_THROWS_AS(live.clip("NoSuchClip"), openfx::ClipNotFoundException);
  CHECK(plugin::Clip().empty());
}

TEST_CASE(the_clip_wrapper_moves_its_properties) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();
  plugin::ImageEffect live(instance.handle(), effect.suites);

  plugin::Clip first = live.clip(kOfxImageEffectSimpleSourceClipName);
  plugin::Clip second = std::move(first);
  // A moved-from Clip has handed over the properties it reads through, so it
  // must say it is empty rather than look usable.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK(first.empty());
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK(!static_cast<bool>(first));
  CHECK(!second.empty());
  CHECK(std::string(second.name()) == kOfxImageEffectSimpleSourceClipName);

  plugin::Clip third;
  CHECK(third.empty());
  third = std::move(second);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK(second.empty());
  CHECK(!third.empty());
  CHECK(std::string(third.name()) == kOfxImageEffectSimpleSourceClipName);
  CHECK(!third.getImage(0).empty());  // and it still fetches images
}

TEST_CASE(the_image_wrapper_fetches_and_releases_an_image) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();
  instance.buffer(kOfxImageEffectSimpleSourceClipName)[5] = 0.5f;

  plugin::ImageEffect live(instance.handle(), effect.suites);
  plugin::Clip source = live.clip(kOfxImageEffectSimpleSourceClipName);
  {
    plugin::Image image = source.getImage(2.0);
    CHECK(!image.empty());
    CHECK(instance.liveImages() == 1);
    CHECK(instance.lastFetchTime == 2.0);
    CHECK(!instance.lastFetchHadRegion);
    CHECK(image.rowBytes() == tests::Instance::rowBytes());
    CHECK(image.data() == instance.buffer(kOfxImageEffectSimpleSourceClipName).data());
    CHECK(static_cast<float*>(image.data())[5] == 0.5f);
    CHECK(image.bounds().x2 == tests::Instance::kWidth);
    CHECK(image.regionOfDefinition().y2 == tests::Instance::kHeight);
    CHECK(image.renderScale().x == 1.0);
    CHECK(image.pixelAspectRatio() == 1.0);
    CHECK(std::string(image.pixelDepth()) == kOfxBitDepthFloat);
    CHECK(std::string(image.components()) == kOfxImageComponentRGBA);
    CHECK(std::string(image.field()) == kOfxImageFieldNone);
    CHECK(std::string(image.preMultiplication()) == kOfxImagePreMultiplied);
    CHECK(std::string(image.uniqueIdentifier()).find("Source") == 0);
    CHECK(image.handle() != nullptr);
    CHECK(std::string(image.accessor().pixelDepth()) == kOfxBitDepthFloat);
  }
  CHECK(instance.liveImages() == 0);  // released when the wrapper went out of scope
  CHECK(instance.fetchCount() == 1);
  CHECK(instance.releaseCount() == 1);
}

TEST_CASE(the_image_wrapper_moves_its_image) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();
  plugin::Clip source = plugin::ImageEffect(instance.handle(), effect.suites)
                            .clip(kOfxImageEffectSimpleSourceClipName);
  {
    plugin::Image first = source.getImage(0);
    plugin::Image second = std::move(first);
    // Reading the moved-from wrapper is the test: it must have given up the
    // image rather than kept a second handle on it.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK(first.empty());
    CHECK(!second.empty());
    CHECK(instance.liveImages() == 1);

    plugin::Image third;
    CHECK(third.empty());
    third = std::move(second);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK(second.empty());
    CHECK(!third.empty());
    CHECK(third.rowBytes() == tests::Instance::rowBytes());
    CHECK(instance.liveImages() == 1);
  }
  CHECK(instance.liveImages() == 0);
  CHECK(instance.releaseCount() == 1);  // the moved-from wrapper released nothing
}

TEST_CASE(fetching_an_image_for_a_region_passes_the_region_on) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();
  plugin::Clip source = plugin::ImageEffect(instance.handle(), effect.suites)
                            .clip(kOfxImageEffectSimpleSourceClipName);
  const OfxRectD region{0, 0, 4, 2};
  plugin::Image image = source.getImage(1.0, region);
  CHECK(instance.lastFetchHadRegion);
  CHECK(!image.empty());
}

TEST_CASE(fetching_from_an_unconnected_clip_throws) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();
  instance.unconnected = kOfxImageEffectSimpleSourceClipName;
  plugin::Clip source = plugin::ImageEffect(instance.handle(), effect.suites)
                            .clip(kOfxImageEffectSimpleSourceClipName);
  CHECK_THROWS_AS(source.getImage(0), openfx::ImageNotFoundException);
  CHECK(instance.liveImages() == 0);
}

TEST_CASE(the_clip_region_of_definition_comes_from_the_host) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  defineFilterClips(wrapper);
  tests::Instance instance(*effect.contextDescriptor);
  instance.create();
  const OfxImageEffectSuiteV1* suite = effect.suites.get<OfxImageEffectSuiteV1>();
  OfxImageClipHandle output = instance.clip(kOfxImageEffectOutputClipName)->handle();
  OfxRectD bounds{0, 0, 0, 0};
  CHECK(suite->clipGetRegionOfDefinition(output, 0, &bounds) == kOfxStatOK);
  CHECK(bounds.x2 == 64.0);
  // An input clip has none of its own in this host.
  OfxImageClipHandle source =
      instance.clip(kOfxImageEffectSimpleSourceClipName)->handle();
  CHECK(suite->clipGetRegionOfDefinition(source, 0, &bounds) == kOfxStatFailed);
  CHECK(suite->clipGetRegionOfDefinition(nullptr, 0, &bounds) == kOfxStatErrBadHandle);
  // Clips are defined while describing, never on an instance.
  CHECK(suite->clipDefine(instance.handle(), "Late", nullptr) == kOfxStatErrBadHandle);
}

// ---------------------------------------------------------------------------
// Parameter values
// ---------------------------------------------------------------------------

namespace {

// An instance with one parameter of each type the tests set values on.
struct Params {
  Params() {
    plugin::ImageEffect wrapper(effect.handle(), effect.suites);
    defineFilterClips(wrapper);
    plugin::ParamSet params = wrapper.params();
    params.defineDouble("scale").setDefaultValue(1.0);
    params.defineDouble("gain");
    params.defineDouble2D("centre");
    params.defineDouble3D("rotation");
    params.defineInt("count");
    params.defineInt2D("origin");
    params.defineInt3D("cell");
    params.defineBoolean("invert");
    params.defineChoice("mode");
    params.defineStrChoice("preset");
    params.defineRGB("tint");
    params.defineRGBA("colour");
    params.defineString("caption").setAnimates(true);
    params.defineCustom("shape");
    instance = std::make_unique<tests::Instance>(*effect.contextDescriptor);
    instance->create();
  }

  plugin::ParamSet params() const {
    return plugin::ImageEffect(instance->handle(), effect.suites).params();
  }

  template <class P>
  P get(const char* name) const {
    return params().get<P>(name);
  }

  tests::Effect effect;
  std::unique_ptr<tests::Instance> instance;
};

}  // namespace

TEST_CASE(typed_parameters_round_trip_their_values) {
  Params fixture;
  auto scale = fixture.get<plugin::DoubleParam>("scale");
  CHECK(scale.getValue() == 1.0);  // the descriptor's default
  scale.setValue(2.5);
  CHECK(scale.getValue() == 2.5);
  CHECK(std::string(scale.name()) == "scale");
  CHECK(std::string(scale.type()) == kOfxParamTypeDouble);
  CHECK(scale.handle() != nullptr);
  CHECK(scale.propertySetHandle() != nullptr);

  auto centre = fixture.get<plugin::Double2DParam>("centre");
  centre.setValue({1.5, -2.5});
  CHECK(centre.getValue().x == 1.5);
  CHECK(centre.getValue().y == -2.5);

  auto rotation = fixture.get<plugin::Double3DParam>("rotation");
  rotation.setValue({1.0, 2.0, 3.0});
  CHECK(rotation.getValue()[2] == 3.0);

  auto count = fixture.get<plugin::IntParam>("count");
  count.setValue(7);
  CHECK(count.getValue() == 7);

  auto origin = fixture.get<plugin::Int2DParam>("origin");
  origin.setValue({3, 4});
  CHECK(origin.getValue().y == 4);

  auto cell = fixture.get<plugin::Int3DParam>("cell");
  cell.setValue({5, 6, 7});
  CHECK(cell.getValue()[0] == 5);

  auto invert = fixture.get<plugin::BooleanParam>("invert");
  invert.setValue(true);
  CHECK(invert.getValue());

  auto mode = fixture.get<plugin::ChoiceParam>("mode");
  mode.setValue(2);
  CHECK(mode.getValue() == 2);

  auto preset = fixture.get<plugin::StrChoiceParam>("preset");
  preset.setValue("soft");
  CHECK(preset.getValue() == "soft");

  auto tint = fixture.get<plugin::RGBParam>("tint");
  tint.setValue({0.25, 0.5, 0.75});
  CHECK(tint.getValue().g == 0.5);

  auto colour = fixture.get<plugin::RGBAParam>("colour");
  colour.setValue({0.1, 0.2, 0.3, 0.4});
  CHECK(colour.getValue().a == 0.4);

  auto caption = fixture.get<plugin::StringParam>("caption");
  caption.setValue("hello");
  CHECK(caption.getValue() == "hello");

  auto shape = fixture.get<plugin::CustomParam>("shape");
  shape.setValue("<svg/>");
  CHECK(shape.getValue() == "<svg/>");
}

TEST_CASE(a_keyed_parameter_interpolates_between_its_keys) {
  Params fixture;
  auto scale = fixture.get<plugin::DoubleParam>("scale");
  scale.setValueAtTime(0, 1.0);
  scale.setValueAtTime(4, 3.0);
  CHECK(scale.numKeys() == 2);
  CHECK(scale.keyTime(0) == 0.0);
  CHECK(scale.keyTime(1) == 4.0);
  CHECK(scale.getValueAtTime(2) == 2.0);
  CHECK(scale.getValueAtTime(-1) == 1.0);  // held before the first key
  CHECK(scale.getValueAtTime(9) == 3.0);   // and after the last
  // Writing at a time a key already sits at replaces it.
  scale.setValueAtTime(4, 5.0);
  CHECK(scale.numKeys() == 2);
  CHECK(scale.getValueAtTime(4) == 5.0);

  auto count = fixture.get<plugin::IntParam>("count");
  count.setValueAtTime(0, 0);
  count.setValueAtTime(4, 10);
  CHECK(count.getValueAtTime(1) == 3);  // interpolated and rounded
}

TEST_CASE(a_parameter_that_does_not_interpolate_holds_its_previous_key) {
  Params fixture;
  auto caption = fixture.get<plugin::StringParam>("caption");
  caption.setValueAtTime(0, "first");
  caption.setValueAtTime(4, "second");
  CHECK(caption.numKeys() == 2);
  CHECK(caption.getValueAtTime(3.9) == "first");
  CHECK(caption.getValueAtTime(4) == "second");
}

TEST_CASE(a_parameter_that_does_not_animate_takes_the_key_as_its_value) {
  Params fixture;
  auto invert = fixture.get<plugin::BooleanParam>("invert");
  invert.setValueAtTime(3, true);
  CHECK(invert.numKeys() == 0);
  CHECK(invert.getValue());
  CHECK(invert.getValueAtTime(0));
}

TEST_CASE(a_keyed_parameter_reports_its_slope_and_its_area) {
  Params fixture;
  auto scale = fixture.get<plugin::DoubleParam>("scale");
  scale.setValueAtTime(0, 1.0);
  scale.setValueAtTime(4, 3.0);
  CHECK(scale.getDerivative(2) == 0.5);
  CHECK(scale.getDerivative(-1) == 0.0);  // held, so flat
  CHECK(scale.getDerivative(4) == 0.0);
  CHECK(scale.getIntegral(0, 4) == 8.0);   // the trapezoid under the line
  CHECK(scale.getIntegral(4, 0) == -8.0);  // backwards is the negative
  CHECK(scale.getIntegral(-2, 0) == 2.0);  // held at 1.0 before the first key
  CHECK(scale.getIntegral(2, 2) == 0.0);
  // With no keys at all the value is constant and the slope is zero.
  auto other = fixture.get<plugin::DoubleParam>("scale");
  other.deleteAllKeys();
  other.setValue(2.0);
  CHECK(other.getDerivative(1) == 0.0);
  CHECK(other.getIntegral(0, 3) == 6.0);
}

TEST_CASE(a_keyed_parameter_finds_its_keys_by_time) {
  Params fixture;
  auto scale = fixture.get<plugin::DoubleParam>("scale");
  scale.setValueAtTime(0, 1.0);
  scale.setValueAtTime(4, 2.0);
  scale.setValueAtTime(8, 3.0);
  CHECK(scale.keyIndex(4, 0) == 1);   // the key at this time
  CHECK(scale.keyIndex(5, 0) == -1);  // there is none at this one
  CHECK(scale.keyIndex(5, 1) == 2);   // the first key after it
  CHECK(scale.keyIndex(5, -1) == 1);  // the last key before it
  CHECK(scale.keyIndex(9, 1) == -1);
  CHECK(scale.keyIndex(-1, -1) == -1);
}

TEST_CASE(keys_can_be_deleted_one_at_a_time_or_all_at_once) {
  Params fixture;
  auto scale = fixture.get<plugin::DoubleParam>("scale");
  scale.setValueAtTime(0, 1.0);
  scale.setValueAtTime(4, 2.0);
  scale.deleteKey(0);
  CHECK(scale.numKeys() == 1);
  CHECK(scale.keyTime(0) == 4.0);
  try {
    scale.deleteKey(7);  // no key there
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrBadIndex);
  }
  scale.deleteAllKeys();
  CHECK(scale.numKeys() == 0);
  try {
    scale.keyTime(0);
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrBadIndex);
  }
}

TEST_CASE(a_parameter_copies_another_ones_value_and_keys) {
  Params fixture;
  plugin::ParamSet params = fixture.params();
  auto from = params.get<plugin::DoubleParam>("scale");
  from.setValueAtTime(0, 1.0);
  from.setValueAtTime(4, 3.0);

  // The destination has to be of the same type.
  auto count = params.get<plugin::IntParam>("count");
  try {
    count.copyFrom(from, 0);
    CHECK(false);
  } catch (const openfx::OfxException& e) {
    CHECK(e.code() == kOfxStatErrValue);
  }

  auto to = params.get<plugin::DoubleParam>("gain");
  to.copyFrom(from, 0);
  CHECK(to.numKeys() == 2);
  CHECK(to.keyTime(1) == 4.0);
  CHECK(to.getValueAtTime(2) == 2.0);

  // The offset shifts every key it copies.
  to.copyFrom(from, 10);
  CHECK(to.keyTime(0) == 10.0);
  CHECK(to.keyTime(1) == 14.0);
  CHECK(to.getValueAtTime(12) == 2.0);

  // A range keeps only the keys inside it.
  const OfxRangeD range{3.0, 20.0};
  to.copyFrom(from, 0, &range);
  CHECK(to.numKeys() == 1);
  CHECK(to.keyTime(0) == 4.0);

  // A parameter copied onto itself keeps its keys, shifted.
  from.copyFrom(from, 10);
  CHECK(from.numKeys() == 2);
  CHECK(from.keyTime(0) == 10.0);
  CHECK(from.keyTime(1) == 14.0);
}

TEST_CASE(setting_a_keyed_parameters_value_moves_the_key_at_the_current_time) {
  Params fixture;
  auto scale = fixture.get<plugin::DoubleParam>("scale");
  scale.setValueAtTime(0, 1.0);
  scale.setValueAtTime(4, 3.0);
  host::timeline().current = 4;
  scale.setValue(9.0);  // a keyed parameter has no value apart from its curve
  CHECK(scale.numKeys() == 2);
  CHECK(scale.getValueAtTime(4) == 9.0);
  host::timeline().current = 0;
}

TEST_CASE(a_parameter_reports_its_animation_state) {
  Params fixture;
  auto scale = fixture.get<plugin::DoubleParam>("scale");
  CHECK(!scale.accessor().isAnimating());
  scale.setValueAtTime(1, 2.0);
  CHECK(scale.accessor().isAnimating());
  scale.deleteAllKeys();
  CHECK(!scale.accessor().isAnimating());
  CHECK(scale.props().get<openfx::PropId::OfxParamPropAnimates>() == 1);
}

TEST_CASE(a_parameter_set_brackets_an_edit) {
  Params fixture;
  plugin::ParamSet params = fixture.params();
  params.editBegin("Scale");
  params.editEnd();
  {
    plugin::ParamSet::EditScope scope = params.editScope("Scale");
    params.get<plugin::DoubleParam>("scale").setValue(3.0);
  }
  CHECK(params.get<plugin::DoubleParam>("scale").getValue() == 3.0);
  CHECK(params.handle() != nullptr);
  CHECK(params.suite() == fixture.effect.suites.get<OfxParameterSuiteV1>());
  CHECK(params.props().handle() != nullptr);
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

TEST_CASE(image_memory_is_allocated_locked_and_freed) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  plugin::ImageMemory memory = wrapper.imageMemory(256);
  CHECK(memory.size() == 256);
  CHECK(memory.data() != nullptr);
  CHECK(memory.handle() != nullptr);
  memory.as<char>()[0] = 'x';
  CHECK(memory.as<char>()[0] == 'x');

  plugin::ImageMemory moved = std::move(memory);
  CHECK(moved.size() == 256);
  // The moved-from block owns nothing, so its destructor frees nothing.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK(memory.data() == nullptr);
  CHECK(memory.handle() == nullptr);
}

TEST_CASE(host_memory_is_allocated_and_freed) {
  tests::Effect effect;
  plugin::Memory memory(effect.suites, 128);
  CHECK(memory.size() == 128);
  CHECK(memory.data() != nullptr);
  memory.as<int>()[0] = 42;
  CHECK(memory.as<int>()[0] == 42);

  plugin::Memory moved = std::move(memory);
  CHECK(moved.size() == 128);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK(memory.data() == nullptr);
  CHECK(moved.as<int>()[0] == 42);

  const openfx::SuiteContainer empty;
  CHECK_THROWS_AS(plugin::Memory(empty, 16), openfx::SuiteNotFoundException);
}

// ---------------------------------------------------------------------------
// Multithreading
// ---------------------------------------------------------------------------

TEST_CASE(multi_thread_runs_the_callable_on_every_thread) {
  tests::Effect effect;
  CHECK(plugin::numCPUs(effect.suites) >= 1);
  CHECK(plugin::threadIndex(effect.suites) == 0);  // outside a multiThread call
  CHECK(!plugin::isSpawnedThread(effect.suites));

  std::mutex mutex;
  std::set<unsigned> indices;
  unsigned reportedCount = 0;
  plugin::multiThread(effect.suites, 2, [&](unsigned index, unsigned count) {
    const std::lock_guard<std::mutex> lock(mutex);
    indices.insert(index);
    reportedCount = count;
    CHECK(plugin::threadIndex(effect.suites) == index);
    CHECK(plugin::isSpawnedThread(effect.suites));
  });
  CHECK(indices.size() == 2);
  CHECK(reportedCount == 2);

  // Zero threads means one per CPU.
  std::atomic<unsigned> ran{0};
  plugin::multiThread(effect.suites, 0, [&](unsigned, unsigned) { ++ran; });
  CHECK(ran == plugin::numCPUs(effect.suites));
}

TEST_CASE(multi_thread_rethrows_what_a_thread_threw) {
  tests::Effect effect;
  std::atomic<unsigned> ran{0};
  const auto worker = [&](unsigned index, unsigned) {
    ++ran;
    if (index == 0)
      throw std::runtime_error("no");
  };
  CHECK_THROWS_AS(plugin::multiThread(effect.suites, 2, worker), std::runtime_error);
  CHECK(ran == 2);  // every thread still ran to completion

  const openfx::SuiteContainer empty;
  CHECK_THROWS_AS(plugin::numCPUs(empty), openfx::SuiteNotFoundException);
}

TEST_CASE(a_host_mutex_works_with_a_lock_guard) {
  tests::Effect effect;
  plugin::Mutex mutex(effect.suites);
  CHECK(mutex.handle() != nullptr);
  {
    const std::lock_guard<plugin::Mutex> lock(mutex);
    CHECK(mutex.tryLock());  // the same thread may lock it again
    mutex.unlock();
  }
  CHECK(mutex.tryLock());
  mutex.unlock();

  // A mutex may start out locked.
  plugin::Mutex held(effect.suites, 1);
  held.unlock();
  plugin::Mutex moved = std::move(held);
  CHECK(moved.handle() != nullptr);
}

// ---------------------------------------------------------------------------
// Progress, messages and the timeline
// ---------------------------------------------------------------------------

TEST_CASE(progress_starts_and_ends_when_the_host_offers_it) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  {
    plugin::Progress progress(effect.suites, wrapper.handle(), "Rendering", "render");
    CHECK(progress.active());
    CHECK(progress.update(0.5));
    plugin::Progress moved = std::move(progress);
    CHECK(moved.active());
  }
}

TEST_CASE(progress_without_a_suite_does_nothing) {
  tests::Effect effect;
  const openfx::SuiteContainer empty;
  plugin::Progress progress(empty, nullptr, "Rendering");
  CHECK(!progress.active());
  CHECK(progress.update(0.5));  // and never asks for the task to be abandoned
}

TEST_CASE(messages_reach_the_hosts_message_suite) {
  tests::Effect effect;
  plugin::ImageEffect wrapper(effect.handle(), effect.suites);
  OfxImageEffectHandle handle = wrapper.handle();
  CHECK(plugin::message(effect.suites, handle, kOfxMessageError, "id", "100% wrong") ==
        kOfxStatOK);
  CHECK(plugin::message(effect.suites, handle, kOfxMessageQuestion, "id", "Really?") ==
        kOfxStatReplyYes);
  CHECK(plugin::setPersistentMessage(effect.suites, handle, kOfxMessageError, "id",
                                     "stays") == kOfxStatOK);
  CHECK(plugin::clearPersistentMessage(effect.suites, handle) == kOfxStatOK);

  const openfx::SuiteContainer empty;
  CHECK(plugin::message(empty, handle, kOfxMessageError, "id", "x") ==
        kOfxStatErrMissingHostFeature);
  CHECK(plugin::setPersistentMessage(empty, handle, kOfxMessageError, "id", "x") ==
        kOfxStatErrMissingHostFeature);
  CHECK(plugin::clearPersistentMessage(empty, handle) == kOfxStatErrMissingHostFeature);
}

TEST_CASE(the_timeline_is_read_and_moved_through_its_suite) {
  tests::Effect effect;
  host::timeline() = host::Timeline{1.0, 10.0, 5.0};
  CHECK(plugin::currentTime(effect.suites) == 5.0);
  plugin::gotoTime(effect.suites, 7.0);
  CHECK(plugin::currentTime(effect.suites) == 7.0);
  CHECK(host::timeline().current == 7.0);
  const OfxRangeD bounds = plugin::timeBounds(effect.suites);
  CHECK(bounds.min == 1.0);
  CHECK(bounds.max == 10.0);

  const openfx::SuiteContainer empty;
  CHECK_THROWS_AS(plugin::currentTime(empty), openfx::SuiteNotFoundException);
  CHECK_THROWS_AS(plugin::gotoTime(empty, 0), openfx::SuiteNotFoundException);
  CHECK_THROWS_AS(plugin::timeBounds(empty), openfx::SuiteNotFoundException);
  host::timeline() = host::Timeline{};
}

// ---------------------------------------------------------------------------
// Suites
// ---------------------------------------------------------------------------

TEST_CASE(the_suite_container_holds_what_the_host_offered) {
  tests::Effect effect;
  CHECK(effect.suites.has<OfxPropertySuiteV1>());
  CHECK(effect.suites.has<OfxImageEffectSuiteV1>());
  CHECK(effect.suites.has<OfxParameterSuiteV1>());
  CHECK(effect.suites.has<OfxMultiThreadSuiteV1>());
  CHECK(effect.suites.has(kOfxProgressSuite, 2));
  CHECK(!effect.suites.has(kOfxDrawSuite, 1));
  CHECK(!effect.suites.has<OfxDrawSuiteV1>());
  CHECK(effect.suites.get<OfxDrawSuiteV1>() == nullptr);
  CHECK(effect.suites.get<OfxImageEffectSuiteV1>() == host::effectSuite());
  CHECK(effect.suites.find(kOfxParameterSuite, 1) == host::paramSuite());
  CHECK(effect.suites.find(kOfxParameterSuite, 2) == nullptr);
  CHECK(effect.suites.find("NoSuchSuite", 1) == nullptr);
  // The host answers the same way when asked directly.
  CHECK(effect.host.ofx()->fetchSuite(effect.host.ofx()->host, kOfxPropertySuite, 1) ==
        host::PropertySet::suite());
  CHECK(effect.host.ofx()->fetchSuite(effect.host.ofx()->host, "NoSuchSuite", 1) ==
        nullptr);
}

TEST_CASE(the_suite_container_reports_a_suite_it_does_not_hold) {
  const openfx::SuiteContainer empty;
  // A missing suite is reported, never thrown about, in each of the forms a
  // caller can ask in.
  CHECK(empty.get<OfxProgressSuiteV2>() == nullptr);
  CHECK(empty.get<const OfxProgressSuiteV2>() == nullptr);
  CHECK(empty.get<OfxProgressSuiteV2>(kOfxProgressSuite, 2) == nullptr);
  CHECK(!empty.has<OfxProgressSuiteV2>());
  CHECK(!empty.has(kOfxProgressSuite, 2));
}

TEST_CASE(the_action_args_wrapper_is_a_typed_view_of_a_property_set) {
  tests::Effect effect;
  host::PropertySet inArgs =
      host::PropertySet::forAction(kOfxImageEffectActionRender, "inArgs");
  host::propsets::ImageEffectActionRender_InArgs written(inArgs.handle(),
                                                         host::PropertySet::suite());
  written.setTime(3.0).setRenderWindow({0, 0, 8, 4}).setRenderScale({0.5, 0.5});

  plugin::ActionArgs args(inArgs.handle(), effect.suites);
  CHECK(!args.empty());
  CHECK(args.handle() == inArgs.handle());
  const auto render = args.as<plugin::propsets::ImageEffectActionRender_InArgs>();
  CHECK(render.time() == 3.0);
  CHECK(render.renderWindow()[2] == 8);
  CHECK(render.renderScale()[0] == 0.5);
  CHECK(args.props().get<openfx::PropId::OfxPropTime>() == 3.0);

  const plugin::ActionArgs none(nullptr, effect.suites);
  CHECK(none.empty());
}
