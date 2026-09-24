// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <ofxCore.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "openfx/ofxLog.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace openfx::host {

// Implementation details for PluginBinary::load and standardPluginPaths.
namespace detail {

// Architecture directories to try inside a bundle, most specific first
// (see Documentation/sources/Reference/ofxPackaging.rst).
inline std::vector<std::string> archDirs() {
#if defined(__APPLE__)
  // MacOS plug-ins are universal binaries and live in a single "MacOS"
  // directory; "MacOS-x86-64" is kept only for old, Intel-only bundles.
  return {"MacOS", "MacOS-x86-64"};
#elif defined(_WIN32)
#  if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || \
      defined(__arm64__)
  return {"Win-arm64ec", "Win64", "Win32"};
#  else
  return {"Win64", "Win32"};
#  endif
#elif defined(__linux__)
  return {"Linux-x86-64", "Linux-x86"};
#else
  return {};
#endif
}

inline std::filesystem::path binaryInBundle(const std::filesystem::path& bundle) {
  std::string base = bundle.filename().string();
  base = base.substr(0, base.size() - std::strlen(".bundle"));  // Foo.ofx
  for (const auto& arch : archDirs()) {
    std::filesystem::path candidate = bundle / "Contents" / arch / base;
    if (std::filesystem::exists(candidate))
      return candidate;
  }
  throw std::runtime_error("no plugin binary for this architecture in " +
                           bundle.string());
}

inline bool hasBundleSuffix(const std::filesystem::path& p) {
  static const std::string kSuffix = ".ofx.bundle";
  std::string name = p.filename().string();
  return name.size() >= kSuffix.size() &&
         name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

inline bool isBundleDir(const std::filesystem::path& p) {
  return std::filesystem::is_directory(p) && hasBundleSuffix(p);
}

// The bundle a binary is in (Foo.ofx.bundle/Contents/<arch>/Foo.ofx), or the
// binary itself if it is in none.
inline std::filesystem::path bundleOf(const std::filesystem::path& binary) {
  const std::filesystem::path contents = binary.parent_path().parent_path();
  if (contents.filename() == "Contents" && hasBundleSuffix(contents.parent_path()))
    return contents.parent_path();
  return binary;
}

}  // namespace detail

// One plugin binary (.ofx) and the OfxPlugin structs it exports.
class PluginBinary {
 public:
  // Accepts a .ofx.bundle directory, a bare .ofx binary, or a directory
  // containing bundles (each of which is loaded); bundles that fail to load
  // inside a directory are skipped with a warning.
  static std::vector<std::unique_ptr<PluginBinary>> load(
      const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::vector<std::unique_ptr<PluginBinary>> out;
    if (detail::isBundleDir(path)) {
      out.push_back(std::make_unique<PluginBinary>(detail::binaryInBundle(path)));
    } else if (fs::is_directory(path)) {
      std::vector<fs::path> bundles;
      for (const auto& entry : fs::directory_iterator(path))
        if (detail::isBundleDir(entry.path()))
          bundles.push_back(entry.path());
      std::sort(bundles.begin(), bundles.end());
      for (const auto& b : bundles) {
        try {
          out.push_back(std::make_unique<PluginBinary>(detail::binaryInBundle(b)));
        } catch (const std::exception& e) {
          openfx::Logger::warn("skipping {}: {}", b.string(), e.what());
        }
      }
    } else if (fs::exists(path)) {
      out.push_back(std::make_unique<PluginBinary>(path));
    } else {
      throw std::runtime_error("no such plugin path: " + path.string());
    }
    return out;
  }

  explicit PluginBinary(const std::filesystem::path& binary)
      : path_(binary), bundlePath_(detail::bundleOf(binary)) {
#ifdef _WIN32
    HMODULE mod = LoadLibraryW(binary.wstring().c_str());
    if (!mod)
      throw std::runtime_error("cannot load " + binary.string());
    dl_ = mod;
    auto* getNumber =
        reinterpret_cast<int (*)()>(GetProcAddress(mod, "OfxGetNumberOfPlugins"));
    auto* getPlugin =
        reinterpret_cast<OfxPlugin* (*)(int)>(GetProcAddress(mod, "OfxGetPlugin"));
#else
    dl_ = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!dl_)
      throw std::runtime_error(std::string("cannot load ") + binary.string() + ": " +
                               dlerror());
    auto* getNumber = reinterpret_cast<int (*)()>(dlsym(dl_, "OfxGetNumberOfPlugins"));
    auto* getPlugin = reinterpret_cast<OfxPlugin* (*)(int)>(dlsym(dl_, "OfxGetPlugin"));
#endif
    if (!getNumber || !getPlugin)
      throw std::runtime_error(binary.string() + " does not export the OFX entry points");
    int n = getNumber();
    for (int i = 0; i < n; ++i)
      if (OfxPlugin* p = getPlugin(i))
        plugins_.push_back(p);
    openfx::Logger::debug("loaded {} ({} plugin{})", binary.string(), n,
                          n == 1 ? "" : "s");
  }

  ~PluginBinary() {
    // Plugin binaries are deliberately never unloaded: the OfxPlugin structs and
    // any static state they hold must outlive every use, and unloading buys nothing here.
  }

  PluginBinary(const PluginBinary&) = delete;
  PluginBinary& operator=(const PluginBinary&) = delete;

  const std::filesystem::path& path() const { return path_; }
  // The bundle the binary is in, or the binary itself if it is in none: the
  // path kOfxPluginPropFilePath gives a plugin.
  const std::filesystem::path& bundlePath() const { return bundlePath_; }
  const std::vector<OfxPlugin*>& plugins() const { return plugins_; }

 private:
  std::filesystem::path path_;
  std::filesystem::path bundlePath_;
  void* dl_ = nullptr;
  std::vector<OfxPlugin*> plugins_;
};

// The plugin search path a host should use by default: OFX_PLUGIN_PATH,
// followed by the platform's standard install directory (see
// Documentation/sources/Reference/ofxPackaging.rst). Entries are not
// filtered by existence; callers do that.
inline std::vector<std::filesystem::path> standardPluginPaths() {
  std::vector<std::filesystem::path> paths;

  // The packaging spec gives ';' for Windows and macOS and ':' for Linux.
#if defined(_WIN32) || defined(__APPLE__)
  constexpr char kSeparator = ';';
#else
  constexpr char kSeparator = ':';
#endif

  if (const char* envPath = std::getenv("OFX_PLUGIN_PATH")) {
    std::string s(envPath);
    std::size_t start = 0;
    while (start <= s.size()) {
      std::size_t end = s.find(kSeparator, start);
      if (end == std::string::npos)
        end = s.size();
      if (end > start)
        paths.emplace_back(s.substr(start, end - start));
      start = end + 1;
    }
  }

#if defined(_WIN32)
  // The Windows standard location is %CommonProgramFiles%\OFX\Plugins; the
  // literal "C:\Program Files\Common Files\OFX\Plugins" is the deprecated
  // English-locale fallback the packaging doc keeps for backwards compatibility.
  wchar_t commonProgramFiles[MAX_PATH];
  DWORD len =
      GetEnvironmentVariableW(L"CommonProgramFiles", commonProgramFiles, MAX_PATH);
  if (len > 0 && len < MAX_PATH)
    paths.push_back(std::filesystem::path(commonProgramFiles) / "OFX" / "Plugins");
  paths.push_back("C:\\Program Files\\Common Files\\OFX\\Plugins");
#elif defined(__APPLE__)
  paths.push_back("/Library/OFX/Plugins");
#else
  paths.push_back("/usr/OFX/Plugins");
#endif

  return paths;
}

}  // namespace openfx::host
