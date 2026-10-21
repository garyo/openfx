// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// CUDA rendering (OFX 1.5): the host's device and stream, and the device
// memory it hands a plugin in place of CPU pixel pointers. The driver is
// loaded when first needed, so the host runs where there is none; built
// without the CUDA headers, available() is false and nothing else may be
// called.
//
// Everything runs in the device's primary context, the one the CUDA runtime
// uses, as in a typical host built on the runtime API.

#include <cstddef>
#include <string>

namespace testhost::cuda {

// Picks the device to render with: an ordinal, or a substring of the
// device's name. Left unset, it's the device with the highest compute
// capability. Call before anything below.
void selectDevice(const std::string& spec);

// Whether this build and machine can render with CUDA.
bool available();

// The chosen device's name and compute capability, for messages.
std::string deviceName();

// The host's stream (a CUstream, which is also a cudaStream_t), made on
// first use.
void* stream();

// A new device allocation holding a copy of `length` bytes; the pointer is a
// CUdeviceptr the caller owns until release().
void* upload(const std::byte* bytes, size_t length);
// Copies `length` bytes starting `offset` bytes into the allocation.
void download(void* buffer, size_t offset, std::byte* bytes, size_t length);
void release(void* buffer);

// Waits until all the work on the device so far has finished, as a host
// does before it reads a plugin's output: on the host's stream, and on any
// other stream of the context, such as the default stream a plugin that
// takes no stream renders on.
void finish();

}  // namespace testhost::cuda
