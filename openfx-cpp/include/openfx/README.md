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
| `openfx/` | `openfx` | plugins and hosts | Property metadata (`ofxPropsMetadata.h`, `ofxPropsBySet.h`), the type-safe `PropertyAccessor` (`ofxPropsAccess.h`) and `CStringView`, the C string its string getters return (`ofxCStringView.h`), `SuiteContainer` (`ofxSuites.h`), the pixel depth and component vocabulary (`ofxPixels.h`), the colour management styles and the native config's colourspaces (`ofxColourspaces.h`), exceptions, logging, status strings, rect/point and per-clip property-name helpers (`ofxMisc.h`), the span shim. |
| `openfx/plugin/` | `openfx::plugin` | plugins only | RAII `Image` and `Clip` wrappers over the image effect suite (`ofxImage.h`, `ofxClip.h`); `ImageEffect`, `ActionArgs` and `ImageMemory` over an effect handle (`ofxEffect.h`); `ParamSet` and one class per parameter type (`ofxParam.h`); wrappers over the message, progress, memory, multithread and timeline suites (`ofxMessage.h`, `ofxProgress.h`, `ofxMemory.h`, `ofxMultiThread.h`, `ofxTimeLine.h`); `Interact` and the `InteractPlugin` dispatcher for an overlay (`ofxInteract.h`) with `Draw` over the OFX 1.5 draw suite (`ofxDraw.h`); the `ImageEffectPlugin` action dispatcher and its `PluginEntry` and `PluginEntries` boilerplate (`ofxPluginBase.h`); and the generated per-property-set accessor classes (`openfx::plugin::propsets`: getters for host-written properties, setters for plugin-written ones). |
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
  gives that property and each fluent, so they chain. A property an older host
  may not have, such as the plugin description, goes through `soft()` (see
  [Missing properties](#missing-properties)).
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

### Several plugins, and a main entry of your own

`PluginEntries` answers the two exported functions for a binary holding
several plugins, in the order listed. Each class keeps its own instance,
`setHost` and main entry, as with `PluginEntry`:

```cpp
using Entries = PluginEntries<Blur, Sharpen, Gain>;
int OfxGetNumberOfPlugins(void) { return Entries::numberOfPlugins(); }
OfxPlugin* OfxGetPlugin(int nth) { return Entries::get(nth); }
```

A plugin moving from C one action at a time keeps its own main entry and hands
`dispatch()` the actions it has moved. `dispatch()` fetches the suites in the
Load action, so a main entry that answers Load itself calls
`ImageEffectPlugin::fetchSuites(host, suites)` there. That fetches every
suite the bindings know about that the host has, and fails with
`kOfxStatErrMissingHostFeature` if the property, image effect or parameter
suite is missing.

```cpp
OfxStatus mainEntry(const char* action, const void* handle,
                    OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
  if (std::strcmp(action, kOfxActionLoad) == 0)
    return ImageEffectPlugin::fetchSuites(gHost, gPlugin.suites);
  if (std::strcmp(action, kOfxActionDescribe) == 0)
    return gPlugin.dispatch(action, handle, inArgs, outArgs);
  return legacyMainEntry(action, handle, inArgs, outArgs);
}
```

An action `ImageEffectPlugin` has no virtual for --- the OpenGL context
actions, an action of the host's own, one added to OpenFX after the bindings
--- reaches `otherAction(action, handle, inArgs, outArgs)` exactly as the host
passed it. Its default answers `kOfxStatReplyDefault`.

### Overlays

`InteractPlugin` is to an overlay what `ImageEffectPlugin` is to an effect:
a virtual per interact action (`describe`, `createInstance`,
`destroyInstance`, `draw`, `penDown`, `penMotion`, `penUp`, `keyDown`,
`keyUp`, `keyRepeat`, `gainFocus`, `loseFocus`), each given an `Interact`
and, for the events, the action's `ActionArgs`, and `otherAction()` for the
rest. `Draw` wraps the OFX 1.5 draw suite.

The host calls an overlay through an entry point that is given only the
interact handle, so the overlay object keeps copies of the suites it needs.
`entryPoint(suites)` gives them to `instance()`, the object the entry point
dispatches to, and returns the entry point to put on the effect descriptor. A
plugin that puts `mainEntry` on the descriptor itself, or dispatches to
overlay objects of its own, gives each object its suites with
`setSuites(suites)`; one without them answers every action it has a virtual
for with `kOfxStatErrMissingHostFeature`.

```cpp
class Crosshair : public InteractPlugin<Crosshair> {
 protected:
  OfxStatus createInstance(Interact& interact) override {
    interact.slaveToParams({"centre", "size"});
    return kOfxStatOK;
  }
  OfxStatus draw(Interact& interact, ActionArgs& in) override {
    Draw draw(in, suites());
    draw.setColour(draw.getColour(kOfxStandardColourOverlayActive));
    draw.drawLine({0, 0}, {100, 100});
    return kOfxStatOK;
  }
};

// in the effect's describe():
effect.descriptor().setOverlayInteractV2(Crosshair::entryPoint(suites));
```

`Interact::slaveToParam(name)` adds a parameter to
`kOfxInteractPropSlaveToParam`, after those already there, and
`slaveToParams({...})` adds several; the host redraws the overlay when any of
them changes. `Interact::effect()` is the effect the overlay belongs to, for
its parameters and clips.

### What the host reports

Where the specification gives a status a meaning, the wrappers keep it:

- A clip lookup, `effect.clip(name)` or a `Clip` built by name, throws
  `ClipNotFoundException` with the host's status, or with
  `kOfxStatErrBadHandle` if the host answered `kOfxStatOK` without the clip.
  `defineClip()` throws `OfxException` with the host's status.
- `Clip::getImage()` returns an empty `Image` when the host answers
  `kOfxStatFailed`, which the specification gives as the clip having no image
  at that time or over that region, to be treated as transparent black. Any
  other failure throws `ImageNotFoundException`. Test `if (image)` before
  reading one.
- `params.get<DoubleParam>(name)`, and each typed parameter's constructor,
  compare the parameter's type with the class's `kParamType` and throw
  `OfxException` with `kOfxStatErrValue` if they differ, since the parameter
  suite's varargs calls would otherwise write the wrong number of values.
- `Progress::update()` says to go on only for `kOfxStatOK` and
  `kOfxStatReplyYes`. `lastStatus()` then tells the user's cancel
  (`kOfxStatReplyNo`) from an error.
- An action that throws answers with the exception's status (see
  [Exceptions and the C boundary](#exceptions-and-the-c-boundary)).

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

`ImageEffect` needs the parameter suite only for `params()`, and `Interact`
the image effect and parameter suites only for `effect()`, so those may be
left out.

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
value, `findRaw` for one that may be missing, `getRawN` and `setRawN` for
several (`propGetIntN` and the rest), `getDimensionRaw`, `reset` (`propReset`)
and `exists`. For a call it does not wrap, `suite()` and `handle()` give the
suite and the property set:

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

The host side is a set of building blocks, which a host uses piecemeal: a
property store with the property suite over it, plugin loading, the image
effect model with the image effect and parameter suites over it, overlays with
the interact and draw suites, and the generic suites. Each object is what its
C handle points to and each suite is a plain C struct, so a host can take one
block and write its own in place of the next (see
[Taking the host side apart](#taking-the-host-side-apart)), and call C
wherever it likes. Policy --- pixel storage, the formats to negotiate,
threading, the UI --- stays with the host. `TestHost/` is a complete worked
example in about the size of a weekend project; `TestHost/README.md` and
`TestHost/DESIGN.md` explain its choices.

A host built on all of the blocks supplies five things:

1. **A `Host`.** Derive from `openfx::host::Host`, fill its property set
   through the generated `accessor()` (`setName`, `setLabel`,
   `setSupportedPixelDepths`, `setSupportedContexts`, the GPU-support flags,
   the colour management style), and register the suites it offers:
   `PropertySet::suite()`, `effectSuite()`, `paramSuite()`, `interactSuite()`
   and `drawSuite()` for overlays, and `addDefaultSuites(suites())` for
   memory, multithread, message, progress and timeline. `host.ofx()` is what
   a plugin is handed through `setHost`.
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
   them. The descriptors, the parameters with their animation, and the
   actions with their argument property sets come from `openfx::host`.
4. **Image storage.** Derive from `openfx::host::Image`, attach the pixel
   buffer, and write the properties the plugin reads through the generated
   `openfx::host::propsets::Image` setters (`setData`, `setBounds`,
   `setRowBytes`, `setRegionOfDefinition`, `setRenderScale` and the rest).
   `fetchImage()` returns the image with its `clip` set.
5. **The render sequence.** The host decides what to call and when:
   GetRegionOfDefinition, GetRegionsOfInterest, GetFramesNeeded, IsIdentity,
   BeginSequenceRender, Render per tile, EndSequenceRender. `EffectInstance`
   has a driver per action, which builds the action's arguments, sends it,
   and reads back the answer.

### Driving the actions

`Plugin::load(OfxHost*)` calls `setHost` and the Load action, once, with an
`OfxHost` the host built itself or a `Host`'s (`load(Host&)` passes
`host.ofx()`). `describe()` and `describeInContext()` follow it.

What a driver makes of the status the plugin answers with:

- A driver that returns an answer --- `regionOfDefinition()`,
  `getRegionsOfInterest()`, `getFramesNeeded()`, `getTimeDomain()`,
  `getOutputColourspace()`, `queryClipPreferences()` --- returns what the
  plugin wrote on `kOfxStatOK`, and the specification's default on
  `kOfxStatReplyDefault`. Where the specification gives a default --- the
  region of definition, each clip's region of interest and frames needed,
  each clip preference --- the driver writes it into the out-args first, so a
  value the plugin leaves alone comes back as that default. `kOfxStatOK` with
  nothing written to the out-args breaks the specification, which has the
  plugin answer `kOfxStatReplyDefault` for that; the driver logs a warning
  and takes it as `kOfxStatReplyDefault`. Any other status is an error, not
  a request for the default, and the driver throws `OfxException`, whose
  `code()` is the status.
- `isIdentity(time, window, renderScale, field)` returns an `Identity`: the
  plugin's `status`, and on `kOfxStatOK` the `clip` to copy and the `time` to
  copy it at, which the plugin may have moved from the time asked about.
  `isIdentity()` on it is true when there is a clip to copy.
  `kOfxStatReplyDefault` means render; any other status is an error, after
  which the host does not render either.
- `Plugin::load()`, `describe()`, `describeInContext()`,
  `EffectInstance::create()` and `describeOverlay()` throw `OfxException`
  unless the plugin succeeds, since nothing can follow.
- Every other driver returns the status. `paramChanged()` sends
  BeginInstanceChanged, InstanceChanged and EndInstanceChanged whatever each
  answers, and returns the first failure, else InstanceChanged's status.

`actionSucceeded(status)` is true for `kOfxStatOK` and
`kOfxStatReplyDefault`, and `requireSuccess(status, what)` throws
`OfxException` for anything else, for a host that cannot go on after a failed
action.

`regionOfDefinition(time, renderScale)` tells the plugin the render scale the
host is about to render at, `{1, 1}` by default; the region comes back in
canonical coordinates. While a GetRegionOfDefinition is in flight for an
instance, `regionOfDefinitionInFlight()` is true, and the default
`clipRegionOfDefinition()` has no region for the output clip rather than ask
the plugin again.

GetClipPreferences is taken in two steps. `queryClipPreferences()` returns the
plugin's answer as a `ClipPreferences` --- a `ClipPreference` per clip, the
output's premultiplication if the plugin changed it, the timing, the
frame-varying flag and the raw `outArgs` --- or nothing if the plugin replied
with the default, and applies none of it. `applyClipPreferences()` writes an
answer onto the clips and returns whether anything changed. In between, a
host changes what it cannot do. `getClipPreferences()` does both.

```cpp
if (auto answer = instance.queryClipPreferences()) {
  if (!supportsMultipleClipDepths)
    for (auto& [name, clip] : answer->clips)
      clip.depth = openfx::PixelDepth::Float;
  instance.applyClipPreferences(*answer);
}
```

Every action an `EffectInstance` sends goes through `action()`, between
`beforeAction(action, inArgs, outArgs)` and `afterAction(action, inArgs,
outArgs, status)`. They see the argument sets the driver built, or null where
the action has none. `beforeAction()` may add a property the driver does not
write --- one of the host's own, or a newer one such as
`kOfxImageEffectPropCudaStream` on Render --- and `define()` one of its own
in the out-args for the plugin to write; `afterAction()` may read back what
the plugin wrote that the driver ignores. `action()` also records a
successful CreateInstance and DestroyInstance, however they were sent, so the
destructor destroys only an instance the plugin still has. `InteractInstance`
has the same pair around the actions it sends.

```cpp
class MyInstance : public openfx::host::EffectInstance {
  // ...
 protected:
  void beforeAction(const char* action, openfx::host::PropertySet* inArgs,
                    openfx::host::PropertySet*) override {
    if (std::string_view(action) == kOfxImageEffectActionRender)
      inArgs->set(kOfxImageEffectPropCudaStream, 0, cudaStream_);
  }
};
```

`EffectBase::currentTime()` is the time `paramGetValue` and `paramSetValue`
use for the effect's parameters: the default timeline's current time, unless
a host with a time per viewer or per instance overrides it.
`EffectBase::paramSetHandle()` is the parameter set `getParamSet` hands the
plugin. The table below says what goes with overriding either.

### What the suites answer

The suites in `openfx::host` answer a bad call with the status the
specification gives:

- A null handle, or a null pointer to return a value through where the
  specification does not make it optional, is `kOfxStatErrBadHandle`. A null
  array of values for one of the property suite's `propSet*N` calls is
  `kOfxStatErrValue`.
- The property suite answers `kOfxStatErrUnknown` for a read of a property
  the set does not have, and for a write of one that is neither defined in
  the set or its parent nor known to the metadata, which it also warns about.
  A value of the wrong type is `kOfxStatErrValue`, and an index out of range
  `kOfxStatErrBadIndex`. Host code is not held to this: `PropertySet::define()`
  and `set()` create whatever they are given, so a property of the host's own
  that a plugin is to write is `define()`d first.
- `paramDefine` of an unknown parameter type is `kOfxStatErrUnknown`, and of
  a name already defined `kOfxStatErrExists`.
- `multiThread` called from one of its own threads is `kOfxStatErrExists`,
  and one whose thread function throws returns `kOfxStatFailed`.
- `clipGetImage` of a clip with nothing to give, and
  `clipGetRegionOfDefinition` of one with no region, are `kOfxStatFailed`.
- A draw suite call outside a Draw action is `kOfxStatFailed`, and a value
  that is no primitive `kOfxStatErrValue`.

### Taking the host side apart

Each piece of `openfx/host/` is a header of its own, and a host takes the
pieces it wants. What each needs from the others, and what a host that
replaces it provides instead:

| Piece | Needs | A host that replaces it provides |
|---|---|---|
| `PropertySet` (`ofxPropertySet.h`) | The generated metadata, nothing else. | An `OfxPropertySuiteV1` of its own. Every other piece keeps its properties in `PropertySet`s and hands the plugin their handles, so that suite passes the handles it did not make to `PropertySet::suite()`'s entries. |
| The default suites (`ofxDefaultSuites.h`) | `SuiteContainer`, to register them. | Its own suite under the same name and version, added instead of, or after, `addDefaultSuites()`. The timeline is the only one read elsewhere (see below). |
| `Host` (`ofxHost.h`) | `PropertySet`, `SuiteContainer`. | An `OfxHost` of its own, whose `host` is a property set its property suite reads and whose `fetchSuite` finds its suites, given to `Plugin::load(OfxHost*)`. |
| `PluginBinary` (`ofxPluginBinary.h`) | Nothing. | The `OfxPlugin*` from a loader of its own, or from a plugin linked in, given to `Plugin(OfxPlugin*, bundlePath)`. |
| `Plugin` (`ofxPlugin.h`) | An `OfxHost*`, for `setHost`. | Its own calls to `setHost` and `mainEntry`. `EffectDescriptor`, `EffectInstance` and `InteractDescriptor` send their actions through a `Plugin&`, so it replaces those too. |
| `EffectDescriptor`, `EffectInstance` (`ofxEffect.h`) | `Plugin`, `PropertySet`, `ParamSet`, `Clip` and `Image`, and the timeline for `currentTime()`. | Its own `OfxImageEffectSuiteV1`, since `effectSuite()` takes every effect handle for an `EffectBase`, and its own overlays, since `InteractInstance` takes an `EffectInstance`. `Plugin::describe()` and `describeInContext()` make `EffectDescriptor`s; a host that keeps them has its suite pass their handles to `effectSuite()`'s entries. |
| `Param`, `ParamSet` (`ofxEffect.h`) | `PropertySet`, and the owning effect's `currentTime()`. | An override of `EffectBase::paramSetHandle()` on its instances, giving the plugin its own parameter set, and its own `OfxParameterSuiteV1`, which passes the handles it did not make to `paramSuite()`'s entries. Describing stays with `Param`: the plugin defines them on the descriptor, and the host builds its own parameters from their property sets. |
| `Clip`, `Image` (`ofxEffect.h`) | `PropertySet` (an `Image` is one), and the clip's `owner`, an `EffectInstance`. | Nothing: `effectSuite()` takes every clip and image handle for these, so a host derives from them instead. `makeClip()` makes its clips, `fetchImage()` returns its images with `clip` set, and `releaseImage()` takes them back. |
| The timeline and `currentTime()` (`ofxDefaultSuites.h`, `ofxEffect.h`) | `timeline()`, the state behind the default timeline suite. | Its own `OfxTimeLineSuiteV1`, and an override of `EffectBase::currentTime()` giving the same time, so that `paramGetValue` and `paramSetValue` work at the time the plugin reads from `getTime`. |
| `InteractDescriptor`, `InteractInstance` (`ofxInteract.h`) | `Plugin`, `EffectDescriptor` (where the overlay's entry point is), `EffectInstance`, `PropertySet`, and a `DrawContext` for `draw()`. | Its own `OfxInteractSuiteV1`, since `interactSuite()` takes every interact handle for an `InteractBase`, and its own calls to the entry point `overlayEntryPoint()` finds. |
| `DrawContext` (`ofxDrawSuiteHost.h`) | Nothing. | Its own `OfxDrawSuiteV1`. `InteractInstance::draw()` takes a `DrawContext`, so such a host sends Draw through `InteractDescriptor::call()`, with in-args it builds itself. |

Most hosts keep the pieces and change what they do through their virtuals:

- `EffectInstance`: `fetchImage()`, `releaseImage()` and `clipProperties()`,
  which a host must implement; `makeClip()`, `clipRegionOfDefinition()` and
  `abort()`; `currentTime()` and `paramSetHandle()`, from `EffectBase`; and
  `beforeAction()` and `afterAction()`, which see every action `action()`
  sends, with the argument sets the driver built.
- `InteractInstance`: `beforeAction()` and `afterAction()` around every
  action it sends, and `redrawRequested()` and `buffersSwapped()`, which
  `interactRedraw` and `interactSwapBuffers` call.
- `DrawContext`: `standardColour()`, for `getColour`, and an `on...()`
  virtual for each of the other draw suite calls, which a host must
  implement; and `onOpen()`.

`DrawContext` is the object behind `OfxDrawContextHandle`, and it is an
interface, not a renderer. It keeps the specification's rules --- calls only
while a Draw action is open, argument checks, the colour, line width and
stipple a plugin reads back --- and hands the drawing to those virtuals. The
headers carry no test-host code. The test host's own pieces live in
`TestHost/src/`: `RecordingDrawContext` (`DrawRecorder.h`), a `DrawContext`
that records each call instead of drawing and can rasterise the record;
`testhost::EffectInstance`, `TestClip` and `TestImage` (`Effect.h`), which
hold its pixel buffers; `Overlay` and `CountingInteractInstance`
(`Interact.h`), which script an overlay session; and `Host.cpp`, its identity
and the suites it registers.

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

MSVC's `/EHsc` is fine for both. Its `c` lets the compiler assume that a
function *declared* `extern "C"` never throws, and drop a `catch` around a
direct call to one. The host side calls a plugin only through the function
pointers the plugin gives it, and MSVC keeps the `catch` around such a call.
Under `/EHsc`, don't count on a `catch` around a direct call to an
`extern "C"` function.

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

## Property access

`PropertyAccessor` (`ofxPropsAccess.h`) reads and writes one property set
through the C property suite. `get<id>()`, `set<id>()`, `getAll<id>()`,
`setAll<id>()` and the `getPointD<id>()`, `getRectD<id>()` family take a
`PropId`, which gives the property's name, type and dimension from the
metadata, so a wrong name or type does not compile. A property of more than
one type takes the type to use as well:
`get<PropId::OfxParamPropDefault, double>()`. The raw calls reach any
property by name (see
[Mixing the wrappers with C calls](#mixing-the-wrappers-with-c-calls)).

`exists<id>()` and `exists(name)` ask the property set, at run time, whether
it has the property. `prop::exists<id>()` is something else: a compile-time
constant, always true, that says nothing about any property set.

Every call is strict. A failed call throws `PropertyNotFoundException` for a
property the set does not have (`kOfxStatErrUnknown`), and `OfxException`
with the suite's status for any other failure; the message gives the property
and the status. Nothing is logged first: the exception is the one report of
the failure. For a property that may be missing, see
[Missing properties](#missing-properties).

### Strings

A string or enum property comes back from `get<id>()`, `getAll<id>()` and the
generated getters as an `openfx::CStringView` (`ofxCStringView.h`), or a
`std::vector` or `std::array` of them. The property suite hands out a
`const char*`, and `==` on two of those compares addresses, so
`image.pixelDepth() == kOfxBitDepthFloat` could be false for the same text.
`CStringView`'s `==` and `!=` compare the characters, against a
`const char*`, a `std::string_view`, a `std::string` or another `CStringView`,
and two of them order by content, so a sort or a `std::set` does too.

It converts implicitly to `const char*` and to `std::string_view`, so it goes
to a C call or a `string_view` parameter as it is; `std::string(s)` copies it,
and `<<` streams it, so `openfx::format()` and the `Logger` take it as an
argument. It is never null: a null from the host reads as `""`. For the plain
`const char*`, `c_str()` gives it, which printf and other varargs need, since
they take no class type. The raw calls and the multi-type getter return the
type the caller asks for, so `getRaw<const char*>(name)` and
`get<id, const char*>()` give a `const char*`. A setter takes a `const char*`,
which a `CStringView` converts to.

```cpp
CStringView depth = image.pixelDepth();
if (depth == kOfxBitDepthFloat)           // compares the text
  std::printf("%s\n", depth.c_str());     // varargs take the pointer
std::string kept(depth);                  // a copy that outlives the image
```

`CStringView` owns nothing. The characters belong to the host, and last until
the property is next written or its set goes; copy them into a `std::string`
to keep them longer.

### Missing properties

Every call is strict about a property the set does not have, including one
the specification lets a host or a plugin leave out: it throws
`PropertyNotFoundException`. The generated headers mark such a property in the
comment above its methods, so an author can see where one may be missing:

```cpp
// kOfxImageEffectPropCPURenderSupported (optional)
```

For a property that may be missing, there are two ways to ask.

`soft()` gives a copy of a `PropertyAccessor`, or of a generated `propsets`
class, of the same type, which forgives a property the set does not have and
nothing else. A write or reset of one does nothing, and a read of one returns
the fallback for its type, the same from every getter:

| Type | Fallback |
|---|---|
| `int`, `bool` | `0`, `false` |
| `double` | `0.0` |
| a string | `""`, never null: an empty `CStringView`, or a static empty string from `getRaw<const char*>()` |
| `void*` | `nullptr` |
| a dimension | `0`, so a soft `getAll()` of a variable-dimension property gives no values |

The copy chains as the original does:

```cpp
bool overlays = host.soft().supportsOverlays();       // false on a host without it
desc.soft().setPluginDescription("Gain").setVersionLabel("1.0");
CStringView space = props.soft().get<PropId::OfxImageClipPropColourspace>();
int pass = props.soft().getRaw<int>("com.example.Pass");
```

Any other failure throws from a soft copy as from any other: a bad handle, an
index past the end, a value of the wrong type.

`find<id>(index)` and `findRaw<T>(name, index)`, on `PropertyAccessor`, return
a `std::optional`: `std::nullopt` for a property the set does not have, to
tell it from one that holds the fallback's value, and a throw for any other
failure, as from `get`. The generated classes have no `find`; their `props()`
is the `PropertyAccessor` to ask.

```cpp
if (auto path = props.find<myhost::PropId::MyHostProjectPath>())
  openProject(*path);   // MyHost's own property; another host has none
```

Strict suits a property the set must have, whose absence is a bug in one
side or the other. `soft()` suits one whose absence reads as its fallback: a
host capability an older host lacks, a property a plugin need not set, a
host's own property read in another host. `find()` suits one whose absence
means something the fallback cannot say.

## Suites

`SuiteContainer` (`ofxSuites.h`) holds suite pointers by name and version.
`get<SuiteType>()` and `has<SuiteType>()` find a suite by its C struct type,
and `get<T>(name, version)` and `has(name, version)` by name. A suite type the
headers do not know --- a host's own, or one newer than the headers --- is
registered once, at global scope, before any code that looks it up:

```cpp
OPENFX_DEFINE_SUITE(MyHostWidgetSuiteV1, kMyHostWidgetSuite, 1);
```

after which `get<MyHostWidgetSuiteV1>()` and `has<MyHostWidgetSuiteV1>()`
find it like any other. `get<T>()` of a suite type never registered does not
compile. A plugin's `ImageEffectPlugin::suites` is filled in the Load action
by `fetchSuites()`; a host's `Host::suites()` is what its `fetchSuite` looks
in.

## Generated metadata

`ofxPropsMetadata.h`, `ofxPropsBySet.h` and both `ofxPropSetAccessors.h` are
generated from the `@propdef`, `@propset` and `@actiondef` blocks in
`include/*.h`, the same blocks the property reference documentation comes
from. Do not edit them by hand; edit the metadata in the C headers and
regenerate:

```sh
uvx pcons run gen-props          # or: python scripts/gen-props.py
```

`Documentation/README.md` documents the metadata block format.

An accessor method takes its name from the property's name, less the `Ofx`,
the `Prop` and the kind of object the class is for: `kOfxImageClipPropConnected`
is `connected()`, and `kOfxImageEffectPropSupportedPixelDepths` is
`supportedPixelDepths()` and `setSupportedPixelDepths()`. Any other qualifier
stays, as OpenGL does in `setOpenGLPixelDepth()` for `kOfxOpenGLPropPixelDepth`,
and so does the object when two properties of a set would otherwise share a
name (`type()` and `paramType()`). The name is the property's string value,
which the specification fixes; in a few properties it differs from the
`#define`, and the string value wins: `kOfxImageEffectPropProjectPixelAspectRatio`
is `"OfxImageEffectPropPixelAspectRatio"`, so its methods are
`pixelAspectRatio()` and `setPixelAspectRatio()`. In the generated headers,
each property's methods follow a comment giving its `#define`, and the string
value where the two differ, so a search for either finds them, and ending in
"(optional)" where the metadata lets the set leave the property out
(see [Missing properties](#missing-properties)). A property of variable
dimension has a getter taking an index and one for every value:
`supportedContexts(i)` and `supportedContextsAll()`, or for a property of
more than one type, `defaultValue<double>(i)` and `defaultValueAll<double>()`.

A host that defines properties of its own lists them in a YAML file and
generates their metadata in its own namespace with
`gen-props.py host-metadata`, so `PropertyAccessor` reads them by `PropId` as
it does OpenFX's own: `props.get<myhost::PropId::MyHostViewerProcess>()`.
Another host has none of them, so a plugin reads them through `soft()` or
`find()`. The generated header also gives each property a C name, as the OFX
headers do:

```cpp
#define kMyHostViewerProcess "com.example.myhost.ViewerProcess"
```

Each is guarded by `#ifndef`, so the host's own C header may define it first,
and a `static_assert` checks that the two agree; C code passes the same name
to the property suite. `../../examples/host-specific-props/` shows how.

## Logging

`openfx/ofxLog.h` is a small thread-safe logger with `{}`-style formatting:
`Logger::debug/info/warn/error`, a settable level, and a settable handler for
sending messages somewhere else. `Logger::setLevel(Logger::Level::Off)`
silences it. A message below the level is not formatted, and a log call never
throws, so logging is safe at a C boundary. `README-logging.md` has the
details.

## Testing

The CMake build compiles the unit tests in `openfx-cpp/tests`, the test host
and, with the example plugins, the plugins written on the bindings, and
registers their tests with CTest:

```sh
scripts/build-cmake.sh -G Ninja Release      # build everything (see install.md)
ctest --test-dir build/Release --output-on-failure
ctest --test-dir build/Release -L openfx-cpp # only the unit tests
ctest --test-dir build/Release -L host       # only the host against the plugins
```

The unit tests are built twice, at C++20 and at C++17 with tcb-span standing
in for `std::span`, and each source file is a test of its own at each
standard: `openfx-cpp.NAME` and `openfx-cpp.cxx17.NAME`. The `host.*` tests
run the test host against the example and Support plugin bundles, which the
build lays out in `build/Release/plugins`. `OFX_BUILD_OPENFX_CPP_TESTS` (on by
default) builds the unit tests and the host; the host tests also need
`BUILD_EXAMPLE_PLUGINS`, which the script turns on. `OFX_BUILD_OPENFX_CPP_CHECK`
(on by default) adds an `openfx-cpp-check` target that compiles every header,
on each side, at C++17 and at C++20, along with the minimal plugin.

pcons builds and runs the same tests, and adds clang-tidy and sanitizer
builds:

```sh
uvx pcons BUILD_PLUGINS=1                # the test host and the plugin bundles
uvx pcons -B build/pcons/release test    # the unit tests, and the host against the bundles
uvx pcons -B build/pcons/tidy CLANG_TIDY=1      # clang-tidy alongside every compile
uvx pcons -B build/pcons/asan SANITIZE=1 test   # ASan and UBSan
```

`TestHost/fuzz.py` drives the test host over randomised host choices to shake
out assumptions in a plugin.

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
these headers prevents it. A suite the headers do not know is registered with
`OPENFX_DEFINE_SUITE` (see [Suites](#suites)).

-------------
Copyright OpenFX and contributors to the OpenFX project.

`SPDX-License-Identifier: BSD-3-Clause`
