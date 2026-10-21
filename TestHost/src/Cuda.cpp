// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
// Cuda.h on the CUDA driver API, loaded at run time: the host needs the CUDA
// headers to build, but only the driver to run, and runs without CUDA where
// there is no driver.
#if defined(OFX_TESTHOST_CUDA)
#include "Cuda.h"

#include <cuda.h>
#include <cudaTypedefs.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace testhost::cuda {

namespace {

// The driver entry points the host uses, at the version of the headers it
// was built with (so cuMemAlloc is cuMemAlloc_v2, and so on).
struct Driver {
  PFN_cuInit init;
  PFN_cuGetErrorName getErrorName;
  PFN_cuDeviceGetCount deviceGetCount;
  PFN_cuDeviceGet deviceGet;
  PFN_cuDeviceGetName deviceGetName;
  PFN_cuDeviceGetAttribute deviceGetAttribute;
  PFN_cuDevicePrimaryCtxRetain primaryCtxRetain;
  PFN_cuCtxPushCurrent ctxPushCurrent;
  PFN_cuCtxPopCurrent ctxPopCurrent;
  PFN_cuCtxSynchronize ctxSynchronize;
  PFN_cuStreamCreate streamCreate;
  PFN_cuMemAlloc memAlloc;
  PFN_cuMemFree memFree;
  PFN_cuMemcpyHtoD memcpyHtoD;
  PFN_cuMemcpyDtoH memcpyDtoH;
};

void* openDriver() {
#if defined(_WIN32)
  return reinterpret_cast<void*>(LoadLibraryA("nvcuda.dll"));
#else
  return dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
}

void* symbol(void* library, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
  return dlsym(library, name);
#endif
}

// The driver, or nothing where there is none or it is older than the
// headers.
const std::optional<Driver>& driver() {
  static const std::optional<Driver> d = []() -> std::optional<Driver> {
    void* library = openDriver();
    if (!library)
      return std::nullopt;
    auto getProcAddress =
        reinterpret_cast<PFN_cuGetProcAddress>(symbol(library, "cuGetProcAddress_v2"));
    if (!getProcAddress)
      return std::nullopt;
    Driver drv{};
    bool complete = true;
    const auto get = [&](const char* name, auto& fn) {
      void* p = nullptr;
      CUdriverProcAddressQueryResult found{};
      if (getProcAddress(name, &p, CUDA_VERSION, CU_GET_PROC_ADDRESS_DEFAULT, &found) !=
              CUDA_SUCCESS ||
          found != CU_GET_PROC_ADDRESS_SUCCESS || !p)
        complete = false;
      fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
    };
    get("cuInit", drv.init);
    get("cuGetErrorName", drv.getErrorName);
    get("cuDeviceGetCount", drv.deviceGetCount);
    get("cuDeviceGet", drv.deviceGet);
    get("cuDeviceGetName", drv.deviceGetName);
    get("cuDeviceGetAttribute", drv.deviceGetAttribute);
    get("cuDevicePrimaryCtxRetain", drv.primaryCtxRetain);
    get("cuCtxPushCurrent", drv.ctxPushCurrent);
    get("cuCtxPopCurrent", drv.ctxPopCurrent);
    get("cuCtxSynchronize", drv.ctxSynchronize);
    get("cuStreamCreate", drv.streamCreate);
    get("cuMemAlloc", drv.memAlloc);
    get("cuMemFree", drv.memFree);
    get("cuMemcpyHtoD", drv.memcpyHtoD);
    get("cuMemcpyDtoH", drv.memcpyDtoH);
    if (!complete || drv.init(0) != CUDA_SUCCESS)
      return std::nullopt;
    return drv;
  }();
  return d;
}

const Driver& drv() {
  if (!driver())
    throw std::logic_error("no CUDA driver");
  return *driver();
}

void check(CUresult r, const char* what) {
  if (r == CUDA_SUCCESS)
    return;
  const char* name = nullptr;
  drv().getErrorName(r, &name);
  throw std::runtime_error(std::string(what) +
                           " failed: " + (name ? name : "unknown error"));
}

int attribute(CUdevice device, CUdevice_attribute a) {
  int value = 0;
  check(drv().deviceGetAttribute(&value, a, device), "cuDeviceGetAttribute");
  return value;
}

std::string nameOf(CUdevice device) {
  char name[256] = {};
  check(drv().deviceGetName(name, sizeof name, device), "cuDeviceGetName");
  return name;
}

std::string gDeviceSpec;

// The device: the one --cuda-device names, else the highest compute
// capability, the first of those; none where there is no driver or device.
const std::optional<CUdevice>& device() {
  static const std::optional<CUdevice> d = []() -> std::optional<CUdevice> {
    if (!driver())
      return std::nullopt;
    int count = 0;
    if (drv().deviceGetCount(&count) != CUDA_SUCCESS || count == 0)
      return std::nullopt;
    std::vector<CUdevice> devices(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
      check(drv().deviceGet(&devices[static_cast<size_t>(i)], i), "cuDeviceGet");
    if (!gDeviceSpec.empty()) {
      // An ordinal, or else part of a name ("970" is a GeForce GTX 970).
      const bool digits =
          gDeviceSpec.size() < 4 && std::all_of(
                                        gDeviceSpec.begin(), gDeviceSpec.end(),
                                        [](char c) { return c >= '0' && c <= '9'; });
      if (digits && std::stoi(gDeviceSpec) < count)
        return devices[static_cast<size_t>(std::stoi(gDeviceSpec))];
      std::string names;
      for (size_t i = 0; i < devices.size(); ++i) {
        const std::string name = nameOf(devices[i]);
        if (name.find(gDeviceSpec) != std::string::npos)
          return devices[i];
        names += (i ? ", " : "") + std::to_string(i) + ": " + name;
      }
      throw std::runtime_error("--cuda-device " + gDeviceSpec +
                               " is none of the CUDA devices (" + names + ")");
    }
    const auto capability = [](CUdevice dev) {
      return attribute(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR) * 100 +
             attribute(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
    };
    return *std::max_element(devices.begin(), devices.end(), [&](CUdevice a, CUdevice b) {
      return capability(a) < capability(b);
    });
  }();
  return d;
}

CUcontext context() {
  static const CUcontext c = [] {
    if (!device())
      throw std::logic_error("no CUDA device");
    CUcontext ctx = nullptr;
    check(drv().primaryCtxRetain(&ctx, *device()), "cuDevicePrimaryCtxRetain");
    return ctx;
  }();
  return c;
}

// The primary context, current for the guard's scope.
class Current {
 public:
  Current() { check(drv().ctxPushCurrent(context()), "cuCtxPushCurrent"); }
  ~Current() {
    CUcontext popped = nullptr;
    drv().ctxPopCurrent(&popped);
  }
  Current(const Current&) = delete;
  Current& operator=(const Current&) = delete;
};

}  // namespace

void selectDevice(const std::string& spec) { gDeviceSpec = spec; }

bool available() { return device().has_value(); }

std::string deviceName() {
  const CUdevice dev = device().value();
  return nameOf(dev) + " (sm_" +
         std::to_string(attribute(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR)) +
         std::to_string(attribute(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR)) +
         ")";
}

void* stream() {
  static CUstream s = [] {
    Current current;
    CUstream st = nullptr;
    check(drv().streamCreate(&st, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    return st;
  }();
  return s;
}

void* upload(const std::byte* bytes, size_t length) {
  Current current;
  CUdeviceptr buffer = 0;
  // Never empty, so every image has a pointer of its own.
  check(drv().memAlloc(&buffer, std::max<size_t>(length, 16)), "cuMemAlloc");
  if (length > 0) {
    if (CUresult r = drv().memcpyHtoD(buffer, bytes, length); r != CUDA_SUCCESS) {
      drv().memFree(buffer);
      check(r, "cuMemcpyHtoD");
    }
  }
  return reinterpret_cast<void*>(buffer);
}

void download(void* buffer, size_t offset, std::byte* bytes, size_t length) {
  Current current;
  check(drv().memcpyDtoH(bytes, reinterpret_cast<CUdeviceptr>(buffer) + offset, length),
        "cuMemcpyDtoH");
}

void release(void* buffer) {
  Current current;
  drv().memFree(reinterpret_cast<CUdeviceptr>(buffer));
}

void finish() {
  Current current;
  check(drv().ctxSynchronize(), "cuCtxSynchronize");
}

}  // namespace testhost::cuda
#endif
