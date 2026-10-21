// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
// Cuda.h built without the CUDA headers: nothing is available.
#if !defined(OFX_TESTHOST_CUDA)
#include "Cuda.h"

#include <stdexcept>

namespace testhost::cuda {

void selectDevice(const std::string&) {}
bool available() { return false; }
std::string deviceName() { throw std::logic_error("no CUDA"); }
void* stream() { throw std::logic_error("no CUDA"); }
void* upload(const std::byte*, size_t) { throw std::logic_error("no CUDA"); }
void download(void*, size_t, std::byte*, size_t) { throw std::logic_error("no CUDA"); }
void release(void*) {}
void finish() {}

}  // namespace testhost::cuda
#endif
