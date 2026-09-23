// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The owning wrappers next to plain C calls: each adopts a resource a direct C
// call acquired and gives it back exactly once, hands its resource to C code
// with release() so that C code gives it back exactly once, and gives back
// exactly once however it is moved. The suites here count the calls that give
// a resource back and pass each one on to the host's own entry.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProgress.h>
#include <openfx/host/ofxDefaultSuites.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxSuites.h>
#include <openfx/plugin/ofxEffect.h>
#include <openfx/plugin/ofxImage.h>
#include <openfx/plugin/ofxMemory.h>
#include <openfx/plugin/ofxMultiThread.h>
#include <openfx/plugin/ofxParam.h>
#include <openfx/plugin/ofxProgress.h>

#include <memory>
#include <mutex>
#include <utility>

#include "fixture.h"
#include "harness.h"

namespace host = openfx::host;
namespace plugin = openfx::plugin;

namespace {

// What the counted suites below have passed on since the test began.
struct Counts {
  int imageMemoryLocks = 0;
  int imageMemoryUnlocks = 0;
  int imageMemoryFrees = 0;
  int locksHeldAtLastFree = 0;
  int memoryFrees = 0;
  int mutexDestroys = 0;
  int progressEnds = 0;
  int editBegins = 0;
  int editEnds = 0;
  OfxParamSetHandle lastEditEnded = nullptr;
};

Counts counts;

const OfxImageEffectSuiteV1* countedEffectSuite() {
  static const OfxImageEffectSuiteV1 suite = [] {
    OfxImageEffectSuiteV1 s = *host::effectSuite();
    s.imageMemoryLock = [](OfxImageMemoryHandle memory, void** data) {
      ++counts.imageMemoryLocks;
      return host::effectSuite()->imageMemoryLock(memory, data);
    };
    s.imageMemoryUnlock = [](OfxImageMemoryHandle memory) {
      ++counts.imageMemoryUnlocks;
      return host::effectSuite()->imageMemoryUnlock(memory);
    };
    s.imageMemoryFree = [](OfxImageMemoryHandle memory) {
      ++counts.imageMemoryFrees;
      counts.locksHeldAtLastFree = counts.imageMemoryLocks - counts.imageMemoryUnlocks;
      return host::effectSuite()->imageMemoryFree(memory);
    };
    return s;
  }();
  return &suite;
}

const OfxParameterSuiteV1* countedParamSuite() {
  static const OfxParameterSuiteV1 suite = [] {
    OfxParameterSuiteV1 s = *host::paramSuite();
    s.paramEditBegin = [](OfxParamSetHandle set, const char* label) {
      ++counts.editBegins;
      return host::paramSuite()->paramEditBegin(set, label);
    };
    s.paramEditEnd = [](OfxParamSetHandle set) {
      ++counts.editEnds;
      counts.lastEditEnded = set;
      return host::paramSuite()->paramEditEnd(set);
    };
    return s;
  }();
  return &suite;
}

const OfxMemorySuiteV1* countedMemorySuite() {
  static const OfxMemorySuiteV1 suite = [] {
    OfxMemorySuiteV1 s = *host::memorySuite();
    s.memoryFree = [](void* data) {
      ++counts.memoryFrees;
      return host::memorySuite()->memoryFree(data);
    };
    return s;
  }();
  return &suite;
}

const OfxMultiThreadSuiteV1* countedThreadSuite() {
  static const OfxMultiThreadSuiteV1 suite = [] {
    OfxMultiThreadSuiteV1 s = *host::multiThreadSuite();
    s.mutexDestroy = [](OfxMutexHandle mutex) {
      ++counts.mutexDestroys;
      return host::multiThreadSuite()->mutexDestroy(mutex);
    };
    return s;
  }();
  return &suite;
}

OfxStatus countedProgressEnd(void* effect) {
  ++counts.progressEnds;
  return host::progressSuiteV2()->progressEnd(effect);
}

const OfxProgressSuiteV1* countedProgressSuiteV1() {
  static const OfxProgressSuiteV1 suite = [] {
    OfxProgressSuiteV1 s = *host::progressSuiteV1();
    s.progressEnd = countedProgressEnd;
    return s;
  }();
  return &suite;
}

const OfxProgressSuiteV2* countedProgressSuiteV2() {
  static const OfxProgressSuiteV2 suite = [] {
    OfxProgressSuiteV2 s = *host::progressSuiteV2();
    s.progressEnd = countedProgressEnd;
    return s;
  }();
  return &suite;
}

// A described effect, with the counted suites as a plugin would hold them.
struct Counted {
  Counted() {
    counts = Counts{};
    suites.add(kOfxPropertySuite, 1, host::PropertySet::suite());
    suites.add(kOfxImageEffectSuite, 1, countedEffectSuite());
    suites.add(kOfxParameterSuite, 1, countedParamSuite());
    suites.add(kOfxMemorySuite, 1, countedMemorySuite());
    suites.add(kOfxMultiThreadSuite, 1, countedThreadSuite());
    suites.add(kOfxProgressSuite, 1, countedProgressSuiteV1());
    suites.add(kOfxProgressSuite, 2, countedProgressSuiteV2());
  }

  OfxParamSetHandle paramSet() {
    OfxParamSetHandle set = nullptr;
    countedEffectSuite()->getParamSet(effect.handle(), &set);
    return set;
  }

  tests::Effect effect;
  openfx::SuiteContainer suites;
};

// A filter instance whose source clip gives images, counting the images
// released through the fixture's own bookkeeping.
struct Filter {
  Filter() {
    plugin::ImageEffect wrapper(effect.handle(), effect.suites);
    wrapper.defineClip(kOfxImageEffectSimpleSourceClipName);
    wrapper.defineClip(kOfxImageEffectOutputClipName);
    instance = std::make_unique<tests::Instance>(*effect.contextDescriptor);
    instance->create();
  }

  OfxPropertySetHandle fetch() {
    OfxPropertySetHandle image = nullptr;
    effectSuite->clipGetImage(
        instance->clip(kOfxImageEffectSimpleSourceClipName)->handle(), 0, nullptr,
        &image);
    return image;
  }

  plugin::Image adopt(OfxPropertySetHandle image) const {
    return plugin::Image(image, effectSuite, propertySuite);
  }

  tests::Effect effect;
  std::unique_ptr<tests::Instance> instance;
  const OfxImageEffectSuiteV1* effectSuite = host::effectSuite();
  const OfxPropertySuiteV1* propertySuite = host::PropertySet::suite();
};

}  // namespace

// ---------------------------------------------------------------------------
// Image: clipReleaseImage
// ---------------------------------------------------------------------------

TEST_CASE(an_image_adopts_an_image_c_code_fetched) {
  Filter filter;
  OfxPropertySetHandle handle = filter.fetch();
  CHECK(handle != nullptr);
  {
    plugin::Image image = filter.adopt(handle);
    CHECK(image.handle() == handle);
    CHECK(image.rowBytes() == tests::Instance::rowBytes());
    CHECK(filter.instance->liveImages() == 1);
  }
  CHECK(filter.instance->liveImages() == 0);
  CHECK(filter.instance->releaseCount() == 1);

  // Adopted means released, even when the Image cannot be built.
  handle = filter.fetch();
  CHECK_THROWS_AS(plugin::Image(handle, filter.effectSuite, nullptr),
                  openfx::SuiteNotFoundException);
  CHECK(filter.instance->liveImages() == 0);
  CHECK(filter.instance->releaseCount() == 2);
}

TEST_CASE(an_image_hands_its_image_back_to_c_code) {
  Filter filter;
  OfxPropertySetHandle handle = nullptr;
  {
    plugin::Image image(
        filter.effectSuite, filter.propertySuite,
        filter.instance->clip(kOfxImageEffectSimpleSourceClipName)->handle(), 0);
    const OfxPropertySetHandle held = image.handle();
    handle = image.release();
    CHECK(handle == held);
    CHECK(image.empty());
    CHECK(image.release() == nullptr);
  }
  CHECK(filter.instance->liveImages() == 1);
  CHECK(filter.effectSuite->clipReleaseImage(handle) == kOfxStatOK);
  CHECK(filter.instance->liveImages() == 0);
  CHECK(filter.instance->releaseCount() == 1);
}

TEST_CASE(moving_an_image_over_a_live_one_releases_that_one_once) {
  Filter filter;
  {
    plugin::Image first = filter.adopt(filter.fetch());
    plugin::Image second(std::move(first));
    plugin::Image third = filter.adopt(filter.fetch());
    CHECK(filter.instance->liveImages() == 2);
    third = std::move(second);
    CHECK(filter.instance->releaseCount() == 1);
    CHECK(filter.instance->liveImages() == 1);
    CHECK(third.rowBytes() == tests::Instance::rowBytes());
  }
  CHECK(filter.instance->releaseCount() == 2);
  CHECK(filter.instance->liveImages() == 0);
}

// ---------------------------------------------------------------------------
// ImageMemory: imageMemoryUnlock, then imageMemoryFree
// ---------------------------------------------------------------------------

TEST_CASE(image_memory_adopts_an_unlocked_block_c_code_allocated) {
  Counted fx;
  OfxImageMemoryHandle handle = nullptr;
  CHECK(countedEffectSuite()->imageMemoryAlloc(fx.effect.handle(), 64, &handle) ==
        kOfxStatOK);
  {
    plugin::ImageMemory memory(handle, countedEffectSuite(), false, 64);
    CHECK(memory.handle() == handle);
    CHECK(memory.size() == 64);
    CHECK(!memory.isLocked());
    CHECK(memory.data() == nullptr);
    CHECK(memory.lock() != nullptr);
  }
  CHECK(counts.imageMemoryFrees == 1);
  CHECK(counts.locksHeldAtLastFree == 0);
}

TEST_CASE(image_memory_adopts_a_locked_block_c_code_allocated) {
  Counted fx;
  OfxImageMemoryHandle handle = nullptr;
  void* data = nullptr;
  countedEffectSuite()->imageMemoryAlloc(fx.effect.handle(), 64, &handle);
  CHECK(countedEffectSuite()->imageMemoryLock(handle, &data) == kOfxStatOK);
  {
    plugin::ImageMemory memory(handle, countedEffectSuite(), true);
    CHECK(memory.isLocked());
    CHECK(memory.data() == data);
    CHECK(counts.imageMemoryLocks - counts.imageMemoryUnlocks == 1);
  }
  CHECK(counts.imageMemoryFrees == 1);
  CHECK(counts.locksHeldAtLastFree == 0);
}

TEST_CASE(image_memory_hands_its_block_back_to_c_code) {
  Counted fx;
  OfxImageMemoryHandle handle = nullptr;
  {
    plugin::ImageMemory memory =
        plugin::ImageEffect(fx.effect.handle(), fx.suites).imageMemory(64);
    const OfxImageMemoryHandle held = memory.handle();
    CHECK(memory.isLocked());
    handle = memory.release();
    CHECK(handle == held);
    CHECK(memory.handle() == nullptr);
    CHECK(memory.data() == nullptr);
    CHECK(!memory.isLocked());
  }
  CHECK(counts.imageMemoryFrees == 0);
  CHECK(counts.imageMemoryLocks - counts.imageMemoryUnlocks == 1);  // C code's now
  countedEffectSuite()->imageMemoryUnlock(handle);
  CHECK(countedEffectSuite()->imageMemoryFree(handle) == kOfxStatOK);
  CHECK(counts.imageMemoryFrees == 1);
}

TEST_CASE(image_memory_unlocks_and_locks_again) {
  Counted fx;
  plugin::ImageMemory memory =
      plugin::ImageEffect(fx.effect.handle(), fx.suites).imageMemory(64);
  memory.as<char>()[0] = 'x';
  memory.unlock();
  CHECK(!memory.isLocked());
  CHECK(memory.data() == nullptr);
  memory.unlock();  // unlocking what is not locked asks nothing of the host
  CHECK(counts.imageMemoryUnlocks == 1);

  void* data = memory.lock();
  CHECK(data != nullptr);
  CHECK(memory.isLocked());
  CHECK(memory.data() == data);
  CHECK(memory.as<char>()[0] == 'x');
  CHECK(memory.lock() == data);  // nor does locking what is locked
  CHECK(counts.imageMemoryLocks == 2);
}

TEST_CASE(moving_image_memory_over_a_live_block_frees_that_block_once) {
  Counted fx;
  plugin::ImageEffect wrapper(fx.effect.handle(), fx.suites);
  {
    plugin::ImageMemory first = wrapper.imageMemory(64);
    first.as<char>()[0] = 'x';
    plugin::ImageMemory second(std::move(first));
    plugin::ImageMemory third = wrapper.imageMemory(32);
    third = std::move(second);
    CHECK(counts.imageMemoryFrees == 1);
    CHECK(third.size() == 64);
    CHECK(third.as<char>()[0] == 'x');
  }
  CHECK(counts.imageMemoryFrees == 2);
  CHECK(counts.locksHeldAtLastFree == 0);
}

// ---------------------------------------------------------------------------
// Memory: memoryFree
// ---------------------------------------------------------------------------

TEST_CASE(memory_adopts_a_block_c_code_allocated) {
  Counted fx;
  void* data = nullptr;
  CHECK(countedMemorySuite()->memoryAlloc(nullptr, 32, &data) == kOfxStatOK);
  {
    plugin::Memory memory(data, 32, countedMemorySuite());
    CHECK(memory.data() == data);
    CHECK(memory.size() == 32);
  }
  CHECK(counts.memoryFrees == 1);
}

TEST_CASE(memory_hands_its_block_back_to_c_code) {
  Counted fx;
  void* data = nullptr;
  {
    plugin::Memory memory(fx.suites, 32);
    const void* held = memory.data();
    data = memory.release();
    CHECK(data == held);
    CHECK(memory.data() == nullptr);
    CHECK(memory.size() == 0);
  }
  CHECK(counts.memoryFrees == 0);
  CHECK(countedMemorySuite()->memoryFree(data) == kOfxStatOK);
  CHECK(counts.memoryFrees == 1);
}

TEST_CASE(moving_memory_over_a_live_block_frees_that_block_once) {
  Counted fx;
  {
    plugin::Memory first(fx.suites, 32);
    plugin::Memory second(std::move(first));
    plugin::Memory third(fx.suites, 16);
    third = std::move(second);
    CHECK(counts.memoryFrees == 1);
    CHECK(third.size() == 32);
  }
  CHECK(counts.memoryFrees == 2);
}

// ---------------------------------------------------------------------------
// Mutex: mutexDestroy
// ---------------------------------------------------------------------------

TEST_CASE(a_mutex_adopts_a_mutex_c_code_created) {
  Counted fx;
  OfxMutexHandle handle = nullptr;
  CHECK(countedThreadSuite()->mutexCreate(&handle, 0) == kOfxStatOK);
  {
    plugin::Mutex mutex(handle, countedThreadSuite());
    CHECK(mutex.handle() == handle);
    const std::lock_guard<plugin::Mutex> lock(mutex);
  }
  CHECK(counts.mutexDestroys == 1);
}

TEST_CASE(a_mutex_hands_its_mutex_back_to_c_code) {
  Counted fx;
  OfxMutexHandle handle = nullptr;
  {
    plugin::Mutex mutex(fx.suites);
    const OfxMutexHandle held = mutex.handle();
    handle = mutex.release();
    CHECK(handle == held);
    CHECK(mutex.handle() == nullptr);
  }
  CHECK(counts.mutexDestroys == 0);
  CHECK(countedThreadSuite()->mutexDestroy(handle) == kOfxStatOK);
  CHECK(counts.mutexDestroys == 1);
}

TEST_CASE(moving_a_mutex_over_a_live_one_destroys_that_one_once) {
  Counted fx;
  {
    plugin::Mutex first(fx.suites);
    const OfxMutexHandle held = first.handle();
    plugin::Mutex second(std::move(first));
    plugin::Mutex third(fx.suites);
    third = std::move(second);
    CHECK(counts.mutexDestroys == 1);
    CHECK(third.handle() == held);
  }
  CHECK(counts.mutexDestroys == 2);
}

// ---------------------------------------------------------------------------
// Progress: progressEnd
// ---------------------------------------------------------------------------

TEST_CASE(progress_takes_over_a_display_c_code_started) {
  Counted fx;
  OfxImageEffectHandle effect = fx.effect.handle();
  CHECK(countedProgressSuiteV2()->progressStart(effect, "Rendering", "render") ==
        kOfxStatOK);
  {
    plugin::Progress progress =
        plugin::Progress::adoptStarted(effect, countedProgressSuiteV2());
    CHECK(progress.active());
    CHECK(progress.update(0.5));
  }
  CHECK(counts.progressEnds == 1);

  countedProgressSuiteV1()->progressStart(effect, "Rendering");
  {
    const auto progress =
        plugin::Progress::adoptStarted(effect, countedProgressSuiteV1());
    CHECK(progress.active());
  }
  CHECK(counts.progressEnds == 2);

  countedProgressSuiteV2()->progressStart(effect, "Rendering", "render");
  {
    const auto progress = plugin::Progress::adoptStarted(effect, fx.suites);
    CHECK(progress.active());
  }
  CHECK(counts.progressEnds == 3);

  // Without a suite there is nothing to update or end.
  const plugin::Progress none = plugin::Progress::adoptStarted(
      effect, static_cast<const OfxProgressSuiteV2*>(nullptr));
  CHECK(!none.active());
}

TEST_CASE(progress_hands_its_display_back_to_c_code) {
  Counted fx;
  OfxImageEffectHandle effect = nullptr;
  {
    plugin::Progress progress(fx.suites, fx.effect.handle(), "Rendering");
    effect = progress.release();
    CHECK(effect == fx.effect.handle());
    CHECK(!progress.active());
    CHECK(progress.release() == nullptr);
  }
  CHECK(counts.progressEnds == 0);
  CHECK(countedProgressSuiteV2()->progressEnd(effect) == kOfxStatOK);
  CHECK(counts.progressEnds == 1);
}

TEST_CASE(moving_progress_over_a_live_one_ends_that_one_once) {
  Counted fx;
  {
    plugin::Progress first(fx.suites, fx.effect.handle(), "First");
    plugin::Progress second(std::move(first));
    plugin::Progress third(fx.suites, fx.effect.handle(), "Third");
    third = std::move(second);
    CHECK(counts.progressEnds == 1);
    CHECK(third.active());
  }
  CHECK(counts.progressEnds == 2);
}

// ---------------------------------------------------------------------------
// ParamSet::EditScope: paramEditEnd
// ---------------------------------------------------------------------------

// params() returns a ParamSet by value, so the scope outlives it. A scope that
// kept a pointer to the ParamSet would read the dead temporary when it ended,
// which AddressSanitizer reports as stack-use-after-scope.
TEST_CASE(an_edit_scope_outlives_the_parameter_set_it_came_from) {
  Counted fx;
  plugin::ImageEffect effect(fx.effect.handle(), fx.suites);
  {
    auto scope = effect.params().editScope("drag");
    effect.params().defineDouble("scale");
    CHECK(counts.editBegins == 1);
  }
  CHECK(counts.editEnds == 1);
  CHECK(counts.lastEditEnded == fx.paramSet());
  CHECK(fx.effect.contextDescriptor->params().find("scale") != nullptr);
}

TEST_CASE(an_edit_scope_takes_over_an_edit_c_code_began) {
  Counted fx;
  OfxParamSetHandle set = fx.paramSet();
  CHECK(countedParamSuite()->paramEditBegin(set, "drag") == kOfxStatOK);
  {
    auto scope = plugin::ParamSet::EditScope::adoptBegun(set, countedParamSuite());
    CHECK(counts.editEnds == 0);
  }
  CHECK(counts.editEnds == 1);
  CHECK(counts.lastEditEnded == set);
}

TEST_CASE(an_edit_scope_hands_its_edit_back_to_c_code) {
  Counted fx;
  OfxParamSetHandle set = fx.paramSet();
  OfxParamSetHandle released = nullptr;
  {
    plugin::ParamSet::EditScope scope(set, countedParamSuite(), "drag");
    CHECK(counts.editBegins == 1);
    released = scope.release();
    CHECK(released == set);
    CHECK(scope.release() == nullptr);
  }
  CHECK(counts.editEnds == 0);
  CHECK(countedParamSuite()->paramEditEnd(released) == kOfxStatOK);
  CHECK(counts.editEnds == 1);
}

TEST_CASE(moving_an_edit_scope_over_a_live_one_ends_that_one_once) {
  Counted fx;
  OfxParamSetHandle set = fx.paramSet();
  {
    plugin::ParamSet::EditScope first(set, countedParamSuite(), "first");
    plugin::ParamSet::EditScope second(std::move(first));
    plugin::ParamSet::EditScope third(set, countedParamSuite(), "third");
    third = std::move(second);
    CHECK(counts.editEnds == 1);
  }
  CHECK(counts.editBegins == 2);
  CHECK(counts.editEnds == 2);
}
