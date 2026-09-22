<!--
This is a temporary space to hold changes and release notes for the next release.
When preparing a release, copy this into the release-notes.md and reset this to empty.

-->

# Release Notes - NEXT (upcoming)

This is version NEXT of the OpenFX API.

## Key Features of OpenFX Version NEXT:

- **Unlicensed-render behaviour**: Added `kOfxImageEffectPropBehaviourWhenUnlicensed` so a host can tell plugins whether to fail a render or render anyway (e.g. watermarked) when unlicensed (issue #202).
- **Parameter interpolation types**: Added `kOfxParamInterpType` and related definitions documenting standard keyframe interpolation modes (issue #116).
- **Obsolete plugins**: Added `kOfxImageEffectPluginPropObsolete` so a plugin bundle can mark a plugin as obsolete: available for use in old projects but not offered to users for new use (issue #221).
- **Windows ARM64 packaging**: Defined plugin install locations for Windows on ARM, including the new normative `Win-arm64ec` folder for Arm64EC/Arm64X plug-ins, with most-specific-first DLL search order (issue #160).
- **Project-load semantics**: Hosts are now required to send the `instanceChanged` action with `kOfxPropChangeReason` = `kOfxChangePluginEdited` when a clip or parameter was changed while loading a project (issue #184).

- **C++ bindings**: The SDK now ships `openfx-cpp`, a prerelease set of header-only C++ bindings over the OFX C API, for plugins (`openfx/plugin/`) and for hosts (`openfx/host/`). They give compile-time-checked property access generated from the specification's own metadata, RAII over images and memory, wrappers over every suite an image effect normally needs, and a small logging facility, while leaving the raw handles reachable. CMake exports them as `OpenFX::openfx-cpp` and compile-checks every header at C++17 and C++20; Conan exports them as the `openfx-cpp` component. Two programs in the tree are written on them and test them: `ofxtesthost` (below) on the host side, and the `CppGain` example plugin, a gain/offset filter that calls no suite directly. See `openfx-cpp/include/openfx/README.md` and the C++ bindings chapter of the programming guide. Prerelease: fields, the GPU render suites, parametric parameters, interacts and the OCIO colour management style are not covered yet, and the interfaces may still change.

- **Test host**: Added `ofxtesthost` (`TestHost/`), a command-line host in modern C++ built on the `openfx-cpp` type-safe property accessors rather than `HostSupport`. It loads plugins, describes them, sets parameters, renders a frame through one plugin or a chain, writes PPM/PFM output, and checks pixels; the pcons build runs it against the example plugins as a test suite.

## Fixes in OpenFX Version NEXT:

- Fixed incorrect enum value names in property metadata (`@propdef`) for several properties, and made the generator reject unknown enum names so this can't regress (issue #247).
- Set proper RGBA colour defaults on the colour parameter in the Rectangle example (issue #240).
- Fixed the ColourSpace example to compile under `FMT_ENFORCE_COMPILE_STRING`, with a CI job to keep it that way (issue #236).
- HostSupport: an effect instance now inherits `kOfxImageEffectPropSupportsTiles` and the GPU `*RenderSupported` properties from the plugin descriptor instead of overriding them with a hard default, so values set only in describe are honoured (issue #177). The header docs now state this inheritance rule for hosts.
- CMake: use `target_compile_features(cxx_std_17)` instead of forcing `CMAKE_CXX_STANDARD`, so consumers can build with a later C++ standard (issue #208).
- `OfxExport` now marks a symbol visible on GCC and Clang as well as exporting it on Windows. The entry points in `ofxCore.h` are declared with it, so plugins built with hidden visibility export `OfxGetPlugin`, `OfxGetNumberOfPlugins` and `OfxSetHost` definitions properly.  The examples no longer need to define `EXPORT` macros.
- Fixed the ColourSpace example's `OfxSetHost` to have the proper signature so it actually gets called.
- Fixed the `@propdef` metadata of `kOfxParamPropChoiceEnum` (a string array, not a bool) and `kOfxParamPropDimensionLabel` (one label per dimension, not one).
- Fixed the Invert example never releasing its output image (a shadowed handle variable).

## Deprecations

## Detailed List of Changes

- Property metadata now lives in inline `@propdef` blocks in the headers (previously a separate YAML file); `scripts/gen-props.py` generates the reference documentation and the `openfx-cpp` metadata headers from it (#233).
- `openfx-cpp`: the support headers the generated accessors depend on (`ofxPropsAccess.h`, `ofxSpan.h`, logging, exceptions, `SuiteContainer`, `Image`/`Clip` wrappers) are now in the tree; the headers are split into common (`openfx/`), plugin-side (`openfx/plugin/`, namespace `openfx::plugin`) and host-side (`openfx/host/`, namespace `openfx::host`), with the generated property-set accessor classes in `openfx::plugin::propsets` and `openfx::host::propsets`; accessor names no longer collide with C++ keywords or each other (`defaultValue()`, `paramPropType()`), and the host/plugin/instance property families get their natural short names (`setIsBackground()`, `setEffectDuration()`).
- Added a pcons build (`pcons-build.py`) for the libraries, the test host and all plugin bundles, laid out identically to the CMake build.
- Conan packaging: restructured the recipe to the standard Conan Center Index layout (headers under `include/`, libs and CMake module under `lib/`, licenses under `licenses/`) (issues #238, #246), and example-only dependencies (OpenGL, CImg, spdlog, OpenCL) are no longer imposed on consumers — they're gated behind a new `build_examples` option (#253).
- Added `SECURITY.md` and fixed stale repository URLs (#242).
- CI: hardened workflows (actions pinned to SHAs, untrusted inputs via env) (#235); updated Conan and pre-authorized future compiler versions so new Xcode/compiler releases don't break builds (#252); pinned the Windows CUDA job to VS2022.
- CI: the CentOS 7 jobs (VFX CY2021 and CY2022) run GitHub's JavaScript actions on a glibc 2.17 build of Node 24, since GitHub's runners no longer have Node 20.

