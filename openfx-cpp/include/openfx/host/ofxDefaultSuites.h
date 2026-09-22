// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <ofxCore.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxProgress.h>
#include <ofxTimeLine.h>

#include "openfx/ofxLog.h"
#include "openfx/ofxSuites.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The generic suites every simple host provides: memory, multithreading,
// messages, progress and the timeline. A host that only needs the standard
// behaviour can register all of them with addDefaultSuites(); a host that
// wants different behaviour for one of them can register the rest and add
// its own suite under the same name and version.

namespace openfx::host {

// Frame range the timeline suite reports and the current time it holds.
struct Timeline {
  double first = 0;
  double last = 0;
  double current = 0;
};

// The single Timeline instance the timeline suite below reads and writes.
inline Timeline& timeline() {
  static Timeline instance;
  return instance;
}

namespace detail {

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

inline OfxStatus memoryAlloc(void*, size_t nBytes, void** data) {
  *data = std::malloc(nBytes ? nBytes : 1);
  return *data ? kOfxStatOK : kOfxStatErrMemory;
}

inline OfxStatus memoryFree(void* data) {
  std::free(data);
  return kOfxStatOK;
}

// ---------------------------------------------------------------------------
// Multithreading: real threads, one recursive mutex per OfxMutexHandle.
// ---------------------------------------------------------------------------

inline thread_local unsigned int tThreadIndex = 0;
inline thread_local bool tSpawned = false;

inline OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void* arg) {
  if (!func) return kOfxStatFailed;
  unsigned int hw = std::max(1u, std::thread::hardware_concurrency());
  unsigned int n = std::clamp(nThreads, 1u, hw);
  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < n; ++i) {
    threads.emplace_back([=] {
      tThreadIndex = i;
      tSpawned = true;
      func(i, n, arg);
    });
  }
  for (auto& t : threads) t.join();
  return kOfxStatOK;
}

inline OfxStatus multiThreadNumCPUs(unsigned int* n) {
  *n = std::max(1u, std::thread::hardware_concurrency());
  return kOfxStatOK;
}

inline OfxStatus multiThreadIndex(unsigned int* index) {
  *index = tThreadIndex;
  return kOfxStatOK;
}

inline int multiThreadIsSpawnedThread() { return tSpawned; }

inline std::recursive_mutex* asMutex(OfxMutexHandle m) {
  return reinterpret_cast<std::recursive_mutex*>(m);
}

inline OfxStatus mutexCreate(OfxMutexHandle* mutex, int lockCount) {
  auto* m = new std::recursive_mutex;
  for (int i = 0; i < lockCount; ++i) m->lock();
  *mutex = reinterpret_cast<OfxMutexHandle>(m);
  return kOfxStatOK;
}

inline OfxStatus mutexDestroy(OfxMutexHandle mutex) {
  delete asMutex(mutex);
  return kOfxStatOK;
}

inline OfxStatus mutexLock(OfxMutexHandle mutex) {
  asMutex(mutex)->lock();
  return kOfxStatOK;
}

inline OfxStatus mutexUnLock(OfxMutexHandle mutex) {
  asMutex(mutex)->unlock();
  return kOfxStatOK;
}

inline OfxStatus mutexTryLock(OfxMutexHandle mutex) {
  return asMutex(mutex)->try_lock() ? kOfxStatOK : kOfxStatFailed;
}

// ---------------------------------------------------------------------------
// Messages and progress: written to the log.
// ---------------------------------------------------------------------------

inline std::string vformat(const char* fmt, va_list args) {
  va_list copy;
  va_copy(copy, args);
  int n = std::vsnprintf(nullptr, 0, fmt, copy);
  va_end(copy);
  std::string s(n > 0 ? n : 0, '\0');
  std::vsnprintf(s.data(), s.size() + 1, fmt, args);
  return s;
}

inline OfxStatus message(void* handle, const char* type, const char* id, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::string text = vformat(fmt ? fmt : "", args);
  va_end(args);
  openfx::Logger::info("plugin message [{}{}{}]: {}", type ? type : "", id ? " " : "", id ? id : "", text);
  (void)handle;
  return type && std::strcmp(type, kOfxMessageQuestion) == 0 ? kOfxStatReplyYes : kOfxStatOK;
}

inline OfxStatus setPersistentMessage(void* handle, const char* type, const char* id, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::string text = vformat(fmt ? fmt : "", args);
  va_end(args);
  openfx::Logger::info("plugin persistent message [{}{}{}]: {}", type ? type : "", id ? " " : "",
                        id ? id : "", text);
  (void)handle;
  return kOfxStatOK;
}

inline OfxStatus clearPersistentMessage(void*) { return kOfxStatOK; }

inline OfxStatus progressStartV1(void*, const char* label) {
  openfx::Logger::debug("progress start: {}", label ? label : "");
  return kOfxStatOK;
}

inline OfxStatus progressStartV2(void*, const char* label, const char*) {
  openfx::Logger::debug("progress start: {}", label ? label : "");
  return kOfxStatOK;
}

inline OfxStatus progressUpdate(void*, double) { return kOfxStatOK; }

inline OfxStatus progressEnd(void*) {
  openfx::Logger::debug("progress end");
  return kOfxStatOK;
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

inline OfxStatus getTime(void*, double* time) {
  *time = timeline().current;
  return kOfxStatOK;
}

inline OfxStatus gotoTime(void*, double time) {
  timeline().current = time;
  return kOfxStatOK;
}

inline OfxStatus getTimeBounds(void*, double* first, double* last) {
  *first = timeline().first;
  *last = timeline().last;
  return kOfxStatOK;
}

}  // namespace detail

inline const OfxMemorySuiteV1* memorySuite() {
  static const OfxMemorySuiteV1 suite = {detail::memoryAlloc, detail::memoryFree};
  return &suite;
}

inline const OfxMultiThreadSuiteV1* multiThreadSuite() {
  static const OfxMultiThreadSuiteV1 suite = {
      detail::multiThread,       detail::multiThreadNumCPUs, detail::multiThreadIndex,
      detail::multiThreadIsSpawnedThread, detail::mutexCreate,        detail::mutexDestroy,
      detail::mutexLock,         detail::mutexUnLock,        detail::mutexTryLock,
  };
  return &suite;
}

inline const OfxMessageSuiteV1* messageSuiteV1() {
  static const OfxMessageSuiteV1 suite = {detail::message};
  return &suite;
}

inline const OfxMessageSuiteV2* messageSuiteV2() {
  static const OfxMessageSuiteV2 suite = {detail::message, detail::setPersistentMessage,
                                           detail::clearPersistentMessage};
  return &suite;
}

inline const OfxProgressSuiteV1* progressSuiteV1() {
  static const OfxProgressSuiteV1 suite = {detail::progressStartV1, detail::progressUpdate,
                                            detail::progressEnd};
  return &suite;
}

inline const OfxProgressSuiteV2* progressSuiteV2() {
  static const OfxProgressSuiteV2 suite = {detail::progressStartV2, detail::progressUpdate,
                                            detail::progressEnd};
  return &suite;
}

inline const OfxTimeLineSuiteV1* timeLineSuite() {
  static const OfxTimeLineSuiteV1 suite = {detail::getTime, detail::gotoTime, detail::getTimeBounds};
  return &suite;
}

// Registers all seven default suites in `suites` under their kOfx*Suite
// names and versions (message v1 and v2, progress v1 and v2).
inline void addDefaultSuites(SuiteContainer& suites) {
  suites.add(kOfxMemorySuite, 1, memorySuite());
  suites.add(kOfxMultiThreadSuite, 1, multiThreadSuite());
  suites.add(kOfxMessageSuite, 1, messageSuiteV1());
  suites.add(kOfxMessageSuite, 2, messageSuiteV2());
  suites.add(kOfxProgressSuite, 1, progressSuiteV1());
  suites.add(kOfxProgressSuite, 2, progressSuiteV2());
  suites.add(kOfxTimeLineSuite, 1, timeLineSuite());
}

}  // namespace openfx::host
