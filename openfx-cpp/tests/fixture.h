// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// An image effect assembled entirely in process: a host with every suite the
// bindings wrap, the suite container a plugin would have fetched from it, a
// stub OfxPlugin that answers every action with kOfxStatReplyDefault, and an
// effect instance over plain in-memory buffers. The plugin-side wrappers then
// run against the host-side objects with no plugin binary anywhere.
//
// tests::Instance is also the smallest thing openfx::host::EffectInstance's
// contract allows, so it doubles as a statement of what a host must implement.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <openfx/host/ofxDefaultSuites.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxHost.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxSuites.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tests {

// The host the tests run against: the suites over the generic effect model,
// plus the default memory, multithread, message, progress and timeline ones.
class Host : public openfx::host::Host {
 public:
  Host() {
    accessor()
        .setType(kOfxTypeImageEffectHost)
        .setName("org.openeffects.unittesthost")
        .setLabel("OpenFX Unit Test Host")
        .setVersion({1, 0, 0})
        .setIsBackground(1)
        .setSupportsTiles(1)
        .setSupportsMultiResolution(1)
        .setSupportedComponents({kOfxImageComponentRGBA, kOfxImageComponentAlpha})
        .setSupportedContexts({kOfxImageEffectContextFilter,
                               kOfxImageEffectContextGeneral,
                               kOfxImageEffectContextGenerator})
        .setSupportedPixelDepths({kOfxBitDepthFloat})
        .setMaxParameters(-1)
        .setMaxPages(0);
    suites().add(kOfxPropertySuite, 1, openfx::host::PropertySet::suite());
    suites().add(kOfxImageEffectSuite, 1, openfx::host::effectSuite());
    suites().add(kOfxParameterSuite, 1, openfx::host::paramSuite());
    openfx::host::addDefaultSuites(suites());
  }
};

// The suites a plugin holds, fetched through OfxHost::fetchSuite exactly as a
// plugin's Load action fetches them.
inline openfx::SuiteContainer fetchSuites(openfx::host::Host& host) {
  openfx::SuiteContainer suites;
  OfxHost* ofxHost = host.ofx();
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxPropertySuite, 1, OfxPropertySuiteV1);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxImageEffectSuite, 1, OfxImageEffectSuiteV1);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxParameterSuite, 1, OfxParameterSuiteV1);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxMemorySuite, 1, OfxMemorySuiteV1);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxMultiThreadSuite, 1, OfxMultiThreadSuiteV1);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxMessageSuite, 1, OfxMessageSuiteV1);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxMessageSuite, 2, OfxMessageSuiteV2);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxProgressSuite, 1, OfxProgressSuiteV1);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxProgressSuite, 2, OfxProgressSuiteV2);
  OPENFX_FETCH_SUITE(suites, ofxHost, kOfxTimeLineSuite, 1, OfxTimeLineSuiteV1);
  return suites;
}

namespace detail {

inline void stubSetHost(OfxHost*) {}

// Every action is handled and nothing is done, which is what a plugin with no
// opinion replies; the tests drive the model through the suites instead.
inline OfxStatus stubMainEntry(const char*, const void*, OfxPropertySetHandle,
                               OfxPropertySetHandle) {
  return kOfxStatReplyDefault;
}

}  // namespace detail

// The OfxPlugin struct a Plugin is driven through, with no binary behind it.
inline OfxPlugin* stubOfxPlugin() {
  static OfxPlugin plugin = {kOfxImageEffectPluginApi,
                             1,
                             "org.openeffects.tests.stub",
                             1,
                             0,
                             detail::stubSetHost,
                             detail::stubMainEntry};
  return &plugin;
}

// One described effect: the host, the plugin's suites, and the descriptors the
// two describe actions produce.
struct Effect {
  explicit Effect(const std::string& context = kOfxImageEffectContextFilter)
      : suites(fetchSuites(host)), plugin(stubOfxPlugin(), "/stub/Stub.ofx.bundle") {
    plugin.load(host);
    global = plugin.describe();
    contextDescriptor = plugin.describeInContext(*global, context);
  }

  OfxImageEffectHandle handle() { return contextDescriptor->handle(); }

  Host host;
  openfx::SuiteContainer suites;
  openfx::host::Plugin plugin;
  std::unique_ptr<openfx::host::EffectDescriptor> global;
  std::unique_ptr<openfx::host::EffectDescriptor> contextDescriptor;
};

// An effect instance whose clips are RGBA float buffers of a fixed size. A
// clip named in `unconnected` has no image to give, the way an unconnected
// input clip has none.
class Instance : public openfx::host::EffectInstance {
 public:
  static constexpr int kWidth = 8;
  static constexpr int kHeight = 4;

  explicit Instance(const openfx::host::EffectDescriptor& descriptor,
                    const openfx::host::InstanceProject& project = {})
      : EffectInstance(descriptor, project) {
    createClips();
  }

  ~Instance() override { destroyInstance(); }

  // The pixels of one clip, four floats per pixel, created empty on first use.
  std::vector<float>& buffer(const std::string& clipName) {
    auto it = buffers_.find(clipName);
    if (it == buffers_.end())
      it = buffers_.emplace(clipName, std::vector<float>(kWidth * kHeight * 4, 0.0f))
               .first;
    return it->second;
  }

  static int rowBytes() { return kWidth * 4 * static_cast<int>(sizeof(float)); }

  // How many images the plugin currently holds, and how many it has ever asked
  // for: what a host leak-checks its own image bookkeeping with.
  int liveImages() const { return static_cast<int>(images_.size()); }
  int fetchCount() const { return fetches_; }
  int releaseCount() const { return releases_; }

  std::string unconnected;

  openfx::host::Image* fetchImage(openfx::host::Clip& clip, OfxTime time,
                                  const OfxRectD* region) override {
    ++fetches_;
    lastFetchTime = time;
    lastFetchHadRegion = region != nullptr;
    if (clip.name() == unconnected)
      return nullptr;
    auto image = std::make_unique<openfx::host::Image>();
    image->clip = &clip;
    const std::string id = clip.name() + "@" + std::to_string(time);
    openfx::host::propsets::Image props(image->handle(),
                                        openfx::host::PropertySet::suite());
    props.setType(kOfxTypeImage)
        .setPixelDepth(kOfxBitDepthFloat)
        .setComponents(kOfxImageComponentRGBA)
        .setPreMultiplication(kOfxImagePreMultiplied)
        .setRenderScale({1.0, 1.0})
        .setPixelAspectRatio(1.0)
        .setData(buffer(clip.name()).data())
        .setBounds({0, 0, kWidth, kHeight})
        .setRegionOfDefinition({0, 0, kWidth, kHeight})
        .setRowBytes(rowBytes())
        .setField(kOfxImageFieldNone)
        .setUniqueIdentifier(id.c_str());
    images_.push_back(std::move(image));
    return images_.back().get();
  }

  void releaseImage(openfx::host::Image& image) override {
    ++releases_;
    images_.erase(std::remove_if(images_.begin(), images_.end(),
                                 [&](const auto& held) { return held.get() == &image; }),
                  images_.end());
  }

  OfxTime lastFetchTime = 0;
  bool lastFetchHadRegion = false;

 protected:
  openfx::host::ClipProperties clipProperties(const openfx::host::Clip&) const override {
    return {};
  }

 private:
  std::map<std::string, std::vector<float>> buffers_;
  std::vector<std::unique_ptr<openfx::host::Image>> images_;
  int fetches_ = 0;
  int releases_ = 0;
};

}  // namespace tests
