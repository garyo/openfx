// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

// openfx::host::Strings: how long a string the property suite returns stays
// valid under each StringLifetime, and openfx::GuardedText, which makes an
// expired one fault when read.

#include <ofxCore.h>
#include <ofxProperty.h>
#include <openfx/host/ofxPropertySet.h>
#include <openfx/host/ofxStringLifetime.h>
#include <openfx/ofxGuardedText.h>

#include <string>
#include <string_view>
#include <thread>

#ifndef _WIN32
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#include "harness.h"
#include "log_capture.h"

namespace host = openfx::host;
using host::StringLifetime;
using host::Strings;

namespace {

constexpr const char* kName = "OfxPropName";
constexpr const char* kLabel = "OfxPropLabel";

// Sets the lifetime for one test and puts back the default after it.
struct Lifetime {
  explicit Lifetime(StringLifetime lifetime) { Strings::setLifetime(lifetime); }
  ~Lifetime() { Strings::setLifetime(StringLifetime::Storage); }
};

// A set with two string properties, read and written through the suite, as a
// plugin would.
struct Props {
  Props() {
    set.define(kName, host::PropertySet::Type::String, 1);
    set.define(kLabel, host::PropertySet::Type::String, 2);
    set.set(kName, 0, "name");
    set.set(kLabel, 0, "label0");
    set.set(kLabel, 1, "label1");
  }
  const char* get(const char* name, int index = 0) const {
    char* value = nullptr;
    suite->propGetString(set.handle(), name, index, &value);
    return value;
  }
  void put(const char* name, const char* value, int index = 0) {
    suite->propSetString(set.handle(), name, index, value);
  }

  host::PropertySet set;
  const OfxPropertySuiteV1* suite = host::PropertySet::suite();
};

// Whether the store expired a string, asked by address: reading it would fault.
bool expired(const char* s) { return Strings::expired(s); }

}  // namespace

TEST_CASE(storage_returns_the_hosts_own_string) {
  Props props;
  const char* first = props.get(kName);
  CHECK(first == props.get(kName));
  CHECK(std::string(first) == "name");
}

TEST_CASE(lifetime_names_round_trip) {
  for (auto lifetime : {StringLifetime::Storage, StringLifetime::EntryCall,
                        StringLifetime::SameValue, StringLifetime::NextCall})
    CHECK(host::stringLifetimeFromName(host::stringLifetimeName(lifetime)) == lifetime);
  CHECK(!host::stringLifetimeFromName("forever"));
}

TEST_CASE(entry_call_strings_last_until_the_entry_call_returns) {
  Lifetime lifetime(StringLifetime::EntryCall);
  Props props;
  const char* name = nullptr;
  {
    Strings::EntryCall call;
    name = props.get(kName);
    const char* again = props.get(kName);
    props.get(kLabel, 1);
    CHECK(std::string(name) == "name");
    CHECK(std::string(again) == "name");
    CHECK(name != again);  // each read is a copy of its own
  }
  CHECK(expired(name));
}

TEST_CASE(entry_call_strings_expire_when_the_plugin_changes_the_value) {
  Lifetime lifetime(StringLifetime::EntryCall);
  Props props;
  Strings::EntryCall call;
  const char* label0 = props.get(kLabel, 0);
  const char* label1 = props.get(kLabel, 1);
  const char* name = props.get(kName);
  props.put(kLabel, "changed", 0);
  CHECK(expired(label0));
  CHECK(std::string(label1) == "label1");
  CHECK(std::string(name) == "name");
  CHECK(std::string(props.get(kLabel, 0)) == "changed");
}

TEST_CASE(a_change_on_another_thread_expires_the_string_too) {
  Lifetime lifetime(StringLifetime::EntryCall);
  Props props;
  Strings::EntryCall call;
  const char* name = props.get(kName);
  std::thread([&] { props.put(kName, "other"); }).join();
  CHECK(expired(name));
}

TEST_CASE(another_threads_entry_calls_leave_this_threads_strings_alone) {
  Lifetime lifetime(StringLifetime::NextCall);
  Props props;
  Strings::EntryCall call;
  const char* name = props.get(kName);
  std::thread([&] {
    Strings::EntryCall worker;
    props.get(kLabel);
    props.get(kLabel, 1);
  }).join();
  CHECK(std::string(name) == "name");
}

TEST_CASE(nested_entry_calls_expire_only_their_own_strings) {
  Lifetime lifetime(StringLifetime::EntryCall);
  Props props;
  Strings::EntryCall outer;
  const char* name = props.get(kName);
  const char* inner = nullptr;
  {
    Strings::EntryCall nested;
    inner = props.get(kLabel);
  }
  CHECK(expired(inner));
  CHECK(std::string(name) == "name");
}

TEST_CASE(same_value_strings_expire_when_that_value_is_read_again) {
  Lifetime lifetime(StringLifetime::SameValue);
  Props props;
  Strings::EntryCall call;
  const char* label0 = props.get(kLabel, 0);
  const char* label1 = props.get(kLabel, 1);
  const char* name = props.get(kName);
  const char* again = props.get(kLabel, 0);
  CHECK(expired(label0));
  CHECK(std::string(again) == "label0");
  CHECK(std::string(label1) == "label1");
  CHECK(std::string(name) == "name");
}

TEST_CASE(next_call_strings_expire_at_the_next_string_read) {
  Lifetime lifetime(StringLifetime::NextCall);
  Props props;
  Strings::EntryCall call;
  const char* name = props.get(kName);
  int dimension = 0;
  props.suite->propGetDimension(props.set.handle(), kLabel, &dimension);
  CHECK(std::string(name) == "name");  // a call that returns no string
  char* labels[2] = {};
  props.suite->propGetStringN(props.set.handle(), kLabel, 2, labels);
  CHECK(expired(name));
  CHECK(std::string(labels[0]) == "label0");  // one call's strings expire together
  CHECK(std::string(labels[1]) == "label1");
  props.get(kName);
  CHECK(expired(labels[0]));
  CHECK(expired(labels[1]));
}

TEST_CASE(next_call_spans_nested_entry_calls) {
  Lifetime lifetime(StringLifetime::NextCall);
  Props props;
  Strings::EntryCall outer;
  const char* name = props.get(kName);
  {
    Strings::EntryCall nested;
    props.get(kLabel);
  }
  CHECK(expired(name));
}

TEST_CASE(with_no_entry_call_a_string_lasts_until_the_next_string_read) {
  Lifetime lifetime(StringLifetime::EntryCall);
  Props props;
  const char* name = props.get(kName);
  CHECK(std::string(name) == "name");
  props.get(kLabel);
  CHECK(expired(name));
}

TEST_CASE(setting_a_value_from_an_expired_string_is_refused) {
  Lifetime lifetime(StringLifetime::EntryCall);
  Props props;
  const char* name = nullptr;
  {
    Strings::EntryCall call;
    name = props.get(kName);
  }
  CHECK(Strings::expired(name + 3));
  CHECK(!Strings::expired("name"));
  tests::LogCapture log(openfx::Logger::Level::Warning);
  CHECK(props.suite->propSetString(props.set.handle(), kLabel, 0, name) ==
        kOfxStatErrValue);
  CHECK(log.messages.find("already expired") != std::string::npos);
  CHECK(std::string(props.get(kLabel)) == "label0");
}

TEST_CASE(guarded_text_holds_a_copy_until_revoked) {
  std::string source = "OfxBitDepthFloat";
  openfx::GuardedText text(source);
  source[0] = 'X';
  CHECK(std::string(text.c_str()) == "OfxBitDepthFloat");
  CHECK(text.contains(text.c_str() + 5));
  CHECK(!text.contains(source.c_str()));
  openfx::GuardedText moved(std::move(text));
  CHECK(std::string(moved.c_str()) == "OfxBitDepthFloat");
}

#ifndef _WIN32
// Reading a revoked string must stop the reader, so the read happens in a
// child process, which must not exit normally.
TEST_CASE(reading_revoked_guarded_text_faults) {
  openfx::GuardedText text("gone");
  const char* p = text.c_str();
  text.revoke();
  const pid_t child = fork();
  if (child == 0) {
    volatile char c = p[0];
    (void)c;
    _exit(0);
  }
  int status = 0;
  waitpid(child, &status, 0);
  CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == 0));
}
#endif

TEST_CASE(the_hosts_own_reads_and_writes_leave_the_plugins_strings_alone) {
  Lifetime lifetime(StringLifetime::NextCall);
  Props props;
  const OfxPropertySuiteV1* hostSuite = host::PropertySet::hostSuite();
  Strings::EntryCall call;
  const char* name = props.get(kName);
  char* label = nullptr;
  hostSuite->propGetString(props.set.handle(), kLabel, 0, &label);
  hostSuite->propSetString(props.set.handle(), kName, 0, "renamed");
  CHECK(!expired(name));
  CHECK(std::string(name) == "name");
  CHECK(!Strings::expired(label));  // the host's own storage, not a copy
}
