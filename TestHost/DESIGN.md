<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- Copyright OpenFX and contributors to the OpenFX project. -->
# ofxtesthost design notes

Reference notes on how the test host is put together, the decisions behind
it, and what it turned up while being built. `README.md` is the user-facing
description; this is the "why".

## Goals

- A host in modern C++ written directly against the OFX C API, not on
  `HostSupport`, which predates C++11 and carries a decade of accretion.
- Use the `openfx-cpp` type-safe property accessors for every property the
  host reads or writes, and the `Image`/`Clip` RAII wrappers where natural,
  so the host is a real consumer of those bindings.
- Exercise a plugin end to end: load, describe, describe-in-context, create
  an instance, set parameters, negotiate clip preferences, render a frame,
  read the result back, and chain several plugins.
- Be a test tool: assert pixel values, log what the plugin did wrong, and
  survive plugin misbehaviour with a useful report.

Non-goals, at least for now: animation and keyframes, tiled or scaled
rendering, fields, the GPU render suites, interacts, custom parameter
interpolation, and rendering more than one frame at a time.

## Layout

| File | Responsibility |
|---|---|
| `PropertySet.{h,cpp}` | Storage for one property set and the host's `OfxPropertySuiteV1`. |
| `Suites.{h,cpp}` | Memory, multithread, message, progress and timeline suites; `fetchSuite`. |
| `Plugin.{h,cpp}` | `Bundle` (dlopen and plugin enumeration), `Host` (the `OfxHost` struct and its property set), `Plugin` (main-entry calls, load/unload, describe). |
| `Effect.{h,cpp}` | `ImageBuffer`, `Image`, `Clip`, `Param`, `ParamSet`, `EffectDescriptor`, `EffectInstance`, and the image-effect and parameter suites. |
| `ImageIO.{h,cpp}` | PPM/PFM read and write, solid and ramp test images. |
| `main.cpp` | Command line, the driver loop, the crash handler. |

## Property sets from metadata

The central idea. `openfx-cpp/include/openfx/ofxPropsBySet.h` (generated
from the `@propset` and `@actiondef` blocks in the headers) lists every
property of every property set and every action's inArgs/outArgs, each with
its `PropDef` (type, dimension, enum values). `PropertySet(setName)` looks
the set up and pre-defines every single-typed property with the correct
storage type and dimension; `PropertySet::forAction(action, "inArgs")` does
the same for an action's arguments. Multi-typed properties such as
`OfxParamPropDefault` are created on first write with the writer's type.

Consequences:

- A plugin that reads a property the spec says the host must provide always
  finds it defined, with a zero/empty value at worst.
- Writes to properties the metadata does not declare succeed but are logged
  under `--verbose`. That is how the two wrong `@propdef`s (below) surfaced.
- Writes with the wrong type or an out-of-range index are refused with the
  spec status codes and logged, which catches plugin and metadata mistakes
  alike. Int and double are coerced both ways, since real hosts do.
- Host-written values are set through the generated `openfx::host::propsets`
  accessor classes (`ImageEffectHost`, `EffectInstance`, `ClipInstance`,
  `Image`, `ImageEffectActionRender_InArgs`, ...), so a wrong property name
  or type in the host is a compile error.

An instance's property set has a parent: reads of properties absent locally
fall through to the descriptor, and properties present in both sets are
seeded from the descriptor at construction. That is the inheritance rule
issue #177 documented, applied uniformly rather than for a hand-picked list.

Spec defaults that plugins rely on (parameter min/max/enabled/animates,
descriptor thread safety and tile support, clip field extraction, ...) are
set explicitly, following what `HostSupport` has always done, because the
metadata carries no defaults.

## Handles

An OFX handle is a pointer to the host's own object, cast through the opaque
handle type: `PropertySet*` for `OfxPropertySetHandle`, `EffectBase*`
(descriptor or instance) for `OfxImageEffectHandle`, `Clip*`, `Param*`,
`ParamSet*`, and a `MemoryBlock*` for image memory. `Image` derives from
`PropertySet`, so the property-set handle handed to a plugin for an image
*is* the image, and `clipReleaseImage` recovers it with a static downcast
rather than a side table.

## Rendering

For a frame: GetRegionOfDefinition, IsIdentity, BeginSequenceRender, Render,
EndSequenceRender. Two host responsibilities that real plugins depend on:

- The render window is the region of definition clipped to the project
  extent. A generator may declare an infinite region; the first attempt
  turned that into an integer overflow and a crash inside the plugin.
- A spatial parameter whose default is declared in normalised coordinates is
  scaled by the project size when the instance is created; the Rectangle
  example draws nothing sensible otherwise.

Pixel depth is the first of float, byte, short that the plugin supports;
components per clip are the first type the clip lists, unless `--components`
names a type the clip supports. The spec lets a host pick any supported type,
and hosts differ, so the knob exists to reproduce a particular host's choice:
a plugin that lists alpha first for its masks but only handles RGBA works on
an RGBA-preferring host and fails here by default.
Input buffers are converted lazily to the clip's negotiated format when the
plugin fetches them, so a chain of plugins with different depths works. If
IsIdentity names a clip, the host copies that clip's image and skips Render.

`--param` values are applied with BeginInstanceChanged / InstanceChanged /
EndInstanceChanged around each change, as a host must, so plugins that cache
state on change behave.

## Diagnostics

- `--verbose` logs every action with its status, every image fetch and
  release, suites the plugin asked for that the host lacks, and property
  anomalies.
- After Render the host reports any image handle the plugin did not release.
- A signal handler prints the plugin and action in flight plus a backtrace
  before re-raising. Plugins do crash, and "SIGSEGV during Render in
  net.sf.openfx.noisePlugin" with frames into the plugin is far more useful
  than exit code 139. It was how the infinite-RoD bug was found.

## Build

pcons builds the host as `ofxtesthost` in a C++20 clone of the C++17
environment; the openfx-cpp headers use `std::span` under C++20 and would
otherwise need the tcb-span Conan package. With `BUILD_PLUGINS=1` the build
also declares twelve `project.Test()` entries that run the host against the
freshly built bundles with `--expect` pixel checks. Tests execute in the
build directory, so their bundle paths are build-relative, and the host
target depends on the bundles so `pcons test` builds them first.

Because the project's build directory (`build/pcons/<variant>`) differs
from the CLI's default, `pcons test` must be told where the manifest is:
`pcons -B build/pcons/release test`. An explicit `-B` is honoured verbatim by
the build script; only the default is redirected under `build/pcons/`.

## Fuzzing

The randomiser lives in the host (`--randomize SEED`) because it needs the
descriptor: parameter ranges, choice options, optional clips and contexts
are only known after DescribeInContext. It prints the explicit equivalent
command line so findings never depend on the seed or the host version. The
campaign lives in `fuzz.py` because a plugin crash ends the process; the
wrapper runs one process per seed, classifies the outcome from the host's
output, establishes a no-randomisation baseline per plugin so a plugin the
host cannot drive at all is not mistaken for a fuzz finding, and shrinks each
distinct failure by dropping options one at a time while the failure
signature persists.

Two detectors matter more than the random inputs: the crash handler, which
attributes a signal to the plugin and action in flight, and the guard bytes
around every image buffer, which turn a plugin's out-of-bounds write into a
named warning after the render instead of a heap-corruption abort later in
the host.

## What building it found

Fixed on the same branch:

- `Examples/Invert/invert.cpp` never released its output image: a second
  declaration of the handle inside the try block shadowed the outer one.
- `kOfxParamPropChoiceEnum` was declared `bool x 1`; it is a string array.
- `kOfxParamPropDimensionLabel` was declared `x 1`; it is one label per
  dimension.
- The accessor generator emitted a getter named `default` (a keyword), and
  in every parameter class silently dropped the parameter-type accessor
  because `OfxParamPropType` and `OfxPropType` both shortened to `type`.
  Colliding names now keep their category prefix, and collisions are
  reported.
- The generated accessor classes collided with the `Image` and `Clip` RAII
  wrappers. The headers are now split by side: common code in `openfx/`,
  plugin-side in `openfx/plugin/` (`openfx::plugin`, accessors in
  `openfx::plugin::propsets`) and host-side in `openfx/host/`
  (`openfx::host`, accessors in `openfx::host::propsets`).
- The generated `ofxPropsMetadata.h` and accessor headers on main included
  `ofxPropsAccess.h` and `ofxSpan.h`, which existed only on the
  props-metadata branch; the support headers are now in the tree.
- Prefix stripping in the generator missed the host, instance, plugin and
  param-host property families, giving names like
  `setImageEffectHostPropIsBackground()`.

Found by fuzzing (`fuzz.py`, 30-60 seeds per plugin), not fixed here:

- The three ColourSpace example plugins crash on small images (a 107x2 or
  109x1 frame with default parameters) because `drawText` writes its label
  through the bottom of the image. The guard bytes catch the label overrun
  as an out-of-bounds write on a 101x114 frame before it becomes a crash.
  (They also crashed on a string-choice value outside the declared enums,
  but the parameter reference says a host should substitute the default in
  that case, so the host now does and the fuzzer no longer sends them.)
- GPUGain declares alpha output but refuses to render it.
- FLOSS2 (an external plugin): declares 8- and 16-bit support but builds
  float OpenCV matrices over the images, so any depth but float fails;
  rejects any row padding (OpenCV needs the stride to be a multiple of the
  element size, and a 1-byte pad is not); segfaults in the general context on
  a 16x91 frame with a large external blemish-mask blur and a mask attached;
  and intermittently aborts on a 3-pixel-wide 8-bit padded frame with skin
  creation on. Each report carries the minimal command line.

Observed but left alone:

- Several example plugins print through their own logging on load.

## Open questions

- Whether the host should require C++20 or the tree should pick up the
  tcb-span dependency so it can stay at C++17 with the rest of the build.
- Animation support: a keyframe map per parameter would make
  `paramGetValueAtTime`, derivatives and integrals real, and the timeline
  suite meaningful.
- A `--frames N` mode rendering a sequence, which would also exercise
  sequential rendering and `kOfxImageEffectFrameVarying`.
- Tiled rendering and render scale, to test plugins that claim tile support.
