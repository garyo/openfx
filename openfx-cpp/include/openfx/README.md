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
direction, without rewriting the code on either side.

**From a wrapper to C.** Every wrapper gives its C handle: `handle()` on
`ImageEffect`, `ActionArgs`, `Clip`, `Image`, `ImageMemory`, `ParamSet`, the
typed parameters, `Interact`, `Draw`, `Mutex` and `PropertyAccessor`;
`propertySetHandle()` on `Clip` and the parameters; `data()` on `Memory`. The
suites a wrapper calls are there too: `ImageEffect::effectSuite()`,
`propertySuite()` and `paramSuite()`, `ParamSet::suite()` and
`PropertyAccessor::suite()`.

```cpp
OfxStatus render(ImageEffect& effect, ActionArgs& args) override {
  const OfxTime time = args.as<propsets::ImageEffectActionRender_InArgs>().time();
  Clip source = effect.clip(kOfxImageEffectSimpleSourceClipName);
  OfxRectD rod{};
  effect.effectSuite()->clipGetRegionOfDefinition(source.handle(), time, &rod);
  return legacyRender(effect.handle(), args.handle(), &rod);  // a C function
}
```

**From C to a wrapper.** The non-owning wrappers wrap a handle C code already
has, and release nothing: `ImageEffect`, `ActionArgs`, `Clip`, `ParamSet`, the
typed parameters, `Interact`, `Draw`, `PropertyAccessor` and the generated
`propsets` classes. Each takes its suites as a `SuiteContainer` or as raw
suite pointers, and copies the pointers it needs, so a plugin that keeps its
suites in globals needs no container:

```cpp
ImageEffect effect(handle, gEffectSuite, gPropSuite, gParamSuite);
ParamSet params(handle, gEffectSuite, gParamSuite, gPropSuite);
DoubleParam gain(params.handle(), "gain", gParamSuite, gPropSuite);
Interact overlay(interactHandle, gInteractSuite, gPropSuite);
propsets::ClipInstance clipProps(clipPropSet, gPropSuite);
```

An owning wrapper gives its resource back when it goes. It adopts one that C
code acquired, from the C handle and the raw suite pointer. `release()`, which
is `[[nodiscard]]`, hands it back to C code as `std::unique_ptr::release`
does: it returns the handle and leaves the wrapper empty. `reset()` gives the
resource back early.

| Wrapper | Adopts with | On destruction | `release()` returns |
|---|---|---|---|
| `Image` | `Image(image, effectSuite, propertySuite)` | `clipReleaseImage` | the image's property set |
| `ImageMemory` | `ImageMemory(memory, effectSuite, locked)` | `imageMemoryUnlock` if locked, then `imageMemoryFree` | the memory handle; a lock it held passes with it |
| `Memory` | `Memory(data, bytes, memorySuite)` | `memoryFree` | the block |
| `Mutex` | `Mutex(mutex, threadSuite)` | `mutexDestroy` | the mutex |
| `Progress` | `Progress::adoptStarted(effect, progressSuite)` | `progressEnd` | the effect, or null if there is no display |
| `ParamSet::EditScope` | `EditScope::adoptBegun(paramSet, paramSuite)` | `paramEditEnd` | the parameter set, or null if there is no edit |

```cpp
OfxPropertySetHandle raw = nullptr;
if (gEffectSuite->clipGetImage(clip, time, nullptr, &raw) == kOfxStatOK) {
  Image image(raw, gEffectSuite, gPropSuite);  // released when it goes
  if (keepForC)
    gHeldImage = image.release();              // C code releases it now
}
```

**Calling the suite directly.** `PropertyAccessor` reaches any property by
name through the property suite's own calls: `getRaw` and `setRaw` for one
value, `getRawN` and `setRawN` for several (`propGetIntN` and the rest),
`getDimensionRaw`, `reset` (`propReset`) and `exists`. For a call it does not
wrap, `suite()` and `handle()` give the suite and the property set:

```cpp
PropertyAccessor& props = effect.props();
double matrix[9] = {};
props.getRawN("com.example.Matrix", 9, matrix);
props.setRaw("com.example.Pass", 2);
int n = 0;
props.suite()->propGetDimension(props.handle(), kOfxImageEffectPropSupportedContexts, &n);
```

What happens to an exception on its way back to C is under
[Exceptions and the C boundary](#exceptions-and-the-c-boundary).

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

### Mixing the host side with C calls

Every object in `openfx::host` is what its C handle points to. `handle()`
gives the handle, from a const object as well, and the static `from(handle)`
gives the object back: `PropertySet`, `EffectBase` (a descriptor or an
instance, for `OfxImageEffectHandle`), `Clip`, `Image` (a `PropertySet`, so
its handle is the image's property set), `ParamSet`, `Param`, `InteractBase`
(for `OfxInteractHandle`) and `DrawContext`. `Host::ofx()` is the `OfxHost*`
a plugin is given. `from()` is a cast and checks nothing, so it is only for
handles these objects gave out.

The suites are plain C structs of function pointers: `PropertySet::suite()`,
`effectSuite()`, `paramSuite()`, `interactSuite()`, `drawSuite()`,
`memorySuite()`, `multiThreadSuite()`, `messageSuiteV1()` and `V2()`,
`progressSuiteV1()` and `V2()`, and `timeLineSuite()`. Host code calls them as
a plugin would, hands them out from a `fetchSuite` of its own, or calls them
from a suite of its own for the handles it did not make.

Host code reads and writes a property set through `PropertySet`'s own calls
(`set`, `getInt`, `getDouble`, `getString`, `define`), or through the C suite
with `PropertyAccessor` and the generated `openfx::host::propsets` classes
over `PropertySet::suite()`. An action goes to a plugin through
`Plugin::call(action, handle, inArgs, outArgs)`, which is the plugin's main
entry and nothing more, or through `EffectInstance::action(name, inArgs,
outArgs)`, which adds the instance's hooks and its record of whether the
plugin created the instance.

Nothing on the host side adopts or releases: the host owns every object, and
a plugin holds only handles to them.

```cpp
namespace host = openfx::host;

// C++ to C: an instance's property set through the C suites.
OfxPropertySetHandle props = nullptr;
host::effectSuite()->getPropertySet(instance.handle(), &props);
host::PropertySet::suite()->propSetDouble(props, kOfxImageEffectPropFrameRate, 0, 24.0);

// C to C++: an entry of a suite of the host's own, given a clip handle.
OfxStatus myClipCall(OfxImageClipHandle handle) noexcept {
  return openfx::callAtCBoundary([&] {
    host::Clip* clip = host::Clip::from(handle);
    if (!clip || !clip->owner)
      return kOfxStatErrBadHandle;
    return useClip(*clip);  // may throw
  });
}

// An action of the host's own, with an argument no driver knows about.
host::PropertySet in;
in.set("com.example.Reason", 0, "low memory");
OfxStatus status = instance.action("com.example.FlushAction", &in, nullptr);
```

## Exceptions and the C boundary

The wrappers report a failed C call by throwing `openfx::OfxException`, whose
`code()` is the status, or one of its subclasses: `PropertyNotFoundException`
for a property the set does not have (`kOfxStatErrUnknown`),
`ClipNotFoundException`, `ImageNotFoundException`, and
`SuiteNotFoundException` for a suite the host lacks
(`kOfxStatErrMissingHostFeature`).

No exception may cross a C function pointer. That boundary is every function
the other side calls through one: a plugin's `setHost` and main entry, an
overlay's entry point, a thread function given to `multiThread`, and each
suite entry and `fetchSuite` a host provides. `openfx/ofxExceptions.h` has
three functions for it, none of which throws:

- `callAtCBoundary(f, fallback = kOfxStatFailed)` runs `f`, which returns an
  `OfxStatus`, and logs an exception out of it and turns it into a status.
- `statusFromCurrentException(fallback)`, in a `catch` block, gives the status
  for the exception being handled: an `OfxException`'s `code()`,
  `kOfxStatErrMemory` for `std::bad_alloc`, and `fallback` for anything else.
- `logCurrentException(context, args...)`, in a `catch` block, logs
  `context: what()`, with `context`'s `{}` filled from `args`.

The bindings put the boundary in these places, so the code behind them may
throw:

- **Plugin side.** `PluginEntry`'s trampolines and
  `ImageEffectPlugin::dispatch()` answer an action that throws with the
  exception's code, `kOfxStatErrMemory`, or `kOfxStatFailed`, the
  specification's failed action. A plugin class whose constructor throws in
  `setHost` has no status to return there, so every action after that answers
  with it: the exception's code, or `kOfxStatErrFatal`.
  `InteractPlugin::mainEntry()` and `dispatch()` do the same for an overlay.
  `multiThread()` catches what a worker throws on the worker's thread and
  rethrows the first on the calling thread, once every worker has finished.
- **Host side.** Every suite entry in `openfx::host` runs its body through
  `callAtCBoundary`, so the host's own code behind them --- `fetchImage()`,
  `releaseImage()`, `clipRegionOfDefinition()`, the `InteractInstance` and
  `DrawContext` virtuals --- may throw, and the plugin gets a status; an
  exception from `abort()` reads as "go on". `Host`'s `fetchSuite` answers
  one with no suite. The default `multiThread` catches what a plugin's thread
  function throws and returns `kOfxStatFailed`. The destructors that send an
  action, `~Plugin` (Unload) and `destroyInstance()`, log what it throws.

Anywhere else the boundary is the caller's own. A C entry point of your own
that calls the wrappers wraps its body:

```cpp
OfxStatus myMainEntry(const char* action, const void* handle,
                      OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
  return openfx::callAtCBoundary(
      [&] { return handleAction(action, handle, inArgs, outArgs); });
}
```

Code that must go on after a failure catches it itself:

```cpp
try {
  renderTile(tile);
} catch (...) {
  openfx::logCurrentException("tile {}", tile.index);
  status = openfx::statusFromCurrentException(kOfxStatFailed);
}
```

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

An accessor method takes its name from the property's C `#define`, less the
`kOfx`, the `Prop` and the kind of object the class is for:
`kOfxImageEffectPropProjectPixelAspectRatio` is `projectPixelAspectRatio()` and
`setProjectPixelAspectRatio()`. Any other qualifier stays, as OpenGL does in
`setOpenGLPixelDepth()` for `kOfxOpenGLPropPixelDepth`, and so does the object
when two properties of a set would otherwise share a name (`type()` and
`paramType()`). In the generated headers, each property's methods follow a
comment giving its `#define`, so a search for the C constant finds them.

Every getter and setter takes `error_if_missing` as its last argument. A call
with it false is soft about a property the set does not have, and nothing
else: a soft read returns the fallback for the property's type, which
`PropertyAccessor` in `ofxPropsAccess.h` documents, and a soft write does
nothing; any other failure throws. The methods of a property the
specification lets a host leave out are soft by default.

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
these headers prevents it. A suite the headers do not know, such as a host's
own or one newer than they are, is registered once at global scope with
`OPENFX_DEFINE_SUITE(SuiteType, suiteName, version)`, after which
`SuiteContainer::get<SuiteType>()` and `has<SuiteType>()` find it like any
other; `get<T>()` of a suite type never registered does not compile.

-------------
Copyright OpenFX and contributors to the OpenFX project.

`SPDX-License-Identifier: BSD-3-Clause`
