// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// The C boundary: every function the other side of the API calls through a C
// function pointer must answer with a status and never let an exception out,
// since unwinding into a C caller is undefined behaviour. Each test provokes an
// exception behind one such function, or hands it a null a C caller could,
// and calls it the way a plugin or host would: through the pointer.

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxMultiThread.h>
#include <ofxProperty.h>
#include <openfx/host/ofxDrawSuiteHost.h>
#include <openfx/host/ofxEffect.h>
#include <openfx/host/ofxHost.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/ofxExceptions.h>
#include <openfx/ofxLog.h>
#include <openfx/ofxSuites.h>
#include <openfx/plugin/ofxInteract.h>
#include <openfx/plugin/ofxMultiThread.h>
#include <openfx/plugin/ofxPluginBase.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>

#include "fixture.h"
#include "harness.h"

#if defined(__SANITIZE_ADDRESS__)
#  define OPENFX_TESTS_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define OPENFX_TESTS_ASAN 1
#  endif
#endif

namespace host = openfx::host;
namespace plugin = openfx::plugin;

namespace {

// A log handler that throws, as a host's might when its log cannot be written.
void installThrowingLogHandler() {
  openfx::Logger::setLogHandler(
      [](openfx::Logger::Level, std::chrono::system_clock::time_point,
         const std::string&) { throw std::runtime_error("the log handler threw"); });
}

// Back to the run's silent handler, or to the default one when the log is on.
void restoreLogHandler() {
  openfx::Logger::setLogHandler(
      std::getenv("OPENFX_TEST_LOG")
          ? openfx::Logger::LogHandler()
          : openfx::Logger::LogHandler([](openfx::Logger::Level,
                                          std::chrono::system_clock::time_point,
                                          const std::string&) {}));
}

// An instance whose host hooks throw: fetchImage the way `what` says, and
// abort always.
class ThrowingInstance : public tests::Instance {
 public:
  enum class What { OfxError, Standard };
  What what = What::OfxError;

  using tests::Instance::Instance;

  host::Image* fetchImage(host::Clip&, OfxTime, const OfxRectD*) override {
    if (what == What::OfxError)
      throw openfx::OfxException(kOfxStatErrMemory, "fetchImage");
    throw std::runtime_error("fetchImage");
  }

  bool abort() override { throw std::runtime_error("abort"); }
};

// A draw context whose host side throws on every primitive.
class ThrowingDrawContext : public host::DrawContext {
 public:
  OfxRGBAColourF standardColour(OfxStandardColour) const override { return {}; }

 protected:
  void onSetColour(const OfxRGBAColourF&) override {}
  void onSetLineWidth(float) override {}
  void onSetLineStipple(OfxDrawLineStipplePattern) override {}
  void onDraw(OfxDrawPrimitive, const OfxPointD*, int) override {
    throw std::runtime_error("onDraw");
  }
  void onDrawText(const char*, const OfxPointD&, int) override {}
};

// A plugin that cannot be constructed, counting the tries. Each N is a plugin
// of its own, since each PluginEntry holds its own instance.
int unconstructibleAttempts = 0;

template <int N>
class Unconstructible : public plugin::ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.tests.unconstructible";

  Unconstructible() {
    ++unconstructibleAttempts;
    throw std::runtime_error("no plugin today");
  }
};

// A plugin whose render fails with a status of its choosing.
class FailingRender : public plugin::ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.tests.failingrender";

 protected:
  OfxStatus render(plugin::ImageEffect&, plugin::ActionArgs&) override {
    throw openfx::OfxException(kOfxStatErrImageFormat, "render");
  }
};

// An overlay that handles nothing: only the dispatching around it matters.
class Overlay : public plugin::InteractPlugin<Overlay> {};

// A host's multithread suite cut down to what these tests need: multiThread
// runs each thread's share on the calling thread in turn, as a C host might
// on a single core, and mutexUnLock always fails.
struct StubThreadSuite {
  static inline int unlocks = 0;
  static inline int mutex = 0;

  static OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned n, void* arg) {
    for (unsigned i = 0; i < n; ++i) func(i, n, arg);
    return kOfxStatOK;
  }
  static OfxStatus numCPUs(unsigned* n) {
    *n = 1;
    return kOfxStatOK;
  }
  static OfxStatus index(unsigned* i) {
    *i = 0;
    return kOfxStatOK;
  }
  static int isSpawned() { return 0; }
  static OfxStatus create(OfxMutexHandle* m, int) {
    *m = reinterpret_cast<OfxMutexHandle>(&mutex);
    return kOfxStatOK;
  }
  static OfxStatus destroy(OfxMutexHandle) { return kOfxStatOK; }
  static OfxStatus lock(OfxMutexHandle) { return kOfxStatOK; }
  static OfxStatus unlock(OfxMutexHandle) {
    ++unlocks;
    return kOfxStatErrBadHandle;
  }
  static OfxStatus tryLock(OfxMutexHandle) { return kOfxStatOK; }

  static openfx::SuiteContainer suites() {
    static const OfxMultiThreadSuiteV1 suite = {
        multiThread, numCPUs, index, isSpawned, create, destroy, lock, unlock, tryLock};
    openfx::SuiteContainer container;
    container.add(kOfxMultiThreadSuite, 1, &suite);
    return container;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Host side: the suites a plugin calls
// ---------------------------------------------------------------------------

// A null property name is an unknown property, not a string to measure.
TEST_CASE(every_property_suite_entry_refuses_a_null_name) {
  host::PropertySet set;
  set.define("n", host::PropertySet::Type::Int, 1);
  const OfxPropertySetHandle h = set.handle();
  const OfxPropertySuiteV1* suite = host::PropertySet::suite();

  int i = 0;
  double d = 0;
  char* str = nullptr;
  void* ptr = nullptr;
  const char* text = "";
  CHECK(suite->propSetPointer(h, nullptr, 0, nullptr) == kOfxStatErrUnknown);
  CHECK(suite->propSetString(h, nullptr, 0, text) == kOfxStatErrUnknown);
  CHECK(suite->propSetDouble(h, nullptr, 0, 1.0) == kOfxStatErrUnknown);
  CHECK(suite->propSetInt(h, nullptr, 0, 1) == kOfxStatErrUnknown);
  CHECK(suite->propSetPointerN(h, nullptr, 1, &ptr) == kOfxStatErrUnknown);
  CHECK(suite->propSetStringN(h, nullptr, 1, &text) == kOfxStatErrUnknown);
  CHECK(suite->propSetDoubleN(h, nullptr, 1, &d) == kOfxStatErrUnknown);
  CHECK(suite->propSetIntN(h, nullptr, 1, &i) == kOfxStatErrUnknown);
  CHECK(suite->propGetPointer(h, nullptr, 0, &ptr) == kOfxStatErrUnknown);
  CHECK(suite->propGetString(h, nullptr, 0, &str) == kOfxStatErrUnknown);
  CHECK(suite->propGetDouble(h, nullptr, 0, &d) == kOfxStatErrUnknown);
  CHECK(suite->propGetInt(h, nullptr, 0, &i) == kOfxStatErrUnknown);
  CHECK(suite->propGetPointerN(h, nullptr, 1, &ptr) == kOfxStatErrUnknown);
  CHECK(suite->propGetStringN(h, nullptr, 1, &str) == kOfxStatErrUnknown);
  CHECK(suite->propGetDoubleN(h, nullptr, 1, &d) == kOfxStatErrUnknown);
  CHECK(suite->propGetIntN(h, nullptr, 1, &i) == kOfxStatErrUnknown);
  CHECK(suite->propReset(h, nullptr) == kOfxStatErrUnknown);
  CHECK(suite->propGetDimension(h, nullptr, &i) == kOfxStatErrUnknown);
  // A bad handle is still reported first, and a good name still works.
  CHECK(suite->propGetInt(nullptr, nullptr, 0, &i) == kOfxStatErrBadHandle);
  CHECK(suite->propSetInt(h, "n", 0, 7) == kOfxStatOK);
  CHECK(suite->propGetInt(h, "n", 0, &i) == kOfxStatOK);
  CHECK(i == 7);
}

// The warning a type mismatch logs goes to a handler that throws: the set
// still answers, with an error, rather than unwinding into the plugin.
TEST_CASE(a_throwing_log_handler_does_not_escape_a_suite_call) {
  const openfx::Logger::Level level = openfx::Logger::getLevel();
  openfx::Logger::setLevel(openfx::Logger::Level::Debug);
  installThrowingLogHandler();

  host::PropertySet set;
  set.define("n", host::PropertySet::Type::Int, 1);
  const OfxStatus status =
      host::PropertySet::suite()->propSetString(set.handle(), "n", 0, "x");
  CHECK(status != kOfxStatOK);

  restoreLogHandler();
  openfx::Logger::setLevel(level);
}

// A request the allocator cannot meet is kOfxStatErrMemory and a null handle.
TEST_CASE(image_memory_alloc_reports_an_impossible_size) {
  const OfxImageEffectSuiteV1* suite = host::effectSuite();
  // A plugin calls through the pointer from its own binary. So does this, by
  // way of a pointer the optimiser cannot follow: inlined, the call's
  // allocation is one nothing reads, which the optimiser may drop, and with
  // it the failure under test.
  decltype(suite->imageMemoryAlloc) volatile alloc = suite->imageMemoryAlloc;
  OfxImageMemoryHandle memory = nullptr;
  CHECK(alloc(nullptr, 64, &memory) == kOfxStatOK);
  CHECK(memory != nullptr);
  CHECK(suite->imageMemoryFree(memory) == kOfxStatOK);
#ifndef OPENFX_TESTS_ASAN
  // AddressSanitizer's allocator treats a request this size as a fatal error
  // instead of failing it, so this part runs only without it. The handle still
  // holds the freed block's address, which the failure must clear.
  CHECK(alloc(nullptr, SIZE_MAX / 2, &memory) == kOfxStatErrMemory);
  CHECK(memory == nullptr);
#endif
}

// The host's fetchImage throwing is the plugin's clipGetImage failing: an
// OfxException's code as the status, anything else kOfxStatFailed. An abort
// query that throws is "carry on".
TEST_CASE(a_throwing_host_hook_reaches_the_plugin_as_a_status) {
  tests::Effect effect;
  effect.contextDescriptor->defineClip(kOfxImageEffectOutputClipName);
  ThrowingInstance instance(*effect.contextDescriptor);
  const OfxImageEffectSuiteV1* suite = host::effectSuite();
  const OfxImageClipHandle clip = instance.clip(kOfxImageEffectOutputClipName)->handle();
  OfxPropertySetHandle image = nullptr;

  instance.what = ThrowingInstance::What::OfxError;
  CHECK(suite->clipGetImage(clip, 0, nullptr, &image) == kOfxStatErrMemory);
  instance.what = ThrowingInstance::What::Standard;
  CHECK(suite->clipGetImage(clip, 0, nullptr, &image) == kOfxStatFailed);
  CHECK(image == nullptr);

  CHECK(suite->abort(instance.handle()) == 0);
}

// fetchSuite logs a suite it does not have; with that log throwing, a null or
// unknown name is still just no suite.
TEST_CASE(fetch_suite_survives_a_null_name_and_a_throwing_log) {
  tests::Host host;
  OfxHost* ofx = host.ofx();
  const openfx::Logger::Level level = openfx::Logger::getLevel();
  openfx::Logger::setLevel(openfx::Logger::Level::Debug);
  installThrowingLogHandler();

  CHECK(ofx->fetchSuite(ofx->host, nullptr, 1) == nullptr);
  CHECK(ofx->fetchSuite(ofx->host, "OfxNoSuchSuite", 1) == nullptr);
  CHECK(ofx->fetchSuite(ofx->host, kOfxPropertySuite, 1) == host::PropertySet::suite());

  restoreLogHandler();
  openfx::Logger::setLevel(level);
}

// The draw suite hands a plugin a status when the host's renderer throws.
TEST_CASE(a_throwing_draw_context_reaches_the_plugin_as_a_status) {
  ThrowingDrawContext context;
  context.open();
  const OfxPointD points[2] = {{0, 0}, {1, 1}};
  CHECK(host::drawSuite()->draw(context.handle(), kOfxDrawPrimitiveLines, points, 2) ==
        kOfxStatFailed);
  context.close();
}

// ---------------------------------------------------------------------------
// Plugin side: the entry points a host calls
// ---------------------------------------------------------------------------

// The plugin is constructed on first use. When that is in setHost and the
// constructor throws, setHost keeps the failure and the main entry reports it
// to Load without trying again; when it is in the main entry, that call fails.
TEST_CASE(a_plugin_that_cannot_be_constructed_fails_to_load) {
  tests::Host host;
  OfxPlugin* entry = plugin::PluginEntry<Unconstructible<1>>::get(0);
  entry->setHost(host.ofx());
  CHECK(unconstructibleAttempts == 1);
  CHECK(entry->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatErrFatal);
  CHECK(entry->mainEntry(kOfxActionUnload, nullptr, nullptr, nullptr) ==
        kOfxStatErrFatal);
  CHECK(unconstructibleAttempts == 1);

  OfxPlugin* direct = plugin::PluginEntry<Unconstructible<2>>::get(0);
  CHECK(direct->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatErrFatal);
  CHECK(unconstructibleAttempts == 2);
}

// The log of what dispatch caught goes to a handler that throws: the host
// still gets the status the plugin threw.
TEST_CASE(the_main_entry_returns_the_thrown_status_when_its_log_throws) {
  tests::Effect effect;
  OfxPlugin* entry = plugin::PluginEntry<FailingRender>::get(0);
  entry->setHost(effect.host.ofx());
  CHECK(entry->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) ==
        kOfxStatReplyDefault);

  installThrowingLogHandler();
  CHECK(entry->mainEntry(kOfxImageEffectActionRender, effect.handle(), nullptr,
                         nullptr) == kOfxStatErrImageFormat);
  restoreLogHandler();
}

// The same for an overlay's entry point: an action that fails, here for want
// of the interact suite, and a log of it that throws.
TEST_CASE(an_overlay_entry_point_returns_a_status_when_its_log_throws) {
  static const openfx::SuiteContainer noSuites;
  auto* entry = reinterpret_cast<OfxPluginEntryPoint*>(Overlay::entryPoint(noSuites));
  int interact = 0;

  installThrowingLogHandler();
  CHECK(entry(kOfxInteractActionDraw, &interact, nullptr, nullptr) ==
        kOfxStatErrMissingHostFeature);
  restoreLogHandler();
}

// std::lock_guard's destructor unlocks, and an exception out of a destructor
// terminates the program: a host whose mutexUnLock fails is logged instead,
// best effort, so not even a log handler that throws gets out.
TEST_CASE(a_failing_unlock_does_not_terminate_a_lock_guard) {
  const openfx::SuiteContainer suites = StubThreadSuite::suites();
  plugin::Mutex mutex(suites);
  std::string logged;
  openfx::Logger::setLogHandler([&](openfx::Logger::Level,
                                    std::chrono::system_clock::time_point,
                                    const std::string& message) { logged = message; });
  {
    const std::lock_guard<plugin::Mutex> lock(mutex);
  }
  CHECK(StubThreadSuite::unlocks == 1);
  CHECK(logged.find("mutexUnLock") != std::string::npos);

  installThrowingLogHandler();
  {
    const std::lock_guard<plugin::Mutex> lock(mutex);
  }
  CHECK(StubThreadSuite::unlocks == 2);
  restoreLogHandler();
}

// The host calls the thread function from C, so a worker's exception stays
// inside it and comes back on the calling thread once the host's multiThread
// returns; of several, the first.
TEST_CASE(multi_thread_carries_a_workers_exception_across_the_c_boundary) {
  CHECK(noexcept(plugin::detail::threadTrampoline(0, 1, nullptr)));
  const openfx::SuiteContainer suites = StubThreadSuite::suites();
  unsigned ran = 0;
  const auto worker = [&](unsigned index, unsigned) {
    ++ran;
    if (index == 1)
      throw std::runtime_error("first");
    if (index == 2)
      throw std::logic_error("second");
  };
  CHECK_THROWS_AS(plugin::multiThread(suites, 3, worker), std::runtime_error);
  CHECK(ran == 3);
}
