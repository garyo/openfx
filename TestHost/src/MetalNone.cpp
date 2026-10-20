// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
// Metal.h where there is no Metal: nothing is available.
#if !defined(__APPLE__)
#include "Metal.h"

#include <stdexcept>

namespace testhost::metal {

bool available() { return false; }
void* commandQueue() { throw std::logic_error("no Metal"); }
void* upload(const std::byte*, size_t) { throw std::logic_error("no Metal"); }
void download(void*, size_t, std::byte*, size_t) { throw std::logic_error("no Metal"); }
void release(void*) {}
void finish() {}

}  // namespace testhost::metal
#endif
