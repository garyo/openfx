// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// A check, from the plugin's side, of how long a host keeps the strings it
// returns, under one of the lifetimes proposed in issue #273
// (openfx/host/ofxStringLifetime.h implements the same rules on the host's
// side):
//
//   entry-call  until the entry call returns, or the plugin changes the value
//   same-value  as entry-call, and until the plugin reads that value again
//   next-call   until the plugin's next suite call that returns a string
//
// On a thread with no entry call in progress each of them lasts only until
// the next suite call there that returns a string.
//
// auditStrings() wraps an OfxPlugin. The plugin's setHost is handed a copy of
// the host's OfxHost whose fetchSuite returns the property, parameter, image
// effect and multithread suites with checks around them; every other suite is
// the host's own. For each string the host returns, and the data of each
// bytes parameter value (which the rules treat as a string), it keeps a copy
// and, when the lifetime ends, checks the host's string still reads the same;
// one that does not is logged as an error. A string from a value the plugin
// changes itself, or from an image the plugin has released, is not checked
// further.
//
// The other way, every string value the plugin passes the host goes as a
// temporary copy in pages of its own (openfx::GuardedText), which are made
// inaccessible as soon as the suite call returns. A host that kept the
// pointer instead of copying the string faults when it reads it, in its own
// code, and one that hands the pointer back to the plugin is reported.
// Property names and other label strings are passed as they are.
//
// The check compares memory, so a host that frees a string early but leaves
// its bytes alone passes it. Run the host with AddressSanitizer's runtime
// loaded and the plugin built with it, and the check's read of the freed
// string is reported as a use after free.
//
// The entry calls the audit sees are the plugin's main entry, setHost, the
// interact entry points the plugin declares and multithread thread functions;
// custom parameter interpolation callbacks are not wrapped.

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#  include <unistd.h>
#endif

#include "openfx/ofxExceptions.h"
#include "openfx/ofxGuardedText.h"
#include "openfx/ofxLog.h"

namespace ofxtest {

enum class StringRule { EntryCall, SameValue, NextCall };

inline const char* stringRuleName(StringRule rule) {
  switch (rule) {
    case StringRule::EntryCall:
      return "entry-call";
    case StringRule::SameValue:
      return "same-value";
    case StringRule::NextCall:
      return "next-call";
  }
  return "?";
}

inline std::optional<StringRule> stringRuleFromName(std::string_view name) {
  for (auto rule : {StringRule::EntryCall, StringRule::SameValue, StringRule::NextCall})
    if (name == stringRuleName(rule))
      return rule;
  return std::nullopt;
}

namespace audit {

// What the audit knows about a host's string, or a bytes parameter's data,
// while it checks it.
struct Record {
  const char* text;
  std::string copy;
  const void* owner;  // property set or parameter
  std::string name;   // the property, or empty for a parameter's value
  int index;
  double time;
  std::string action;  // the entry call it was returned in
  bool bytes = false;  // data of copy.size() bytes rather than a C string
};

// The value of a parameter read without a time.
inline constexpr double kNow = -std::numeric_limits<double>::infinity();

struct Frame {
  std::string action;
  std::vector<Record> records;
  std::size_t violations = 0;  // found while it was the innermost
};

struct ThreadRecords;

struct ParamInfo {
  enum class Kind { None, Int, Double, String, Bytes };
  Kind kind = Kind::None;
  int arity = 0;
  std::string name;
};

inline ParamInfo paramInfo(std::string_view type, std::string name) {
  using Kind = ParamInfo::Kind;
  static constexpr std::pair<std::string_view, std::pair<Kind, int>> kKinds[] = {
      {kOfxParamTypeInteger, {Kind::Int, 1}},
      {kOfxParamTypeDouble, {Kind::Double, 1}},
      {kOfxParamTypeBoolean, {Kind::Int, 1}},
      {kOfxParamTypeChoice, {Kind::Int, 1}},
      {kOfxParamTypeStrChoice, {Kind::String, 1}},
      {kOfxParamTypeRGBA, {Kind::Double, 4}},
      {kOfxParamTypeRGB, {Kind::Double, 3}},
      {kOfxParamTypeDouble2D, {Kind::Double, 2}},
      {kOfxParamTypeInteger2D, {Kind::Int, 2}},
      {kOfxParamTypeDouble3D, {Kind::Double, 3}},
      {kOfxParamTypeInteger3D, {Kind::Int, 3}},
      {kOfxParamTypeString, {Kind::String, 1}},
      {kOfxParamTypeCustom, {Kind::String, 1}},
      {kOfxParamTypeBytes, {Kind::Bytes, 1}},
  };
  for (const auto& [t, kind] : kKinds)
    if (t == type)
      return {kind.first, kind.second, std::move(name)};
  return {Kind::None, 0, std::move(name)};
}

struct State {
  std::mutex mutex;
  StringRule rule = StringRule::EntryCall;
  bool failOnViolation = false;
  OfxHost* realHost = nullptr;
  OfxHost host{};
  const OfxPropertySuiteV1* props = nullptr;
  const OfxParameterSuiteV1* params = nullptr;
  const OfxImageEffectSuiteV1* effects = nullptr;
  const OfxMultiThreadSuiteV1* threads = nullptr;
  OfxPropertySuiteV1 auditedProps{};
  OfxParameterSuiteV1 auditedParams{};
  OfxImageEffectSuiteV1 auditedEffects{};
  OfxMultiThreadSuiteV1 auditedThreads{};
  std::vector<ThreadRecords*> threadRecords;
  // Parameter types by name, from paramDefine, and the handles paramGetHandle
  // gave out, so a parameter's varargs can be decoded without asking the
  // host, which would itself be a string-returning call. Names are taken to
  // mean one type across the plugins of a binary.
  std::map<std::string, std::string, std::less<>> paramTypes;
  std::map<const void*, ParamInfo> paramHandles;
  // The plugin's strings the host has had, made inaccessible, the most
  // recent kKeepReleased of them.
  std::deque<openfx::GuardedText> released;
  std::map<const char*, const openfx::GuardedText*> releasedAt;
  std::size_t checked = 0, violations = 0, keptPointers = 0;
};

inline State& state() {
  static State s;
  return s;
}

// One thread's frames: the first for when no entry call is in progress, then
// one per entry call, innermost last.
struct ThreadRecords {
  std::vector<Frame> frames = std::vector<Frame>(1);
  ThreadRecords() {
    std::lock_guard lock(state().mutex);
    state().threadRecords.push_back(this);
  }
  ~ThreadRecords() {
    std::lock_guard lock(state().mutex);
    auto& all = state().threadRecords;
    all.erase(std::find(all.begin(), all.end(), this));
  }
};

inline ThreadRecords& thread() {
  thread_local ThreadRecords t;
  return t;
}

// Up to 60 of n bytes, escaped: a string that may no longer be one, or data.
inline std::string printable(const char* s, std::size_t n) {
  std::string out;
  for (std::size_t i = 0; i < n && i < 60; ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 0x20 && c < 0x7f && c != '"') {
      out += static_cast<char>(c);
    } else {
      char hex[5];
      std::snprintf(hex, sizeof hex, "\\x%02x", c);
      out += hex;
    }
  }
  return out;
}

// With the mutex held.
inline std::string describe(const Record& r) {
  if (!r.name.empty())
    return r.name + "[" + std::to_string(r.index) + "]";
  auto it = state().paramHandles.find(r.owner);
  std::string param =
      "parameter " + (it == state().paramHandles.end() ? "?" : it->second.name);
  return r.time == kNow ? param : param + " at " + std::to_string(r.time);
}

// With the mutex held: n bytes from p into out, or false if the memory cannot
// be read. The bytes go through the kernel, which refuses an unreadable
// address rather than faulting, so a host that unmapped a string is reported
// rather than taking the plugin down; under AddressSanitizer a freed one is
// reported as a use after free.
inline bool safeRead(const char* p, std::size_t n, std::string& out) {
  out.resize(n);
#ifdef _WIN32
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), p, out.data(), n, &got) && got == n;
#else
  static int fds[2] = {-1, -1};
  static const bool open = pipe(fds) == 0;
  if (!open)
    return false;
  for (std::size_t done = 0; done < n;) {
    const ssize_t w = write(fds[1], p + done, std::min<std::size_t>(n - done, 4096));
    if (w <= 0 || read(fds[0], out.data() + done, static_cast<std::size_t>(w)) != w)
      return false;
    done += static_cast<std::size_t>(w);
  }
  return true;
#endif
}

// With the mutex held: whether the host's string still reads as it did.
inline bool check(const Record& r, const char* when) {
  State& s = state();
  ++s.checked;
  const std::size_t n = r.copy.size() + (r.bytes ? 0 : 1);  // a string's NUL too
  std::string now;
  const bool readable = safeRead(r.text, n, now);
  if (readable && std::memcmp(now.data(), r.copy.c_str(), n) == 0)
    return true;
  ++s.violations;
  const std::string got =
      readable ? "it read \"" +
                     printable(now.data(), r.bytes ? n : strnlen(now.data(), n)) + "\""
               : std::string("it could not be read");
  openfx::Logger::error(
      "string audit ({}): the host returned \"{}\" for {} in {}, but {} {}",
      stringRuleName(s.rule), printable(r.copy.data(), r.copy.size()), describe(r),
      r.action, when, got);
  return false;
}

// With the mutex held: checks and stops recording the frame's strings that
// pred selects, and returns how many failed.
template <class Pred>
std::size_t checkAndDrop(Frame& frame, const char* when, Pred pred) {
  auto keep = std::stable_partition(frame.records.begin(), frame.records.end(),
                                    [&](const Record& r) { return !pred(r); });
  std::size_t failed = 0;
  for (auto it = keep; it != frame.records.end(); ++it)
    failed += check(*it, when) ? 0 : 1;
  frame.records.erase(keep, frame.records.end());
  return failed;
}

// Around each entry call.
class EntryCall {
 public:
  explicit EntryCall(const char* action) : records_(thread()) {
    std::lock_guard lock(state().mutex);
    records_.frames.push_back({action ? action : "?", {}});
  }
  ~EntryCall() {
    try {
      finish();
    } catch (...) {
      openfx::logCurrentException("string audit: ending an entry call");
    }
  }
  EntryCall(const EntryCall&) = delete;
  EntryCall& operator=(const EntryCall&) = delete;

  // Checks the entry call's remaining strings as it returns, and gives the
  // number of strings found to have expired early during it.
  std::size_t finish() {
    if (finished_)
      return 0;
    finished_ = true;
    std::lock_guard lock(state().mutex);
    Frame& frame = records_.frames.back();
    const std::string when = "when " + frame.action + " returned";
    const std::size_t violations =
        frame.violations +
        checkAndDrop(frame, when.c_str(), [](const Record&) { return true; });
    records_.frames.pop_back();
    return violations;
  }

 private:
  ThreadRecords& records_;
  bool finished_ = false;
};

// Before each suite call that returns strings, which reads the given value at
// count indices from first: the strings whose lifetime that call ends.
inline void beginRead(const void* owner, std::string_view name, int first, int count,
                      double time) {
  auto& frames = thread().frames;
  std::lock_guard lock(state().mutex);
  const StringRule rule = state().rule;
  const auto all = [](const Record&) { return true; };
  std::size_t failed = 0;
  if (rule == StringRule::NextCall)
    for (auto& frame : frames)
      failed += checkAndDrop(frame, "at the next string-returning call", all);
  else if (frames.size() == 1)
    failed += checkAndDrop(frames.front(), "at the next string-returning call", all);
  if (rule == StringRule::SameValue)
    for (auto& frame : frames)
      failed +=
          checkAndDrop(frame, "when the plugin read it again", [&](const Record& r) {
            return r.owner == owner && r.name == name && r.index >= first &&
                   r.index < first + count && r.time == time;
          });
  frames.back().violations += failed;
}

inline constexpr std::size_t kKeepReleased = 16384;

// With the mutex held: whether s is, or points into, a string the plugin
// passed the host.
inline bool released(const char* s) {
  const auto& at = state().releasedAt;
  auto it = at.upper_bound(s);
  return it != at.begin() && std::prev(it)->second->contains(s);
}

// A string the host returned, or with a length the data of a bytes value:
// checked for being one the plugin passed it, and recorded for checking when
// its lifetime ends.
inline void record(const char* text, const void* owner, std::string_view name, int index,
                   double time, std::optional<std::size_t> length = std::nullopt) {
  if (!text)
    return;
  auto& frames = thread().frames;
  std::lock_guard lock(state().mutex);
  State& s = state();
  if (released(text)) {  // reading it would fault
    ++s.keptPointers;
    Record r{text, {}, owner, std::string(name), index, time, frames.back().action};
    openfx::Logger::error(
        "string audit: the host returned the string the plugin passed it for {} in {}, "
        "rather than its own copy",
        describe(r), r.action);
    return;
  }
  frames.back().records.push_back({text, length ? std::string(text, *length) : text,
                                   owner, std::string(name), index, time,
                                   frames.back().action, length.has_value()});
}

// The plugin changed a value: its strings are not checked further, on any
// thread. An empty name is every property; a negative index every index.
inline void changed(const void* owner, std::string_view name = {}, int index = -1) {
  std::lock_guard lock(state().mutex);
  for (ThreadRecords* t : state().threadRecords)
    for (auto& frame : t->frames)
      std::erase_if(frame.records, [&](const Record& r) {
        return r.owner == owner && (name.empty() || r.name == name) &&
               (index < 0 || r.index == index);
      });
}

// The plugin's string values on their way into one suite call: copies, made
// inaccessible when the call returns.
class Lent {
 public:
  Lent() = default;
  Lent(const Lent&) = delete;
  Lent& operator=(const Lent&) = delete;
  ~Lent() {
    try {
      std::lock_guard lock(state().mutex);
      State& s = state();
      for (auto& text : copies_) {
        text.revoke();
        s.released.push_back(std::move(text));
        s.releasedAt.emplace(s.released.back().c_str(), &s.released.back());
      }
      while (s.released.size() > kKeepReleased) {
        s.releasedAt.erase(s.released.front().c_str());
        s.released.pop_front();
      }
    } catch (...) {  // the copies are unmapped with copies_ instead
      openfx::logCurrentException("string audit: releasing the plugin's strings");
    }
  }

  const char* operator()(const char* s) { return s ? (*this)(std::string_view(s)) : s; }
  const char* operator()(std::string_view s) {
    copies_.emplace_back(s);
    return copies_.back().c_str();
  }

 private:
  std::vector<openfx::GuardedText> copies_;
};

// --- Entry points --------------------------------------------------------

inline constexpr std::size_t kSlots = 32;

// The plugin's entry points the audit stands in for, each slot with its own
// trampoline, since a C entry point carries no context.
struct Entries {
  std::array<OfxPluginEntryPoint*, kSlots> real{};
  std::size_t used = 0;
};
inline Entries& entries() {
  static Entries e;
  return e;
}

template <std::size_t I>
OfxStatus entryTrampoline(const char* action, const void* handle, OfxPropertySetHandle in,
                          OfxPropertySetHandle out) {
  EntryCall call(action);
  OfxStatus status = entries().real[I](action, handle, in, out);
  if (call.finish() && state().failOnViolation &&
      (status == kOfxStatOK || status == kOfxStatReplyDefault))
    status = kOfxStatFailed;
  if (action && std::strcmp(action, kOfxActionUnload) == 0) {
    std::lock_guard lock(state().mutex);
    const State& s = state();
    openfx::Logger::info(
        "string audit ({}): {} strings checked, {} expired early, {} kept by pointer",
        stringRuleName(s.rule), s.checked, s.violations, s.keptPointers);
  }
  return status;
}

template <std::size_t... I>
constexpr std::array<OfxPluginEntryPoint*, kSlots> entryTrampolines(
    std::index_sequence<I...>) {
  return {&entryTrampoline<I>...};
}

// The audited stand-in for an entry point, the same one for the same entry
// point; the entry point itself once every slot is taken.
inline OfxPluginEntryPoint* wrapEntry(OfxPluginEntryPoint* real) {
  static constexpr auto kTrampolines =
      entryTrampolines(std::make_index_sequence<kSlots>());
  if (!real)
    return real;
  std::lock_guard lock(state().mutex);
  Entries& e = entries();
  for (std::size_t i = 0; i < kSlots; ++i) {
    if (kTrampolines[i] == real)
      return real;
    if (i < e.used && e.real[i] == real)
      return kTrampolines[i];
  }
  if (e.used == kSlots) {
    openfx::Logger::warn("string audit: more than {} entry points; not auditing another",
                         kSlots);
    return real;
  }
  e.real[e.used] = real;
  return kTrampolines[e.used++];
}

// --- Property suite ------------------------------------------------------

inline OfxStatus propSetString(OfxPropertySetHandle h, const char* name, int index,
                               const char* value) {
  changed(h, name, index);
  Lent lend;
  return state().props->propSetString(h, name, index, lend(value));
}

inline OfxStatus propSetStringN(OfxPropertySetHandle h, const char* name, int count,
                                const char* const* values) {
  changed(h, name);
  Lent lend;
  std::vector<const char*> copies(values ? count : 0);
  for (std::size_t i = 0; i < copies.size(); ++i) copies[i] = lend(values[i]);
  return state().props->propSetStringN(h, name, count, values ? copies.data() : values);
}

inline OfxStatus propSetPointer(OfxPropertySetHandle h, const char* name, int index,
                                void* value) {
  if (name && (std::strcmp(name, kOfxImageEffectPluginPropOverlayInteractV1) == 0 ||
               std::strcmp(name, kOfxImageEffectPluginPropOverlayInteractV2) == 0 ||
               std::strcmp(name, kOfxParamPropInteractV1) == 0))
    value =
        reinterpret_cast<void*>(wrapEntry(reinterpret_cast<OfxPluginEntryPoint*>(value)));
  return state().props->propSetPointer(h, name, index, value);
}

inline OfxStatus propGetString(OfxPropertySetHandle h, const char* name, int index,
                               char** value) {
  beginRead(h, name ? name : "", index, 1, 0);
  const OfxStatus s = state().props->propGetString(h, name, index, value);
  if (s == kOfxStatOK && value && name)
    record(*value, h, name, index, 0);
  return s;
}

inline OfxStatus propGetStringN(OfxPropertySetHandle h, const char* name, int count,
                                char** values) {
  beginRead(h, name ? name : "", 0, count, 0);
  const OfxStatus s = state().props->propGetStringN(h, name, count, values);
  if (s == kOfxStatOK && values && name)
    for (int i = 0; i < count; ++i) record(values[i], h, name, i, 0);
  return s;
}

inline OfxStatus propReset(OfxPropertySetHandle h, const char* name) {
  if (name)
    changed(h, name);
  return state().props->propReset(h, name);
}

// --- Parameter suite -----------------------------------------------------

inline OfxStatus paramDefine(OfxParamSetHandle set, const char* type, const char* name,
                             OfxPropertySetHandle* props) {
  if (type && name) {
    std::lock_guard lock(state().mutex);
    state().paramTypes.insert_or_assign(name, type);
  }
  Lent lend;
  return state().params->paramDefine(set, type, lend(name), props);
}

inline OfxStatus paramGetHandle(OfxParamSetHandle set, const char* name,
                                OfxParamHandle* param, OfxPropertySetHandle* props) {
  const OfxStatus s = state().params->paramGetHandle(set, name, param, props);
  if (s == kOfxStatOK && param && name) {
    std::lock_guard lock(state().mutex);
    auto type = state().paramTypes.find(name);
    state().paramHandles.insert_or_assign(
        *param, paramInfo(type == state().paramTypes.end() ? "" : type->second, name));
  }
  return s;
}

inline std::optional<ParamInfo> lookUp(OfxParamHandle param) {
  std::lock_guard lock(state().mutex);
  auto it = state().paramHandles.find(param);
  if (it == state().paramHandles.end() || it->second.kind == ParamInfo::Kind::None)
    return std::nullopt;
  return it->second;
}

// Calls f with the value pointers in args, as many as the parameter has.
template <class T, class F>
OfxStatus withPointers(int arity, va_list args, F f) {
  std::array<T*, 4> p{};
  for (int i = 0; i < arity && i < 4; ++i) p[i] = va_arg(args, T*);
  switch (arity) {
    case 1:
      return f(p[0]);
    case 2:
      return f(p[0], p[1]);
    case 3:
      return f(p[0], p[1], p[2]);
    default:
      return f(p[0], p[1], p[2], p[3]);
  }
}

// Calls f with the values in args, as many as the parameter has.
template <class T, class F>
OfxStatus withValues(int arity, va_list args, F f) {
  std::array<T, 4> v{};
  for (int i = 0; i < arity && i < 4; ++i) v[i] = va_arg(args, T);
  switch (arity) {
    case 1:
      return f(v[0]);
    case 2:
      return f(v[0], v[1]);
    case 3:
      return f(v[0], v[1], v[2]);
    default:
      return f(v[0], v[1], v[2], v[3]);
  }
}

// paramGetValue(AtTime): get calls the host with the decoded pointers.
template <class Get>
OfxStatus getValue(OfxParamHandle param, double time, va_list args, Get get) {
  auto info = lookUp(param);
  if (!info) {
    openfx::Logger::error(
        "string audit: cannot forward a value read of a parameter it "
        "did not see defined");
    return kOfxStatErrUnsupported;
  }
  switch (info->kind) {
    case ParamInfo::Kind::Int:
      return withPointers<int>(info->arity, args, get);
    case ParamInfo::Kind::Double:
      return withPointers<double>(info->arity, args, get);
    case ParamInfo::Kind::String: {
      char** out = va_arg(args, char**);
      beginRead(param, {}, 0, 1, time);
      const OfxStatus s = get(out);
      if (s == kOfxStatOK && out)
        record(*out, param, {}, 0, time);
      return s;
    }
    case ParamInfo::Kind::Bytes: {
      auto* out = va_arg(args, OfxBytes*);
      beginRead(param, {}, 0, 1, time);
      const OfxStatus s = get(out);
      if (s == kOfxStatOK && out)
        record(reinterpret_cast<const char*>(out->data), param, {}, 0, time, out->length);
      return s;
    }
    case ParamInfo::Kind::None:
      break;
  }
  return kOfxStatErrUnsupported;
}

// paramSetValue(AtTime): set calls the host with the decoded values.
template <class Set>
OfxStatus setValue(OfxParamHandle param, va_list args, Set set) {
  auto info = lookUp(param);
  if (!info) {
    openfx::Logger::error(
        "string audit: cannot forward a value write to a parameter it "
        "did not see defined");
    return kOfxStatErrUnsupported;
  }
  changed(param);
  switch (info->kind) {
    case ParamInfo::Kind::Int:
      return withValues<int>(info->arity, args, set);
    case ParamInfo::Kind::Double:
      return withValues<double>(info->arity, args, set);
    case ParamInfo::Kind::String: {
      Lent lend;
      return set(lend(va_arg(args, const char*)));
    }
    case ParamInfo::Kind::Bytes: {
      const auto* in = va_arg(args, const OfxBytes*);
      if (!in || !in->data)
        return set(in);
      Lent lend;
      const OfxBytes copy{reinterpret_cast<const unsigned char*>(lend(std::string_view(
                              reinterpret_cast<const char*>(in->data), in->length))),
                          in->length};
      return set(&copy);
    }
    case ParamInfo::Kind::None:
      break;
  }
  return kOfxStatErrUnsupported;
}

inline OfxStatus paramGetValue(OfxParamHandle param, ...) {
  va_list args;
  va_start(args, param);
  const OfxStatus s = getValue(param, kNow, args, [&](auto... out) {
    return state().params->paramGetValue(param, out...);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramGetValueAtTime(OfxParamHandle param, OfxTime time, ...) {
  va_list args;
  va_start(args, time);
  const OfxStatus s = getValue(param, time, args, [&](auto... out) {
    return state().params->paramGetValueAtTime(param, time, out...);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramSetValue(OfxParamHandle param, ...) {
  va_list args;
  va_start(args, param);
  const OfxStatus s = setValue(param, args, [&](auto... values) {
    return state().params->paramSetValue(param, values...);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramSetValueAtTime(OfxParamHandle param, OfxTime time, ...) {
  va_list args;
  va_start(args, time);
  const OfxStatus s = setValue(param, args, [&](auto... values) {
    return state().params->paramSetValueAtTime(param, time, values...);
  });
  va_end(args);
  return s;
}

inline OfxStatus paramDeleteKey(OfxParamHandle param, OfxTime time) {
  changed(param);
  return state().params->paramDeleteKey(param, time);
}

inline OfxStatus paramDeleteAllKeys(OfxParamHandle param) {
  changed(param);
  return state().params->paramDeleteAllKeys(param);
}

inline OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime offset,
                           const OfxRangeD* range) {
  changed(to);
  return state().params->paramCopy(to, from, offset, range);
}

// --- Image effect and multithread suites ---------------------------------

inline OfxStatus clipDefine(OfxImageEffectHandle effect, const char* name,
                            OfxPropertySetHandle* props) {
  Lent lend;
  return state().effects->clipDefine(effect, lend(name), props);
}

// An image's strings may go with it: releasing it ends their lifetime.
inline OfxStatus clipReleaseImage(OfxPropertySetHandle image) {
  changed(image);
  return state().effects->clipReleaseImage(image);
}

struct ThreadCall {
  OfxThreadFunctionV1* func;
  void* arg;
};

inline void threadTrampoline(unsigned int index, unsigned int count, void* arg) {
  auto* call = static_cast<ThreadCall*>(arg);
  EntryCall entry("multithread thread function");
  call->func(index, count, call->arg);
}

inline OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void* arg) {
  if (!func)
    return state().threads->multiThread(func, nThreads, arg);
  ThreadCall call{func, arg};
  return state().threads->multiThread(threadTrampoline, nThreads, &call);
}

// --- The host --------------------------------------------------------------

inline const void* fetchSuite(OfxPropertySetHandle host, const char* name, int version) {
  State& s = state();
  const void* suite = s.realHost->fetchSuite(host, name, version);
  if (!suite || version != 1)
    return suite;
  std::lock_guard lock(s.mutex);
  if (std::strcmp(name, kOfxPropertySuite) == 0) {
    s.props = static_cast<const OfxPropertySuiteV1*>(suite);
    s.auditedProps = *s.props;
    s.auditedProps.propSetString = audit::propSetString;
    s.auditedProps.propSetStringN = audit::propSetStringN;
    s.auditedProps.propSetPointer = audit::propSetPointer;
    s.auditedProps.propGetString = audit::propGetString;
    s.auditedProps.propGetStringN = audit::propGetStringN;
    s.auditedProps.propReset = audit::propReset;
    return &s.auditedProps;
  }
  if (std::strcmp(name, kOfxParameterSuite) == 0) {
    s.params = static_cast<const OfxParameterSuiteV1*>(suite);
    s.auditedParams = *s.params;
    s.auditedParams.paramDefine = audit::paramDefine;
    s.auditedParams.paramGetHandle = audit::paramGetHandle;
    s.auditedParams.paramGetValue = audit::paramGetValue;
    s.auditedParams.paramGetValueAtTime = audit::paramGetValueAtTime;
    s.auditedParams.paramSetValue = audit::paramSetValue;
    s.auditedParams.paramSetValueAtTime = audit::paramSetValueAtTime;
    s.auditedParams.paramDeleteKey = audit::paramDeleteKey;
    s.auditedParams.paramDeleteAllKeys = audit::paramDeleteAllKeys;
    s.auditedParams.paramCopy = audit::paramCopy;
    return &s.auditedParams;
  }
  if (std::strcmp(name, kOfxImageEffectSuite) == 0) {
    s.effects = static_cast<const OfxImageEffectSuiteV1*>(suite);
    s.auditedEffects = *s.effects;
    s.auditedEffects.clipDefine = audit::clipDefine;
    s.auditedEffects.clipReleaseImage = audit::clipReleaseImage;
    return &s.auditedEffects;
  }
  if (std::strcmp(name, kOfxMultiThreadSuite) == 0) {
    s.threads = static_cast<const OfxMultiThreadSuiteV1*>(suite);
    s.auditedThreads = *s.threads;
    s.auditedThreads.multiThread = audit::multiThread;
    return &s.auditedThreads;
  }
  return suite;
}

// The plugins the audit wraps, each with its own setHost.
struct Plugins {
  std::array<OfxPlugin, kSlots> audited{};
  std::array<void (*)(OfxHost*), kSlots> realSetHost{};
  std::size_t used = 0;
};
inline Plugins& plugins() {
  static Plugins p;
  return p;
}

template <std::size_t I>
void setHostTrampoline(OfxHost* host) {
  {
    std::lock_guard lock(state().mutex);
    State& s = state();
    s.realHost = host;
    if (host) {
      s.host = *host;
      s.host.fetchSuite = audit::fetchSuite;
    }
  }
  EntryCall call("setHost");
  plugins().realSetHost[I](host ? &state().host : host);
}

template <std::size_t... I>
constexpr std::array<void (*)(OfxHost*), kSlots> setHostTrampolines(
    std::index_sequence<I...>) {
  return {&setHostTrampoline<I>...};
}

}  // namespace audit

// The plugin with its string traffic audited under rule; the plugin itself,
// with a warning, once kSlots plugins are wrapped. The same plugin gives the
// same wrapper. With failOnViolation, an action during which the audit found a
// string expired early returns kOfxStatFailed, if it had succeeded, so the
// host sees the finding as well as the log.
inline OfxPlugin* auditStrings(OfxPlugin* plugin, StringRule rule,
                               bool failOnViolation = false) {
  using namespace audit;
  static constexpr auto kSetHosts =
      setHostTrampolines(std::make_index_sequence<kSlots>());
  if (!plugin)
    return plugin;
  {
    std::lock_guard lock(state().mutex);
    state().rule = rule;
    state().failOnViolation = failOnViolation;
    Plugins& p = plugins();
    for (std::size_t i = 0; i < p.used; ++i)
      if (p.realSetHost[i] == plugin->setHost)
        return &p.audited[i];
    if (p.used == kSlots) {
      openfx::Logger::warn("string audit: more than {} plugins; not auditing {}", kSlots,
                           plugin->pluginIdentifier);
      return plugin;
    }
  }
  OfxPluginEntryPoint* entry = wrapEntry(plugin->mainEntry);
  std::lock_guard lock(state().mutex);
  Plugins& p = plugins();
  const std::size_t i = p.used++;
  p.realSetHost[i] = plugin->setHost;
  p.audited[i] = *plugin;
  p.audited[i].setHost = kSetHosts[i];
  p.audited[i].mainEntry = entry;
  return &p.audited[i];
}

}  // namespace ofxtest
