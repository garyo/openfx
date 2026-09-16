// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "Suites.h"

#include <ofxImageEffect.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProgress.h>
#include <ofxProperty.h>
#include <ofxTimeLine.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Effect.h"
#include "Log.h"
#include "PropertySet.h"

namespace testhost::suites {

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

namespace {

OfxStatus memoryAlloc(void*, size_t nBytes, void** data) {
  *data = std::malloc(nBytes ? nBytes : 1);
  return *data ? kOfxStatOK : kOfxStatErrMemory;
}

OfxStatus memoryFree(void* data) {
  std::free(data);
  return kOfxStatOK;
}

const OfxMemorySuiteV1 kMemorySuite = {memoryAlloc, memoryFree};

// ---------------------------------------------------------------------------
// Multithreading: real threads, one recursive mutex per OfxMutexHandle.
// ---------------------------------------------------------------------------

thread_local unsigned int tThreadIndex = 0;
thread_local bool tSpawned = false;

OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void* arg) {
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

OfxStatus multiThreadNumCPUs(unsigned int* n) {
  *n = std::max(1u, std::thread::hardware_concurrency());
  return kOfxStatOK;
}

OfxStatus multiThreadIndex(unsigned int* index) {
  *index = tThreadIndex;
  return kOfxStatOK;
}

int multiThreadIsSpawnedThread() { return tSpawned; }

std::recursive_mutex* asMutex(OfxMutexHandle m) { return reinterpret_cast<std::recursive_mutex*>(m); }

OfxStatus mutexCreate(OfxMutexHandle* mutex, int lockCount) {
  auto* m = new std::recursive_mutex;
  for (int i = 0; i < lockCount; ++i) m->lock();
  *mutex = reinterpret_cast<OfxMutexHandle>(m);
  return kOfxStatOK;
}

OfxStatus mutexDestroy(OfxMutexHandle mutex) {
  delete asMutex(mutex);
  return kOfxStatOK;
}

OfxStatus mutexLock(OfxMutexHandle mutex) {
  asMutex(mutex)->lock();
  return kOfxStatOK;
}

OfxStatus mutexUnLock(OfxMutexHandle mutex) {
  asMutex(mutex)->unlock();
  return kOfxStatOK;
}

OfxStatus mutexTryLock(OfxMutexHandle mutex) { return asMutex(mutex)->try_lock() ? kOfxStatOK : kOfxStatFailed; }

const OfxMultiThreadSuiteV1 kMultiThreadSuite = {
    multiThread, multiThreadNumCPUs, multiThreadIndex, multiThreadIsSpawnedThread,
    mutexCreate,  mutexDestroy,      mutexLock,        mutexUnLock,
    mutexTryLock,
};

// ---------------------------------------------------------------------------
// Messages and progress: written to the log.
// ---------------------------------------------------------------------------

std::string vformat(const char* fmt, va_list args) {
  va_list copy;
  va_copy(copy, args);
  int n = std::vsnprintf(nullptr, 0, fmt, copy);
  va_end(copy);
  std::string s(n > 0 ? n : 0, '\0');
  std::vsnprintf(s.data(), s.size() + 1, fmt, args);
  return s;
}

OfxStatus message(void* handle, const char* type, const char* id, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::string text = vformat(fmt ? fmt : "", args);
  va_end(args);
  log::info("plugin message [{}{}{}]: {}", type ? type : "", id ? " " : "", id ? id : "", text);
  (void)handle;
  return type && std::strcmp(type, kOfxMessageQuestion) == 0 ? kOfxStatReplyYes : kOfxStatOK;
}

OfxStatus setPersistentMessage(void* handle, const char* type, const char* id, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::string text = vformat(fmt ? fmt : "", args);
  va_end(args);
  log::info("plugin persistent message [{}{}{}]: {}", type ? type : "", id ? " " : "", id ? id : "", text);
  (void)handle;
  return kOfxStatOK;
}

OfxStatus clearPersistentMessage(void*) { return kOfxStatOK; }

const OfxMessageSuiteV1 kMessageSuiteV1 = {message};
const OfxMessageSuiteV2 kMessageSuiteV2 = {message, setPersistentMessage, clearPersistentMessage};

OfxStatus progressStartV1(void*, const char* label) {
  log::debug("progress start: {}", label ? label : "");
  return kOfxStatOK;
}
OfxStatus progressStartV2(void*, const char* label, const char*) {
  log::debug("progress start: {}", label ? label : "");
  return kOfxStatOK;
}
OfxStatus progressUpdate(void*, double) { return kOfxStatOK; }
OfxStatus progressEnd(void*) {
  log::debug("progress end");
  return kOfxStatOK;
}

const OfxProgressSuiteV1 kProgressSuiteV1 = {progressStartV1, progressUpdate, progressEnd};
const OfxProgressSuiteV2 kProgressSuiteV2 = {progressStartV2, progressUpdate, progressEnd};

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

Timeline gTimeline;

OfxStatus getTime(void*, double* time) {
  *time = gTimeline.current;
  return kOfxStatOK;
}
OfxStatus gotoTime(void*, double time) {
  gTimeline.current = time;
  return kOfxStatOK;
}
OfxStatus getTimeBounds(void*, double* first, double* last) {
  *first = gTimeline.first;
  *last = gTimeline.last;
  return kOfxStatOK;
}

const OfxTimeLineSuiteV1 kTimeLineSuite = {getTime, gotoTime, getTimeBounds};

}  // namespace

Timeline& timeline() { return gTimeline; }

const void* fetch(const char* name, int version) {
  std::string_view n = name ? name : "";
  if (n == kOfxPropertySuite && version == 1) return PropertySet::suite();
  if (n == kOfxImageEffectSuite && version == 1) return effectSuite();
  if (n == kOfxParameterSuite && version == 1) return paramSuite();
  if (n == kOfxMemorySuite && version == 1) return &kMemorySuite;
  if (n == kOfxMultiThreadSuite && version == 1) return &kMultiThreadSuite;
  if (n == kOfxMessageSuite && version == 1) return &kMessageSuiteV1;
  if (n == kOfxMessageSuite && version == 2) return &kMessageSuiteV2;
  if (n == kOfxProgressSuite && version == 1) return &kProgressSuiteV1;
  if (n == kOfxProgressSuite && version == 2) return &kProgressSuiteV2;
  if (n == kOfxTimeLineSuite && version == 1) return &kTimeLineSuite;
  log::debug("suite not provided: {} v{}", n, version);
  return nullptr;
}

}  // namespace testhost::suites
