// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrappers over OfxMultiThreadSuiteV1: the CPU count, the SMP
// entry point as a callable, and an RAII mutex.

#include <ofxCore.h>
#include <ofxMultiThread.h>

#include <exception>
#include <functional>
#include <mutex>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxSuites.h"

namespace openfx::plugin {

namespace detail {

inline const OfxMultiThreadSuiteV1* requireThreadSuite(const SuiteContainer& suites) {
  const auto* suite = suites.get<OfxMultiThreadSuiteV1>();
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxMultiThreadSuite);
  return suite;
}

// What multiThread hands to the host as its customArg: the callable, plus the
// first exception any worker threw, since one must not cross the C boundary.
struct ThreadTask {
  const std::function<void(unsigned, unsigned)>* func;
  std::mutex mutex;
  std::exception_ptr error;
};

inline void threadTrampoline(unsigned threadIndex, unsigned threadMax, void* customArg) {
  auto* task = static_cast<ThreadTask*>(customArg);
  try {
    (*task->func)(threadIndex, threadMax);
  } catch (...) {
    const std::lock_guard<std::mutex> lock(task->mutex);
    if (!task->error)
      task->error = std::current_exception();
  }
}

}  // namespace detail

// The number of CPUs the host will let this plugin use.
inline unsigned numCPUs(const SuiteContainer& suites) {
  unsigned n = 1;
  const auto* suite = detail::requireThreadSuite(suites);
  OfxStatus status = suite->multiThreadNumCPUs(&n);
  if (status != kOfxStatOK)
    throw OfxException(status, "multiThreadNumCPUs");
  return n;
}

// The index of the calling thread within a multiThread call, or 0 outside one.
inline unsigned threadIndex(const SuiteContainer& suites) {
  unsigned index = 0;
  const auto* suite = detail::requireThreadSuite(suites);
  OfxStatus status = suite->multiThreadIndex(&index);
  if (status != kOfxStatOK)
    throw OfxException(status, "multiThreadIndex");
  return index;
}

// True if the caller is one of the threads the host spawned for multiThread.
inline bool isSpawnedThread(const SuiteContainer& suites) {
  return detail::requireThreadSuite(suites)->multiThreadIsSpawnedThread() != 0;
}

// Run `func(index, count)` on `nThreads` threads and wait for them all.
// nThreads == 0 means one thread per CPU. An exception from any thread is
// rethrown here once they have all finished.
inline void multiThread(const SuiteContainer& suites, unsigned nThreads,
                        const std::function<void(unsigned index, unsigned count)>& func) {
  const auto* suite = detail::requireThreadSuite(suites);
  if (nThreads == 0)
    nThreads = numCPUs(suites);

  detail::ThreadTask task;
  task.func = &func;
  OfxStatus status = suite->multiThread(detail::threadTrampoline, nThreads, &task);
  if (task.error)
    std::rethrow_exception(task.error);
  if (status != kOfxStatOK)
    throw OfxException(status, "multiThread");
}

// A host mutex, usable with std::lock_guard.
class Mutex {
 public:
  // `lockCount` is the number of times the mutex starts out locked.
  explicit Mutex(const SuiteContainer& suites, int lockCount = 0)
      : mThreadSuite(detail::requireThreadSuite(suites)) {
    OfxStatus status = mThreadSuite->mutexCreate(&mMutex, lockCount);
    if (status != kOfxStatOK)
      throw OfxException(status, "mutexCreate");
  }

  ~Mutex() {
    if (mMutex)
      mThreadSuite->mutexDestroy(mMutex);
  }

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  Mutex(Mutex&& other) noexcept : mThreadSuite(other.mThreadSuite), mMutex(other.mMutex) {
    other.mMutex = nullptr;
  }
  Mutex& operator=(Mutex&&) = delete;

  void lock() {
    OfxStatus status = mThreadSuite->mutexLock(mMutex);
    if (status != kOfxStatOK)
      throw OfxException(status, "mutexLock");
  }

  void unlock() {
    OfxStatus status = mThreadSuite->mutexUnLock(mMutex);
    if (status != kOfxStatOK)
      throw OfxException(status, "mutexUnLock");
  }

  // True if the mutex was free and is now held by this thread.
  bool tryLock() { return mThreadSuite->mutexTryLock(mMutex) == kOfxStatOK; }

  OfxMutexHandle handle() const { return mMutex; }

 private:
  const OfxMultiThreadSuiteV1* mThreadSuite;
  OfxMutexHandle mMutex{};
};

}  // namespace openfx::plugin
