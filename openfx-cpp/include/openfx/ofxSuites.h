// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>
#include <ofxDialog.h>
#include <ofxDrawSuite.h>
#include <ofxGPURender.h>
#include <ofxImageEffect.h>
#include <ofxMessage.h>
#include <ofxParam.h>
#include <ofxParametricParam.h>
#include <ofxProgress.h>
#include <ofxProperty.h>
#include <ofxTimeLine.h>

#include <string>
#include <unordered_map>

// clang-format off
/***
  Usage:

  === Adding suites in onLoad:

    openfx::SuiteContainer gSuites;

    OfxStatus OnLoad() {
      OfxParameterSuiteV1* paramSuite =
        static_cast<OfxParameterSuiteV1*>(gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1));
      if (paramSuite)
        gSuites.add(kOfxParameterSuite, 1, paramSuite);

      // or use the convenience macro:
      OPENFX_FETCH_SUITE(gSuites, gHost, kOfxParameterSuite, 1, OfxParameterSuiteV1);
      OPENFX_FETCH_SUITE(gSuites, gHost, kOfxPropertySuite, 1, OfxPropertySuiteV1);
      // ...
    }

    In both cases, missing suites will be null, Use gSuites.has<type>() to check.

  === Getting suites:
    auto paramSuite = gSuites.get<OfxParameterSuiteV1>();
    auto propSuite = gSuites.get<OfxPropertySuiteV1>();
    auto myCustomSuite = gSuites.get<customSuiteType>("customSuiteName", 1);
    if (suites.has<OfxProgressSuiteV2>()) {
      // ...
    }
*/
// clang-format on

// Use this in the Load action of a plugin to fetch suites from the OfxHost
// and store them in the suites container.
#define OPENFX_FETCH_SUITE(container, ofxHost, suiteName, suiteVersion, suiteType) \
  do {                                                                             \
    const OfxHost* _host = (ofxHost);                                              \
    const suiteType* suite = static_cast<const suiteType*>(                        \
        _host->fetchSuite(_host->host, suiteName, suiteVersion));                  \
    if (suite) {                                                                   \
      container.add(suiteName, suiteVersion, suite);                               \
    }                                                                              \
  } while (0)

namespace openfx {

namespace detail {
struct SuiteKey {
  std::string name;
  int version;

  SuiteKey(const std::string& n, int v) : name(n), version(v) {}

  bool operator==(const SuiteKey& other) const {
    return name == other.name && version == other.version;
  }
};

// Hash a SuiteKey
struct SuiteKeyHash {
  size_t operator()(const SuiteKey& key) const {
    return std::hash<std::string>()(key.name) ^ (std::hash<int>()(key.version) << 1u);
  }
};

template <typename>
inline constexpr bool dependentFalse = false;

}  // namespace detail

struct SuiteContainer {
  std::unordered_map<detail::SuiteKey, const void*, detail::SuiteKeyHash> suites;

  template <typename T>
  const T* get(const std::string& name, int version) const {
    return static_cast<const T*>(find(name, version));
  }

  // Returns the stored suite pointer, or nullptr if not registered. Suited
  // for use as the fetchSuite callback a host hands a plugin through OfxHost.
  const void* find(const std::string& name, int version) const {
    auto it = suites.find({name, version});
    return (it != suites.end()) ? it->second : nullptr;
  }

  // The suite of type T, by the name and version OPENFX_DEFINE_SUITE gave
  // that type, or nullptr if none was added. A type with no
  // OPENFX_DEFINE_SUITE does not compile.
  template <typename T>
  const T* get() const {
    static_assert(detail::dependentFalse<T>,
                  "no suite name for this type: register it with OPENFX_DEFINE_SUITE");
    return nullptr;
  }

  // Check if suite is registered in the container
  bool has(const std::string& name, int version) const {
    return find(name, version) != nullptr;
  }

  // Whether the suite of type T was added; specialized with get().
  template <typename T>
  bool has() const {
    static_assert(detail::dependentFalse<T>,
                  "no suite name for this type: register it with OPENFX_DEFINE_SUITE");
    return false;
  }

  template <typename T>
  void add(const std::string& name, int version, T* suite) {
    suites[{name, version}] = static_cast<const void*>(suite);
  }
};

}  // namespace openfx

// Registers a suite type with SuiteContainer under its name and version, so
// get<suiteType>() and has<suiteType>() find it. The standard suites are
// registered below; a host's own suite, or one newer than these headers, is
// registered the same way, at global scope and before any code that looks
// it up:
//
//   OPENFX_DEFINE_SUITE(MyHostWidgetSuiteV1, kMyHostWidgetSuite, 1);
//
// It defines get() for both suiteType and const suiteType, as callers write
// either; each reports a missing suite as nullptr rather than throwing.
#define OPENFX_DEFINE_SUITE(suiteType, suiteName, suiteVersion)                  \
  template <>                                                                    \
  inline const suiteType* openfx::SuiteContainer::get<const suiteType>() const { \
    return static_cast<const suiteType*>(find(suiteName, suiteVersion));         \
  }                                                                              \
  template <>                                                                    \
  inline const suiteType* openfx::SuiteContainer::get<suiteType>() const {       \
    return static_cast<const suiteType*>(find(suiteName, suiteVersion));         \
  }                                                                              \
  template <>                                                                    \
  inline bool openfx::SuiteContainer::has<suiteType>() const {                   \
    return find(suiteName, suiteVersion) != nullptr;                             \
  }

OPENFX_DEFINE_SUITE(OfxTimeLineSuiteV1, kOfxTimeLineSuite, 1);
OPENFX_DEFINE_SUITE(OfxParameterSuiteV1, kOfxParameterSuite, 1);
OPENFX_DEFINE_SUITE(OfxPropertySuiteV1, kOfxPropertySuite, 1);
OPENFX_DEFINE_SUITE(OfxDialogSuiteV1, kOfxDialogSuite, 1);
OPENFX_DEFINE_SUITE(OfxMessageSuiteV1, kOfxMessageSuite, 1);
OPENFX_DEFINE_SUITE(OfxMessageSuiteV2, kOfxMessageSuite, 2);
OPENFX_DEFINE_SUITE(OfxParametricParameterSuiteV1, kOfxParametricParameterSuite, 1);
OPENFX_DEFINE_SUITE(OfxMultiThreadSuiteV1, kOfxMultiThreadSuite, 1);
OPENFX_DEFINE_SUITE(OfxProgressSuiteV1, kOfxProgressSuite, 1);
OPENFX_DEFINE_SUITE(OfxProgressSuiteV2, kOfxProgressSuite, 2);
OPENFX_DEFINE_SUITE(OfxImageEffectOpenGLRenderSuiteV1, kOfxOpenGLRenderSuite, 1);
OPENFX_DEFINE_SUITE(OfxOpenCLProgramSuiteV1, kOfxOpenCLProgramSuite, 1);
OPENFX_DEFINE_SUITE(OfxMemorySuiteV1, kOfxMemorySuite, 1);
OPENFX_DEFINE_SUITE(OfxImageEffectSuiteV1, kOfxImageEffectSuite, 1);
OPENFX_DEFINE_SUITE(OfxDrawSuiteV1, kOfxDrawSuite, 1);
OPENFX_DEFINE_SUITE(OfxInteractSuiteV1, kOfxInteractSuite, 1);
