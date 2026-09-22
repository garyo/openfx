<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- Copyright OpenFX and contributors to the OpenFX project. -->
# ofxtesthost: a command-line OpenFX host for testing plugins

`ofxtesthost` loads OpenFX image effect plugins, describes them, sets their
parameters, renders an image through one plugin or a chain of them, and can
check pixels of the result. It is a small, modern C++ host built on the
header-only host framework in [openfx-cpp](../openfx-cpp) -- its property
store, plugin loading, effect model and suites -- and it does not use the
legacy `HostSupport` library. What is left here is the part a host decides
for itself: pixel buffers, the formats to negotiate, and the test tooling.

It is a development tool, not a reference host: it renders one frame at a
time on the CPU, at render scale 1, with no animation, fields, tiling, GPU
suites or interacts.

## Building

The host is part of the pcons build (see [install.md](../install.md)):

```sh
uvx pcons                     # builds build/pcons/release/ofxtesthost
uvx pcons BUILD_PLUGINS=1     # ...and the example and Support plugin bundles
uvx pcons -B build/pcons/release test   # runs the host against the bundles
```

It needs a C++20 compiler. Plugins are loaded from `.ofx.bundle` directories,
bare `.ofx` binaries, or a directory of bundles such as
`build/pcons/release/plugins`.

## Usage

```sh
ofxtesthost build/pcons/release/plugins --list

ofxtesthost build/pcons/release/plugins/example-Basic.ofx.bundle \
    --describe --param scale=2 --fill 0.25,0.25,0.25,1 \
    --expect 5,5,0.5,0.5,0.5,2 --out gain.ppm

# A chain: gain, then invert. --param applies to the most recent --plugin.
ofxtesthost build/pcons/release/plugins \
    --plugin net.sf.openfx.basicPlugin --param scale=2 \
    --plugin net.sf.openfx.invertPlugin \
    --in photo.ppm --out out.pfm
```

`ofxtesthost --help` lists every option. Inputs are P6 PPM or PFM files, a
constant colour (`--fill`), or a ramp (`--ramp`, the default); that image
goes to the effect's main input, and `--clip NAME=SOURCE` attaches an image
to any other clip, such as a mask (`--clip Mask=fill:0,0,0,0.5`,
`--clip Matte=matte.pfm`, `--clip Aux=input`). Outputs are PPM (8-bit) or
PFM (float) by extension. `--expect X,Y,R,G,B,A[,TOL]` makes
the exit status reflect a pixel check, which is how the pcons tests work.
A string-choice value that is not one of the plugin's declared enums is
replaced by the parameter's default, with a warning, as the parameter
reference recommends for a project saved with a since-removed option.
`--verbose` traces every action and its status, image fetches and releases,
and property-set anomalies such as a plugin writing a property with the wrong
type or index.

If a plugin crashes, the host reports the plugin and the action it was
handling, with a backtrace. Image buffers carry guard bytes, so a plugin that
writes outside an image is reported after the render rather than corrupting
the heap silently.

## Fuzzing

Several host choices are options because the spec leaves them open and
plugins tend to assume one answer: `--depth`, `--components`, `--origin` (the
input's bounds and the project offset), `--row-padding` (row stride larger
than the pixels), `--renders` (re-rendering an instance), and `--time`.
`--randomize SEED` picks all of those plus image size, parameter values
within each parameter's declared range (and occasionally at its hard limits
or out of range), optional clip connections and the context, and prints the
equivalent explicit command line as `repro:` before rendering.

`fuzz.py` drives that over many seeds, one process per run so a crash is
just a finding, and reports each distinct failure with the shortest command
line that still reproduces it:

```sh
uv run TestHost/fuzz.py build/pcons/release/ofxtesthost build/pcons/release/plugins --runs 100
uv run TestHost/fuzz.py build/pcons/release/ofxtesthost my.ofx.bundle --plugin com.example.Effect --warnings
```

Findings are ranked crash (a signal during an action), error (a failed
action), warning (an unreleased image, an out-of-bounds write, a property
written with the wrong type or index, non-finite output) and timeout. A
plugin that fails without any randomisation is reported once and not fuzzed.

A plugin built with AddressSanitizer needs the sanitizer runtime loaded
into the host process before the plugin, which on macOS means launching the
host with `DYLD_INSERT_LIBRARIES` pointing at `libclang_rt.asan_osx_dynamic.dylib`
(the plugin prints the exact path when it is missing). The shell cannot
pass that variable through the Python interpreter, so give it to the
wrapper directly: `fuzz.py ... --env DYLD_INSERT_LIBRARIES=/path/to/libclang_rt.asan_osx_dynamic.dylib`.

## What the host does with a plugin

1. Loads the binary, calls `setHost` and the Load action.
2. Describe, then DescribeInContext for the filter context if the plugin
   supports it, else general, else generator (`--context` overrides).
3. Creates an instance, applies `--param` values (with the
   BeginInstanceChanged / InstanceChanged / EndInstanceChanged actions), and
   calls GetClipPreferences.
4. Connects the source image to the `Source` clip (or the first non-mask input)
   and any `--clip` images to their clips.
5. Renders: GetRegionOfDefinition, IsIdentity, BeginSequenceRender, Render,
   EndSequenceRender. The render window is the region of definition clipped
   to the project, so an infinite region works. Pixel depth is the first of
   float, byte, short the plugin supports; each clip gets the first component
   type it lists, unless `--components` names one it supports. Input images
   are converted to match.
6. In a chain, the output image becomes the next plugin's source.
7. Destroys the instance and unloads the plugin.

The host provides the property, image effect, parameter, memory, multithread
(real threads), message (v1 and v2), progress (v1 and v2) and timeline suites.

## Design notes

Every property set the plugin sees is built from the generated metadata in
`openfx-cpp/include/openfx/ofxPropsBySet.h`: the host looks up the property
set by name (`EffectDescriptor`, `ClipInstance`, `Image`, the parameter sets,
an action's `inArgs`...) and pre-defines every property with the type and
dimension the specification gives it. Host-written values are then set
through the generated `openfx::host::propsets` accessor classes, so a property name
or type mistake in the host is a compile error. An instance's property set
falls back to its descriptor's for properties it does not define, so a value
set in Describe is visible on the instance.

Handles are pointers to the host's own objects (`PropertySet`, `Clip`,
`Param`, `ParamSet`, `EffectDescriptor`, `EffectInstance`, `Image`), cast to
and from the opaque OFX handle types. Those classes are the framework's; this
host derives from `Clip`, `Image` and `EffectInstance` to attach its pixel
buffers and its own policy.

Properties a plugin sets that the metadata does not declare are created on
first write and reported under `--verbose`; that is how the host found the
metadata errors it has fixed so far.
