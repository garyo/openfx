// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include "openfx/host/ofxPropSetAccessors.h"
#include "openfx/host/ofxPropertySet.h"
#include "openfx/ofxExceptions.h"
#include "openfx/ofxLog.h"
#include "openfx/ofxPropsAccess.h"
#include "openfx/ofxSuites.h"

namespace openfx::host {

// The three things every host hands a plugin: the OfxHost struct, the
// "ImageEffectHost" property set it points at, and the suites the plugin may
// fetch through it.
//
// Nothing here describes a particular host. A host fills in its identity and
// capabilities through accessor() and registers the suites it offers in
// suites(): its own, the ones openfx::host provides over its effect model
// (PropertySet::suite(), effectSuite(), paramSuite() and so on), and
// addDefaultSuites(suites()) for the generic memory, multithread, message,
// progress and timeline suites. A host that builds its own OfxHost instead
// hands that to Plugin::load(OfxHost*).
class Host {
 public:
  Host() : props_(this) {
    ofx_.host = props_.handle();
    ofx_.fetchSuite = &Host::fetchSuite;
  }

  // The property set carries a back-pointer to this Host, so neither may move.
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  // What a plugin is given through setHost; valid for this Host's lifetime.
  OfxHost* ofx() const { return const_cast<OfxHost*>(&ofx_); }

  PropertySet& props() { return props_; }
  const PropertySet& props() const { return props_; }

  SuiteContainer& suites() { return suites_; }
  const SuiteContainer& suites() const { return suites_; }

  // Type-safe setters for the host's own properties, for fluent use:
  //   host.accessor().setName("org.example.host").setLabel("Example");
  propsets::ImageEffectHost accessor() {
    return propsets::ImageEffectHost(props_.handle(), PropertySet::suite());
  }

 private:
  // fetchSuite is handed only the host's property-set handle, so that set
  // carries a back-pointer to its Host, the way an Image knows its Clip.
  struct HostProperties : PropertySet {
    explicit HostProperties(Host* owner) : PropertySet("ImageEffectHost"), host(owner) {}
    Host* host;
  };

  // The C callback in OfxHost. Nothing may unwind into the plugin through it,
  // so an exception, such as the lookup's failed allocation, means no suite.
  static const void* fetchSuite(OfxPropertySetHandle handle, const char* name,
                                int version) noexcept {
    try {
      auto* props = static_cast<HostProperties*>(PropertySet::from(handle));
      if (!props || !props->host)
        return nullptr;
      const void* suite = props->host->suites_.find(name ? name : "", version);
      if (!suite)
        Logger::debug("suite not provided: {} v{}", name ? name : "", version);
      return suite;
    } catch (...) {
      logCurrentException("fetchSuite {} v{}", name ? name : "", version);
      return nullptr;
    }
  }

  HostProperties props_;
  SuiteContainer suites_;
  OfxHost ofx_{};
};

}  // namespace openfx::host
