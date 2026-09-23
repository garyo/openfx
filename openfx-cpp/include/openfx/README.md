# OpenFX C++ Bindings

`openfx-cpp` is a header-only C++ layer over the OpenFX C API, for both sides
of it. It does not replace the API: every wrapper calls the same suites a C
plugin or host would, and a program can drop to the raw handles at any point.
What it adds is type safety and lifetime safety --- property names and types
checked at compile time from the specification's own metadata, RAII over
images and memory, exceptions instead of status codes where that reads better,
and a small logging facility.

It is prerelease: the headers ship with the SDK and are exercised by
everything in this tree, but the interfaces may still change. See
[Status](#status).

## Requirements

A C++17 compiler, plus [tcb-span](https://github.com/tcbrindle/span) for the
`std::span` stand-in `openfx/ofxSpan.h` needs below C++20; from C++20 on,
nothing but the standard library. There is nothing to build or link: the
headers are the whole library.

## Using them

From CMake, in this source tree or from an installed/Conan package:

```cmake
target_link_libraries(my-plugin PRIVATE OpenFX::openfx-cpp)
```

That target carries the include directories for both `openfx-cpp/include` and
the C API headers in `include/`, requires C++17, and links tcb-span when
`find_package(tcb-span)` finds it. The Conan recipe requires tcb-span under
the `build_openfx_cpp` option (on by default; a C++20-only consumer can turn
it off) and exports the headers as the `openfx-cpp` component, so
`openfx::openfx-cpp` is the package's target name.

Without CMake, put `openfx-cpp/include` and `include/` on the include path and
include what you need:

```cpp
#include <openfx/plugin/ofxPluginBase.h>   // plugin side
#include <openfx/host/ofxHost.h>           // host side
```

## Layout

The headers are split by which side of the API uses them. Include them
with `openfx-cpp/include` on the include path, as `<openfx/...>`.

| Directory | Namespace | Used by | Contents |
|---|---|---|---|
| `openfx/` | `openfx` | plugins and hosts | Property metadata (`ofxPropsMetadata.h`, `ofxPropsBySet.h`), the type-safe `PropertyAccessor` (`ofxPropsAccess.h`), `SuiteContainer` (`ofxSuites.h`), the pixel depth and component vocabulary (`ofxPixels.h`), the colour management styles and the native config's colourspaces (`ofxColourspaces.h`), exceptions, logging, status strings, rect/point and per-clip property-name helpers (`ofxMisc.h`), the span shim. |
| `openfx/plugin/` | `openfx::plugin` | plugins only | RAII `Image` and `Clip` wrappers over the image effect suite (`ofxImage.h`, `ofxClip.h`); `ImageEffect`, `ActionArgs` and `ImageMemory` over an effect handle (`ofxEffect.h`); `ParamSet` and one class per parameter type (`ofxParam.h`); wrappers over the message, progress, memory, multithread and timeline suites (`ofxMessage.h`, `ofxProgress.h`, `ofxMemory.h`, `ofxMultiThread.h`, `ofxTimeLine.h`); `Interact` and the `InteractPlugin` dispatcher for an overlay (`ofxInteract.h`) with `Draw` over the OFX 1.5 draw suite (`ofxDraw.h`); the `ImageEffectPlugin` action dispatcher and its `PluginEntry` boilerplate (`ofxPluginBase.h`); and the generated per-property-set accessor classes (`openfx::plugin::propsets`: getters for host-written properties, setters for plugin-written ones). |
| `openfx/host/` | `openfx::host` | hosts only | `PropertySet`, a metadata-driven property store with an `OfxPropertySuiteV1` over it (`ofxPropertySet.h`); `PluginBinary`, which loads a plugin binary or bundle and lists its plugins, plus the standard plugin search paths (`ofxPluginBinary.h`); default memory, multithread, message, progress and timeline suites (`ofxDefaultSuites.h`); `Host`, the `OfxHost` struct with its property set and suite container (`ofxHost.h`); `Plugin`, one plugin driven through its main entry point (`ofxPlugin.h`); the generic effect model -- parameters, clips, images, descriptors and an abstract `EffectInstance` that drives the actions -- with the image effect and parameter suites over it (`ofxEffect.h`); the overlay interact model -- `InteractDescriptor`, `InteractInstance` and the draw, pen, key and focus drivers -- with the interact suite over it (`ofxInteract.h`), and the abstract `DrawContext` a host implements behind `OfxDrawSuiteV1` (`ofxDrawSuiteHost.h`); and the generated per-property-set accessor classes for the host side (`openfx::host::propsets`: setters for host-written properties, getters for plugin-written ones). |

Anything in `openfx/` takes the suites it needs as arguments, so it works
against a real host's suites from a plugin and against a host's own suite
implementations from inside that host. `ofxPropSetAccessors.h` exists in
both `plugin/` and `host/` with the same class names, so a translation unit
that needs both keeps them apart by namespace.

Four programs in the tree consume the bindings and double as their tests:
`TestHost/` (a command-line host built on the host side),
`Examples/TestProps/` (a plugin that checks a host's property sets against
the metadata), `Examples/CppGain/` (a gain filter written entirely on the
plugin side, calling no suite directly) and
[`../../examples/minimal-plugin/minimal.cpp`](../../examples/minimal-plugin/minimal.cpp),
the skeleton below.

## Writing a plugin

A plugin is one class and two exported functions. Derive from
`ImageEffectPlugin`, override the actions you care about --- every action is a
virtual with typed arguments, and the ones you leave alone return
`kOfxStatReplyDefault` --- and hand the class to `PluginEntry`, which builds
the `OfxPlugin` struct, routes the main entry point and turns an escaped
exception into a status code. The two functions need no export specifier:
`ofxCore.h` declares them with `OfxExport`, and the definitions inherit it,
even in a build with hidden symbol visibility.

```cpp
class MinimalPlugin : public ImageEffectPlugin {
 public:
  static constexpr const char* kIdentifier = "org.openeffects.example.minimal";

 protected:
  // What the plugin is and what it can do, once per binary.
  OfxStatus describe(ImageEffect& effect) override;
  // The clips and parameters, once per context the host asks about.
  OfxStatus describeInContext(ImageEffect& effect, std::string_view) override;
  // One render window, at one time, into the output clip's image.
  OfxStatus render(ImageEffect& effect, ActionArgs& args) override;
};

using Entry = PluginEntry<MinimalPlugin>;

int OfxGetNumberOfPlugins(void) { return Entry::numberOfPlugins(); }
OfxPlugin* OfxGetPlugin(int nth) { return Entry::get(nth); }
```

Inside those three:

- `describe` writes the plugin's descriptor through
  `effect.descriptor()`, whose setters are generated from the property
  metadata: `setLabel`, `setSupportedContexts`, `setSupportedPixelDepths`,
  `setSupportsTiles` and the rest, each taking the type the specification
  gives that property and each fluent, so they chain.
- `describeInContext` calls `effect.defineClip(name)` per clip and
  `effect.params()` for the parameter set, which has a `define<Type>` per
  parameter type (`defineDouble`, `defineRGBA`, `defineChoice`, `definePage`
  ...), again with generated setters.
- `render` reads its arguments through
  `args.as<propsets::ImageEffectActionRender_InArgs>()`, fetches parameter
  values with `params.get<DoubleParam>(name).getValueAtTime(time)`, and gets
  the images from `effect.clip(name).getImage(time)`. `Image` releases itself,
  so an early `return` cannot leak one.

[`minimal.cpp`](../../examples/minimal-plugin/minimal.cpp) is that skeleton
filled in --- a brightness filter on float RGBA, about sixty lines, compiled
by the `openfx-cpp-check` CMake target so it cannot go stale.
[`Examples/CppGain/cppgain.cpp`](../../../Examples/CppGain/cppgain.cpp) is the
same plugin grown up: every pixel depth, multithreading, progress reporting,
abort checks and OFX 1.5 colour management, still without calling a suite
directly.

### Mixing the wrappers with C calls

A plugin can pass between the wrappers and raw C calls at any point, in either
direction, without rewriting the code on either side:

- Every wrapper exposes its C handle: `handle()`, and `propertySetHandle()`
  on `Clip` and the parameters.
- The non-owning wrappers (`Clip`, `ImageEffect`, `ParamSet`, the typed
  parameters, `Interact`, `Draw`) and the generated `propsets` accessors wrap
  any handle a C call returned, and release nothing.
- An owning wrapper gives its resource back when it goes. It adopts one C code
  acquired, taking the C handle and the raw suite pointer, so code that keeps
  its suites in globals needs no `SuiteContainer`. `release()` hands the
  resource back to C code as `std::unique_ptr::release` does: it returns the
  handle and leaves the wrapper empty. `reset()` gives the resource back early.

| Wrapper | Adopts with | On destruction |
|---|---|---|
| `Image` | `Image(image, effectSuite, propertySuite)` | `clipReleaseImage` |
| `ImageMemory` | `ImageMemory(memory, effectSuite, locked)` | `imageMemoryUnlock` if locked, then `imageMemoryFree` |
| `Memory` | `Memory(data, bytes, memorySuite)` | `memoryFree` |
| `Mutex` | `Mutex(mutex, threadSuite)` | `mutexDestroy` |
| `Progress` | `Progress::adoptStarted(effect, progressSuite)` | `progressEnd` |
| `ParamSet::EditScope` | `EditScope::adoptBegun(paramSet, paramSuite)` | `paramEditEnd` |

## Writing a host

The host side is a framework, not a host: it owns the property store, the
plugin loading, the effect model and the suites, and leaves the host its own
policy --- pixel storage, the formats to negotiate, threading, the UI.
`TestHost/` is a complete worked example in about the size of a weekend
project; `TestHost/README.md` and `TestHost/DESIGN.md` explain its choices.

Five things a host supplies:

1. **A `Host`.** Derive from `openfx::host::Host`, fill its property set
   through the generated `accessor()` (`setName`, `setLabel`,
   `setSupportedPixelDepths`, `setSupportedContexts`, the GPU-support flags,
   the colour management style), and register the suites it offers:
   `PropertySet::suite()`, `effectSuite()`, `paramSuite()`, and
   `addDefaultSuites()` for memory, multithread, message, progress and
   timeline. `host.ofx()` is what a plugin is handed through `setHost`.
2. **Plugin loading.** `PluginBinary::load(path)` takes a `.ofx.bundle`
   directory, a bare `.ofx` binary or a directory of bundles, and lists the
   plugins each one exports; `standardPluginPaths()` gives the search paths
   the specification defines. Wrap one in `openfx::host::Plugin` to drive it:
   `setHost`, Load, Describe, DescribeInContext.
3. **An `EffectInstance` subclass.** Three pure virtuals:
   `clipProperties()`, the format and timing this host negotiates for one
   clip; `fetchImage()`, an image of a clip at a time over a region; and
   `releaseImage()`. Override `makeClip()` as well if the host's clips carry
   storage, and `abort()` and `clipRegionOfDefinition()` if it has answers for
   them. Everything else --- the descriptors, the parameters with their
   animation, the actions and their argument property sets --- the framework
   handles.
4. **Image storage.** Derive from `openfx::host::Image` and attach the pixel
   buffer; the framework fills in the properties the plugin reads (`data`,
   `bounds`, `rowBytes`, `regionOfDefinition`, `renderScale`).
5. **The render sequence.** The host decides what to call and when:
   GetRegionOfDefinition, GetRegionsOfInterest, GetFramesNeeded, IsIdentity,
   BeginSequenceRender, Render per tile, EndSequenceRender. `EffectInstance`
   has a method per action that marshals the arguments and returns the status.

## Generated metadata

`ofxPropsMetadata.h`, `ofxPropsBySet.h` and both `ofxPropSetAccessors.h` are
generated from the `@propdef`, `@propset` and `@actiondef` blocks in
`include/*.h`, the same blocks the property reference documentation comes
from. Do not edit them by hand; edit the metadata in the C headers and
regenerate:

```sh
uvx pcons run gen-props          # or: python scripts/gen-props.py
```

`Documentation/README.md` documents the metadata block format. A host that
defines properties of its own generates matching accessors for them in its own
namespace; `../../examples/host-specific-props/` shows how.

## Logging

`openfx/ofxLog.h` is a small thread-safe logger with `{}`-style formatting:
`Logger::debug/info/warn/error`, a settable level, and a settable handler for
sending messages somewhere else. `README-logging.md` has the details.

## Testing

The pcons build compiles the headers, the test host and the plugins written on
them, and runs the host against the plugins:

```sh
uvx pcons BUILD_PLUGINS=1                # the test host and the plugin bundles
uvx pcons -B build/pcons/release test    # run the host against them
uvx pcons -B build/pcons/tidy CLANG_TIDY=1      # clang-tidy alongside every compile
uvx pcons -B build/pcons/asan SANITIZE=1 test   # ASan and UBSan
```

CMake does not build the host or the examples, but it does compile-check the
headers: `OFX_BUILD_OPENFX_CPP_CHECK` (on by default) adds an
`openfx-cpp-check` target that compiles every header, on each side, at C++17
and at C++20, along with the minimal plugin. `TestHost/fuzz.py` drives the
test host over randomised host choices to shake out assumptions in a plugin.

## Status

Prerelease. The headers are complete enough to write a real filter and a real
host on --- the test host and `CppGain` are the proof --- but the interfaces
are not frozen, and these parts of OpenFX have no wrappers yet:

- fields and field rendering
- the GPU render suites (OpenGL, CUDA, Metal, OpenCL)
- parametric parameters
- OpenGL (V1) overlay interacts: the Draw-suite (V2) overlays are covered
- the dialog suite
- the OCIO and full colour management styles (basic and core are covered)

For those, call the C suites directly through `SuiteContainer`; nothing in
these headers prevents it.

-------------
Copyright OpenFX and contributors to the OpenFX project.

`SPDX-License-Identifier: BSD-3-Clause`
