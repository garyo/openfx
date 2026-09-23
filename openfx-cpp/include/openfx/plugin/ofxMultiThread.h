// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrappers over OfxMultiThreadSuiteV1: the CPU count, the SMP
// entry point as a callable, and an RAII mutex.

#include <ofxCore.h>
#include <ofxMultiThread.h>

#include <atomic>
#include <exception>
#include <functional>
#include <utility>

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
// The first worker to fail claims the flag and alone writes the error, which
// is read only once the host's multiThread has joined every worker.
struct ThreadTask {
  const std::function<void(unsigned, unsigned)>* func;
  std::atomic<bool> failed{false};
  std::exception_ptr error;
};

// The thread function the host calls, from C. Everything in the handler is
// noexcept, so nothing it does can throw past it either.
inline void threadTrampoline(unsigned threadIndex, unsigned threadMax,
                             void* customArg) noexcept {
  auto* task = static_cast<ThreadTask*>(customArg);
  try {
    (*task->func)(threadIndex, threadMax);
  } catch (...) {
    if (!task->failed.exchange(true))
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

// A host mutex, destroyed with mutexDestroy when the Mutex goes, and usable
// with std::lock_guard, std::unique_lock and std::scoped_lock. It can adopt a
// mutex C code created, and give its handle back with release().
class Mutex {
 public:
  // Create a mutex with mutexCreate. `lockCount` is the number of times the
  // mutex starts out locked.
  explicit Mutex(const SuiteContainer& suites, int lockCount = 0)
      : threadSuite_(detail::requireThreadSuite(suites)) {
    OfxStatus status = threadSuite_->mutexCreate(&mutex_, lockCount);
    if (status != kOfxStatOK)
      throw OfxException(status, "mutexCreate");
  }

  // Adopt a mutex C code created with mutexCreate: the Mutex destroys it from
  // now on.
  Mutex(OfxMutexHandle mutex, const OfxMultiThreadSuiteV1* threadSuite) noexcept
      : threadSuite_(threadSuite), mutex_(mutex) {}

  ~Mutex() { reset(); }

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  Mutex(Mutex&& other) noexcept
      : threadSuite_(other.threadSuite_), mutex_(std::exchange(other.mutex_, nullptr)) {}

  Mutex& operator=(Mutex&& other) noexcept {
    if (this != &other) {
      reset();
      threadSuite_ = other.threadSuite_;
      mutex_ = std::exchange(other.mutex_, nullptr);
    }
    return *this;
  }

  // Destroy the mutex now, leaving this Mutex empty.
  void reset() noexcept {
    if (mutex_)
      threadSuite_->mutexDestroy(std::exchange(mutex_, nullptr));
  }

  // Hand the mutex to C code, which must destroy it with mutexDestroy, leaving
  // this Mutex empty.
  [[nodiscard]] OfxMutexHandle release() noexcept {
    return std::exchange(mutex_, nullptr);
  }

  void lock() {
    OfxStatus status = threadSuite_->mutexLock(mutex_);
    if (status != kOfxStatOK)
      throw OfxException(status, "mutexLock");
  }

  // Never throws, since the destructors of std::lock_guard and the like call
  // it: a failure to unlock is logged instead.
  void unlock() noexcept {
    try {
      OfxStatus status = threadSuite_->mutexUnLock(mutex_);
      if (status != kOfxStatOK)
        throw OfxException(status, "mutexUnLock");
    } catch (...) {
      logCurrentException("Mutex::unlock");
    }
  }

  // True if the mutex was free and is now held by this thread. try_lock is
  // the name std::unique_lock and std::lock call; tryLock matches the rest of
  // these wrappers.
  bool try_lock() { return threadSuite_->mutexTryLock(mutex_) == kOfxStatOK; }
  bool tryLock() { return try_lock(); }

  OfxMutexHandle handle() const { return mutex_; }

 private:
  const OfxMultiThreadSuiteV1* threadSuite_;
  OfxMutexHandle mutex_{};
};

}  // namespace openfx::plugin
