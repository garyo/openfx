<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- Copyright OpenFX and contributors to the OpenFX project. -->
# ofxtesthost: a command-line OpenFX host for testing plugins

`ofxtesthost` loads OpenFX image effect plugins, describes them, sets their
parameters, renders an image through one plugin or a chain of them, and can
check pixels of the result. It is a small, modern C++ host written directly
against the OFX C API and the [openfx-cpp](../openfx-cpp) type-safe property
accessors; it does not use the legacy `HostSupport` library.

It is a development tool, not a reference host: it renders one frame at a
time on the CPU, at render scale 1, with no animation, fields, tiling, GPU
suites or interacts.

## Building

The host is part of the pcons build (see [install.md](../install.md)):

```sh
uvx pcons                     # builds build/pcons/release/ofxtesthost
uvx pcons BUILD_PLUGINS=1     # ...and the example and Support plugin bundles
uvx pcons BUILD_PLUGINS=1 test   # runs the host against the bundles (pcons test -L host)
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
constant colour (`--fill`), or a ramp (`--ramp`, the default). Outputs are
PPM (8-bit) or PFM (float) by extension. `--expect X,Y,R,G,B,A[,TOL]` makes
the exit status reflect a pixel check, which is how the pcons tests work.
`--verbose` traces every action and its status, image fetches and releases,
and property-set anomalies such as a plugin writing a property with the wrong
type or index.

If a plugin crashes, the host reports the plugin and the action it was
handling, with a backtrace.

## What the host does with a plugin

1. Loads the binary, calls `setHost` and the Load action.
2. Describe, then DescribeInContext for the filter context if the plugin
   supports it, else general, else generator (`--context` overrides).
3. Creates an instance, applies `--param` values (with the
   BeginInstanceChanged / InstanceChanged / EndInstanceChanged actions), and
   calls GetClipPreferences.
4. Connects the source image to the `Source` clip (or the first non-mask input).
5. Renders: GetRegionOfDefinition, IsIdentity, BeginSequenceRender, Render,
   EndSequenceRender. The render window is the region of definition clipped
   to the project, so an infinite region works. Pixel depth is the first of
   float, byte, short the plugin supports; components are the first of RGBA,
   RGB, alpha each clip supports. Input images are converted to match.
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
through the generated `openfx::propsets` accessor classes, so a property name
or type mistake in the host is a compile error. An instance's property set
falls back to its descriptor's for properties it does not define, so a value
set in Describe is visible on the instance.

Handles are pointers to the host's own objects (`PropertySet`, `Clip`,
`Param`, `ParamSet`, `EffectDescriptor`, `EffectInstance`, `Image`), cast to
and from the opaque OFX handle types.

Properties a plugin sets that the metadata does not declare are created on
first write and reported under `--verbose`; that is how the host found the
metadata errors it has fixed so far.
