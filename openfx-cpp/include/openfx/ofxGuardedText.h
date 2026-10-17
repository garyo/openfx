// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace openfx {

// A copy of a string in pages of its own, which revoke() makes inaccessible:
// from then on any read of it faults at once, at the instruction that made
// it. Test tools use it for strings whose lifetime has ended, so that code
// still holding one is stopped where it reads it, not left with whatever the
// memory holds by then. After revoke() the pages keep their addresses, but
// no memory, until the object is destroyed.
class GuardedText {
 public:
  // Throws std::bad_alloc if the system has no pages to give.
  explicit GuardedText(std::string_view text) : size_(roundUp(text.size() + 1)) {
#ifdef _WIN32
    data_ = static_cast<char*>(
        VirtualAlloc(nullptr, size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    void* p = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    data_ = p == MAP_FAILED ? nullptr : static_cast<char*>(p);
#endif
    if (!data_)
      throw std::bad_alloc();
    std::memcpy(data_, text.data(), text.size());
    data_[text.size()] = '\0';
  }
  ~GuardedText() {
    if (!data_)
      return;
#ifdef _WIN32
    VirtualFree(data_, 0, MEM_RELEASE);
#else
    munmap(data_, size_);
#endif
  }
  GuardedText(GuardedText&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {
  }
  GuardedText& operator=(GuardedText&& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    return *this;
  }
  GuardedText(const GuardedText&) = delete;
  GuardedText& operator=(const GuardedText&) = delete;

  const char* c_str() const { return data_; }
  bool contains(const char* p) const { return data_ && p >= data_ && p < data_ + size_; }

  // Gives the pages back to the system and leaves their addresses
  // inaccessible.
  void revoke() {
    if (!data_)
      return;
#ifdef _WIN32
    VirtualFree(data_, size_, MEM_DECOMMIT);
#else
    // A fresh mapping over the same addresses drops the contents with the
    // access, which mprotect alone would keep resident.
    if (mmap(data_, size_, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) ==
        MAP_FAILED)
      (void)mprotect(data_, size_, PROT_NONE);
#endif
  }

 private:
  static std::size_t pageSize() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
#else
    return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
  }
  static std::size_t roundUp(std::size_t n) {
    static const std::size_t page = pageSize();
    return (n + page - 1) / page * page;
  }

  char* data_ = nullptr;
  std::size_t size_ = 0;
};

}  // namespace openfx
