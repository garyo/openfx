// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxGuardedText.h"

namespace openfx::host {

// How long a string the host returns to a plugin stays valid. Issue #273
// proposes tightening the specification here, and until it settles a host can
// follow any of the proposals. Under the strict lifetimes every string is a
// copy in pages of its own (openfx::GuardedText) that expires as early as its
// rule allows: its pages are then made inaccessible, so a plugin that reads
// it after that faults at the read.
//
// Under each of them a string read from an image's property set also expires
// when the plugin releases the image.
//
// The entry calls are the host's calls into the plugin: an action, setHost,
// an interact's action, a multithread thread function. On a thread with no
// entry call in progress, such as one the plugin started itself, each strict
// lifetime ends at the next suite call on that thread that returns a string.
enum class StringLifetime {
  // A pointer into the host's own storage, valid until the value changes.
  Storage,
  // Until the innermost entry call on that thread returns, or the plugin
  // changes the value it came from, from any of its threads.
  EntryCall,
  // As EntryCall, and also until the plugin reads that same value again on
  // that thread.
  SameValue,
  // Until the next suite call on that thread that returns a string, or the
  // entry call returns.
  NextCall,
};

inline const char* stringLifetimeName(StringLifetime lifetime) {
  switch (lifetime) {
    case StringLifetime::Storage:
      return "storage";
    case StringLifetime::EntryCall:
      return "entry-call";
    case StringLifetime::SameValue:
      return "same-value";
    case StringLifetime::NextCall:
      return "next-call";
  }
  return "?";
}

inline std::optional<StringLifetime> stringLifetimeFromName(std::string_view name) {
  for (auto lifetime : {StringLifetime::Storage, StringLifetime::EntryCall,
                        StringLifetime::SameValue, StringLifetime::NextCall})
    if (name == stringLifetimeName(lifetime))
      return lifetime;
  return std::nullopt;
}

// The strings the host has returned to the plugin, per thread and per entry
// call, under the chosen lifetime. The property and parameter suites hand
// strings out through it; under StringLifetime::Storage it holds nothing.
class Strings {
 public:
  // What a string was read from: a property set or parameter, the property
  // (empty for a parameter's value), the index and, for a parameter, the time.
  struct Source {
    const void* owner;
    std::string_view name;
    int index;
    double time;
  };

  static void setLifetime(StringLifetime lifetime) { state().lifetime = lifetime; }
  static StringLifetime lifetime() { return state().lifetime; }
  static bool strict() { return lifetime() != StringLifetime::Storage; }

  // Around each entry call. When it ends, the strings returned during it expire.
  class EntryCall {
   public:
    EntryCall() : active_(strict()) {
      if (active_) {
        auto& t = thread();
        std::lock_guard lock(state().mutex);
        t.frames.emplace_back();
      }
    }
    ~EntryCall() {
      if (!active_)
        return;
      try {
        auto& frames = thread().frames;
        std::lock_guard lock(state().mutex);
        expire(frames.back());
        frames.pop_back();
      } catch (...) {
        logCurrentException("string lifetime: ending an entry call");
      }
    }
    EntryCall(const EntryCall&) = delete;
    EntryCall& operator=(const EntryCall&) = delete;

   private:
    bool active_;
  };

  // At the start of a suite call that returns strings, before its first
  // handOut(): where the lifetime ends at the next such call, it ends here.
  static void beginStringCall() {
    if (!strict())
      return;
    auto& frames = thread().frames;
    std::lock_guard lock(state().mutex);
    if (lifetime() == StringLifetime::NextCall)
      for (auto& frame : frames) expire(frame);
    else if (frames.size() == 1)
      expire(frames.front());
  }

  // A copy of value to return to the plugin, which expires as the lifetime has
  // it. Only under a strict lifetime.
  static const char* handOut(std::string_view value, const Source& from) {
    auto& frames = thread().frames;
    std::lock_guard lock(state().mutex);
    if (lifetime() == StringLifetime::SameValue)
      for (auto& frame : frames)
        expireIf(frame, [&](const Held& h) {
          return h.owner == from.owner && h.name == from.name && h.index == from.index &&
                 h.time == from.time;
        });
    Held h{GuardedText(value), from.owner, std::string(from.name), from.index, from.time};
    const char* text = h.text.c_str();
    frames.back().push_back(std::move(h));
    return text;
  }

  // The plugin changed a value: the strings read from it expire, on every
  // thread. An empty name is every property, a negative index every index;
  // for a parameter, every time.
  static void changed(const void* owner, std::string_view name = {}, int index = -1) {
    if (!strict())
      return;
    std::lock_guard lock(state().mutex);
    for (ThreadStrings* t : state().threads)
      for (auto& frame : t->frames)
        expireIf(frame, [&](const Held& h) {
          return h.owner == owner && (name.empty() || h.name == name) &&
                 (index < 0 || h.index == index);
        });
  }

  // Whether s is, or points into, a string that has expired, one of the most
  // recent kQuarantine of them. A plugin handing one back would have the host
  // read it, and fault.
  static bool expired(const char* s) {
    if (!s || !strict())
      return false;
    std::lock_guard lock(state().mutex);
    const auto& q = state().quarantined;
    auto it = q.upper_bound(s);
    if (it == q.begin())
      return false;
    return std::prev(it)->second->contains(s);
  }

 private:
  struct Held {
    GuardedText text;
    const void* owner;
    std::string name;
    int index;
    double time;
  };
  using Frame = std::vector<Held>;

  // One thread's frames: the first for when no entry call is in progress,
  // then one per entry call, innermost last.
  struct ThreadStrings {
    std::vector<Frame> frames = std::vector<Frame>(1);
    ThreadStrings() {
      std::lock_guard lock(state().mutex);
      state().threads.push_back(this);
    }
    ~ThreadStrings() {
      try {
        std::lock_guard lock(state().mutex);
        for (auto& frame : frames) expire(frame);
        auto& all = state().threads;
        all.erase(std::find(all.begin(), all.end(), this));
      } catch (...) {
        logCurrentException("string lifetime: ending a thread");
      }
    }
  };

  // How many expired strings keep their addresses, inaccessible, before
  // those are given back for reuse.
  static constexpr std::size_t kQuarantine = 16384;

  struct State {
    std::mutex mutex;
    std::atomic<StringLifetime> lifetime{StringLifetime::Storage};
    std::vector<ThreadStrings*> threads;
    std::deque<GuardedText> quarantine;
    std::map<const char*, const GuardedText*> quarantined;
  };

  static State& state() {
    static State s;
    return s;
  }

  static ThreadStrings& thread() {
    thread_local ThreadStrings t;
    return t;
  }

  // With the mutex held.
  template <class Pred>
  static void expireIf(Frame& frame, Pred pred) {
    auto keep = std::stable_partition(frame.begin(), frame.end(),
                                      [&](const Held& h) { return !pred(h); });
    for (auto it = keep; it != frame.end(); ++it) quarantine(std::move(*it));
    frame.erase(keep, frame.end());
  }
  static void expire(Frame& frame) noexcept {
    for (auto& h : frame) quarantine(std::move(h));
    frame.clear();
  }

  // Without room to keep it, the string is unmapped at once: inaccessible as
  // well, but its addresses may be reused sooner.
  static void quarantine(Held&& h) noexcept {
    State& s = state();
    h.text.revoke();
    try {
      s.quarantine.push_back(std::move(h.text));
      s.quarantined.emplace(s.quarantine.back().c_str(), &s.quarantine.back());
    } catch (...) {
      return;
    }
    if (s.quarantine.size() > kQuarantine) {
      s.quarantined.erase(s.quarantine.front().c_str());
      s.quarantine.pop_front();
    }
  }
};

}  // namespace openfx::host
