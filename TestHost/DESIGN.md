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

Non-goals, at least for now: fields, the GPU render suites, OpenGL (V1)
overlay interacts, custom parameter interacts, custom parameter
interpolation, and converting pixels between colourspaces.

## Layout

Everything a host does the same way as every other host lives in the
framework, under `openfx-cpp/include/openfx/host/` (`namespace openfx::host`),
header-only: `PropertySet` (the metadata-driven property store and
`OfxPropertySuiteV1`), `PluginBinary` (loading a plugin binary or bundle and
enumerating its plugins, plus the standard plugin search paths), the default
memory, multithread, message, progress and timeline suites, `Host` (the
`OfxHost` struct, its property set and the suite container behind
`fetchSuite`), `Plugin` (main-entry calls, load/unload, describe) and the
effect model itself (`Param`, `ParamSet`, `Clip`, `Image`, `EffectDescriptor`,
`EffectInstance`, the image effect and parameter suites, and the action
sequences the specification fixes), plus the overlay interact model
(`InteractDescriptor`, `InteractInstance` and the interact suite, with the
abstract `DrawContext` behind the OFX 1.5 draw suite that a host implements).
The pixel depth and component vocabulary is common code in
`openfx/ofxPixels.h`.

What remains here is what this host decides for itself, and the test tooling:

| File | Responsibility |
|---|---|
| `Host.{h,cpp}` | The test host's identity and capabilities, and the suites it registers. |
| `Effect.{h,cpp}` | `ImageBuffer` (pixel storage with guard bytes), `TestImage` and `TestClip`, `Project`, and `EffectInstance`: the derived class that supplies the buffers, the depth and component policy, the render window, the colour management negotiation, identity copying and the post-render checks. Plus parameter parsing and the descriptor pretty-printer. |
| `Interact.{h,cpp}` | `Overlay`: the effect's interact while the host drives it -- the view it is drawn in, the scripted events from the command line, and (through `DrawRecorder.h`) the recorded draw commands and their rasterisation. |
| `DrawRecorder.h` | `RecordingDrawContext`, this host's `DrawContext`: appends every draw-suite call to a `DrawCommand` list instead of drawing, plus `rasterise()` and the neutral palette. |
| `ImageIO.{h,cpp}` | PPM/PFM read and write, solid and ramp test images. |
| `main.cpp` | Command line, the driver loop, the randomiser, the crash handler. |

`openfx::host::EffectInstance` is abstract: it drives the actions and owns the
property sets, and calls back into the host for everything it cannot know.
Clips are created through a virtual `makeClip()`, so `TestClip` carries the
pixel buffer and the list of image handles the plugin currently holds, and
their negotiated format comes from a virtual `clipProperties()`, which is
where `pickDepth` and `pickComponents` stay. Because both are virtual they are
unavailable while the base constructor runs, so the derived constructor calls
`createClips()` once its own members exist; symmetrically its destructor calls
`destroyInstance()` while the plugin can still reach the host. The suites call
`fetchImage`, `releaseImage`, `clipRegionOfDefinition` and `abort` the same
way. That is the split `HostSupport`'s `ofxhImageEffect.h` makes between
generic mechanics and host-specific virtuals, in C++17 over the metadata.
Every action goes out through `EffectInstance::action()`, between two more
virtuals, `beforeAction()` and `afterAction()`, which see the argument sets
the drivers built, so a host can add a property of its own or read one a
driver ignores without writing the driver again; this host has nothing to add.

## Property sets from metadata

The central idea. `openfx-cpp/include/openfx/ofxPropsBySet.h` (generated
from the `@propset` and `@actiondef` blocks in the headers) lists every
property of every property set and every action's inArgs/outArgs, each with
its `PropDef` (type, dimension, enum values, default). `openfx::host::PropertySet(setName)`
looks the set up and pre-defines every single-typed property with the correct
storage type and dimension; `PropertySet::forAction(action, "inArgs")` does
the same for an action's arguments. The store started life in this host and
moved into the framework once it had no host-specific policy left in it, as
the rest of the effect model has since. Multi-typed properties such as
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

Spec defaults that plugins rely on come from the metadata: a `@propdef` may
carry a `default:`, and `PropertySet` seeds it when it pre-defines the
property. The host sets by hand only what a constant cannot express: the
labels and script name derived from a parameter's name, and the min/max,
display range, default and animation flag that depend on its value type.

## Handles

An OFX handle is a pointer to the host's own object, cast through the opaque
handle type: `PropertySet*` for `OfxPropertySetHandle`, `EffectBase*`
(descriptor or instance) for `OfxImageEffectHandle`, `InteractBase*`
(likewise) for `OfxInteractHandle`, `DrawContext*` for
`OfxDrawContextHandle`, `Clip*`, `Param*`, `ParamSet*`, and a `MemoryBlock*`
for image memory. `Image` derives from
`PropertySet`, so the property-set handle handed to a plugin for an image
*is* the image, and `clipReleaseImage` recovers it with a static downcast
rather than a side table. The host's own property set does the same in
reverse: `OfxHost::fetchSuite` is handed only that handle, so the set carries
a back-pointer to its `Host` and the suite lookup needs no globals.

## Rendering

For a frame: GetRegionOfDefinition, GetRegionsOfInterest, GetFramesNeeded,
IsIdentity, BeginSequenceRender, Render, EndSequenceRender, PurgeCaches. Two
host responsibilities that real plugins depend on:

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
state on change behave; a keyframe (`--param NAME@TIME=VALUE`) fires the same
triple, with the key's time. The whole parameter-setting phase sits inside one
BeginInstanceEdit / EndInstanceEdit pair, which is the period a host's user
could have the effect open in front of them, and SyncPrivateData is sent once
more just before the instance is destroyed.

## What the plugin says it needs

Three actions exist so a host can ask a plugin what it will want before it asks
for it, and a host that never sends them never finds out that a plugin's
answers and its behaviour disagree. All three have drivers on
`openfx::host::EffectInstance`, which build the per-clip out-args the
specification names by clip (`OfxImageClipPropRoI_<clip>`,
`OfxImageClipPropFrameRange_<clip>`) and seed every one with the default the
spec requires before the action, so a plugin that answers for one clip and not
another still gets sensible values back:

- GetRegionsOfInterest, for the render window: the region of each input the
  plugin needs. Clips it says nothing about keep the requested region.
- GetFramesNeeded, for the frame: a 2N-dimensional list of frame ranges per
  input, defaulting to the single frame. An odd dimension is a plugin bug and
  is reported.
- GetTimeDomain, once per instance, for a general or generator effect.

The first two are then used as conformance checks: `fetchImage` warns when the
plugin fetches a clip at a time outside the ranges it declared, or fetches a
clip it declared an empty region of interest for. A host that only materialised
what was asked for would hand that plugin nothing; this one says so instead.

Two deviations from the letter of the specification, both deliberate in a test
host. GetFramesNeeded need only be called when the plugin sets
`kOfxImageEffectPropTemporalClipAccess`; this host always calls it, because a
plugin that answers it without claiming temporal access is worth knowing about.
GetTimeDomain is specified for the general context alone; this host also calls
it on a generator, whose own duration is the one thing it might have to say.

## Colour management

OFX 1.5 (`ofxColour.h`) has five styles ordered None < Basic < Core < Full <
OCIO, and `--colour-management none|basic|core` chooses which of them the host
advertises. Full needs a real colour pipeline and OCIO needs the OCIO library,
so neither is offered.

Who writes what, following the specification:

| Property | Set on | By |
|---|---|---|
| `kOfxImageEffectPropColourManagementStyle` | the host's property set | the host, always |
| `kOfxImageEffectPropColourManagementAvailableConfigs` | the host's property set | the host, when the style is not None |
| `kOfxImageEffectPropColourManagementStyle` | the effect descriptor | the plugin, in Describe |
| `kOfxImageEffectPropColourManagementStyle` | the effect instance | the host: the negotiated style, always, even when it is None |
| `kOfxImageEffectPropColourManagementConfig` | the effect instance | the host |
| `kOfxImageEffectPropDisplayColourspace` | the effect instance | the host, for a native style |
| `kOfxImageClipPropColourspace` | each input clip instance | the host |
| `kOfxImageClipPropPreferredColourspaces` | each input clip instance | the plugin, through the GetClipPreferences out-args |
| `kOfxImageClipPropPreferredColourspaces` | the output clip instance | the host |
| `kOfxImageClipPropColourspace` | the output clip instance | the host, from the GetOutputColourspace answer |

The style is negotiated as the specification puts it -- "the highest style
supported by both host and plug-in should usually be chosen by the host" -- so
the instance gets `min(plugin, host)`. A plugin that asks for Full or OCIO here
gets Core or Basic, which is safe because each style's colourspaces are a
subset of the next one's; the host logs that it did so at Debug. A plugin that
declares nothing wants no colour management and gets None. The ColourSpace
examples compute the same minimum themselves in DescribeInContext, from the
host's advertised style, and that is only sound because the host settles on the
same answer.

The style, the config, the display colourspace and the input clips'
colourspaces are all written before CreateInstance, because a plugin reads them
from its first action onwards -- the ColourSpace examples read the style in
GetClipPreferences. Colour management does not apply to a mask clip or to a
clip that carries alpha alone, and `kOfxImageClipPropColourspace` is left
untouched on those. (The specification says it "must be unset"; this host's
metadata-driven store pre-defines every property of a set, so the best it can
do is never write one.)

The output colourspace is negotiated right after the clip preferences, because
the plugin's own preferences arrive with them and because the spec has the
action called again whenever anything colour-related changes. The host offers
its working colourspace followed by the generic `ofx_scene_linear`, which is
the fallback the spec recommends including. A cross-reference answer
(`OfxColourspace_<clip>`) is resolved to that clip's colourspace before it goes
on the output clip, which is what a plugin expects to read back there. The
answer is checked against the negotiated style and a colourspace that style
does not offer is a Warning; one that is merely not on the host's list is not,
because the spec explicitly lets a plugin ignore the list -- a motion-vector
plugin is supposed to answer `ofx_raw` however nicely it was asked.

`openfx/ofxColourspaces.h` carries the style enum and the config's
colourspaces and roles. Only the identifiers are written out there; `IsBasic`,
`IsCore` and the encoding come from the config header's own macros, so the
table cannot disagree with the config it describes. The encoding is what makes
`basicColourspaceFor` possible: a basic colourspace is by definition any
colourspace with the same encoding and reference space, so the host can always
name the generic stand-in for its working colourspace. It needs that when
`--colourspace` names a colourspace of the style the host advertises but the
plugin has pinned the negotiation lower -- `--colour-management core
--colourspace ACEScct` against a Basic plugin makes the host tell it
`ofx_scene_log`, and `srgb_display` becomes `ofx_display_sdr`.

What this host does *not* do is convert anything. The colourspace on a clip is
a label on the pixels the host already has, so when a plugin asks for an input
in a colourspace the host is not working in, the host keeps its own and says so
at Debug -- which is exactly what the spec allows ("In the event that the host
cannot supply images in a requested colourspace, it may supply images in any
valid colourspace. Plug-ins must check `kOfxImageClipPropColourspace`"). The
point here is to drive and check the negotiation, not to grade the pixels.

## Tiles and render scale

`renderFrame(time)` stays the entry point for one frame; tiling is a step
inside it. `tilesOf()` splits the render window (a count with `--tiles N`, a
fixed size with `--tile WxH`) and `renderTiles()` issues one Render action per
tile, all inside the single BeginSequenceRender / EndSequenceRender pair the
specification requires. A window that the count does not divide evenly is the
case worth testing, so the split takes `i * extent / count` and leaves the
remainder in the last tile rather than padding the window out.

A tile is a *view*, not a buffer of its own. The host allocates one
`ImageBuffer` for the frame and hands the plugin an image whose bounds are the
tile, whose row bytes are the frame's, and whose data pointer is
`pixelData(tile.x1, tile.y1)`. Nothing is copied, nothing is assembled
afterwards, and `kOfxImagePropRegionOfDefinition` still describes the whole
frame, as it must. Input images work the same way from the region
`clipGetImage` passes: the bounds are that region converted from canonical to
pixel coordinates and clipped to what the clip holds, the region of definition
is the whole input. A view allocates nothing, so the guard bytes around the
frame buffer still cover every tile.

Views make the interesting failure visible only as a wrong pixel, because a
plugin that reads or writes past its bounds inside the frame corrupts no
memory -- it reads its neighbours. That is what `--check-tiles` is for: it
renders the frame a second time, whole, and warns about the first pixel where
the two differ. The result the host returns is always the tiled one, so the
check cannot be passed by reading the rest of the buffer. It found that the
Support noise generator draws different noise per tile.

Render scale keeps the two coordinate systems apart, which the host had not
had to do before: the project, the region of definition and spatial parameters
are canonical, while the render window and every image bound are pixels.
`canonicalToPixel()` is the specification's mapping, `X' = X * SX / PAR` and
`Y' = Y * SY`, rounded outwards. Inputs are resampled by nearest neighbour
(`ImageBuffer::resampled`) into a second buffer on the clip, so the original
stays available for the next render and the guard check covers the buffer the
plugin actually saw. Box filtering would be better pixels but would make an
`--expect` at a scaled coordinate depend on the filter rather than on the
plugin.

Both are subject to the plugin's declaration:
`kOfxImageEffectPropSupportsTiles` and
`kOfxImageEffectPropSupportsMultiResolution` on the descriptor. A plugin that
declares neither is rendered whole and at scale 1 with an info line, because
that is a limitation it has announced, not a fault. Warnings are reserved for
a plugin that claims the support and then fails.

## Overlay interacts

An overlay is not driven through the plugin's main entry point but through the
separate entry point it puts on its effect descriptor
(`kOfxImageEffectPluginPropOverlayInteractV2`, or V1 for an OpenGL overlay),
so the framework models it separately: `openfx::host::InteractDescriptor`
holds that entry point and the "InteractDescriptor" property set, and one
`InteractInstance` per effect instance holds the "InteractInstance" set,
parented to it. Both derive from an `InteractBase` whose pointer *is* the
`OfxInteractHandle`, because `interactGetPropertySet` is called on a
descriptor during Describe and on an instance from then on. The instance also
holds the view -- viewport size, pixel scale, background and suggested colour
-- and writes it onto its property set as well as into every action's in-args,
which is what a host must supply. The order is the one the specification
fixes: describe once per effect descriptor, create after the effect instance,
destroy before it.

`openfx::host::DrawContext` (`ofxDrawSuiteHost.h`) is the object behind
`OfxDrawContextHandle`: it validates every call against the specification --
open only for the duration of one Draw action, so a plugin that keeps the
handle and draws later is refused with `kOfxStatFailed` -- and hands the
actual drawing to virtuals a host implements. This host has no display, so
`testhost::RecordingDrawContext` (`DrawRecorder.h`) implements them by
appending a `DrawCommand` per call -- colour, line width, stipple, each
primitive with its points, each text with its position -- each carrying the
state in force when it was made, instead of drawing. `rasterise()` turns the
recorded lines, rectangles, polygons and ellipses into pixels through a plot
callback, which is what `--draw-out` writes; text is recorded and drawn as
nothing, since the font is the host's.

`--interact` creates the overlay and `--pen`, `--key`, `--focus` and `--draw`
script a session in command-line order, which runs after the parameters are
set and before the frame is rendered, so a pen drag that moves a parameter
shows up in the render. Pen positions are canonical; the viewport position is
derived from the same projection the pixel scale comes from, which here maps
the whole project onto the viewport, so `--viewport` is the only knob that
makes the pixel scale anything but 1. `kOfxInteractPropSlaveToParam` is
honoured: a `--param` that changes a parameter the interact is slaved to
draws it again, and so does a plugin's own `interactRedraw` once the script
has run.

The host drives only V2 overlays. A V1 overlay draws with OpenGL, and a host
with no GL context that sent it the Draw action would have it issue calls into
nothing; several of the Support plugins declare one, so the host says so and
leaves them alone rather than fuzzing them into a crash of its own making.

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
also declares a set of `project.Test()` entries that run the host against the
freshly built bundles with `--expect` pixel checks. The colour management ones
need the ColourSpace examples, which are built only when CImg and spdlog come
from Conan, so they are declared behind the same guard. Tests execute in the
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
  Adding `--colour-management` to the randomiser did not turn up anything
  else in them: every crash still shrinks to a small frame, and none of the
  colourspace, region-of-interest or frames-needed checks fired.
- The Support noise generator declares tile support, but its noise is seeded
  per render rather than per pixel, so a tiled frame does not match the same
  frame rendered whole; `--tiles 5 --check-tiles` reports it on any size.
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
- `kOfxInteractPropViewportSize` was removed from the specification in OFX
  1.4, so the `InteractInstance` `@propset` block in `ofxInteract.h` does not
  list it and the generated accessor has no setter for it. The host writes it
  anyway, for the pre-1.4 plugins that still read it: the store creates it on
  the first write from its own `@propdef` (declared in `ofxOld.h`, as
  `"OfxInteractPropViewport"`), so the type, dimension and value are right.

## Animation

A parameter in the framework (`openfx::host::Param`) holds a `ParamValue` --
the doubles, the ints or the string its kind uses -- and, once keyed, its keys
in increasing time order. `value(t)` is the static value while there are no
keys; with keys it is the parameter reference's "Animation" rule for the type:
the numeric types interpolate linearly between the two keys around `t` (the
integer types rounding the result), every other type holds the key before `t`,
and outside the keys the first or last one holds. `derivative(t)` is the slope
of that segment, zero for the held types and outside the keys; `integral(t1,
t2)` cuts the range at every key inside it, so a trapezoid per piece (a
rectangle for a held type) is the exact area. `setValueAtTime` adds or replaces
a key, `setValue` sets the static value -- or, on a parameter that already has
keys, the value at the timeline's current time, which is what the
specification asks of a host. The parameter suite is a thin layer over that:
the varargs still read and write by kind, the key functions map onto
`numKeys`, `keyTime`, `keyIndex`, `deleteKey`, `deleteAllKeys` and `copyFrom`,
and `kOfxParamPropIsAnimating` tracks whether there are keys at all.

Which types animate is the descriptor's `kOfxParamPropAnimates`, defaulted per
type from the same table that gives each type its kind and arity: the numeric
types animate, group, page and push button cannot, and string, custom,
boolean, choice and string-choice animate only if the plugin asks, because
this host does not declare support for animating them. `setValueAtTime` on a
parameter that does not animate sets the value instead, with a note under
`--verbose`, rather than silently keeping a key the host would never use.

The host renders a sequence with `--frames`: one BeginSequenceRender with the
whole range and the frame step, `renderFrame(t)` per frame, then one
EndSequenceRender. `renderFrame` is still the unit of work and still brackets
itself when it is called on its own. Inside a sequence the render arguments
carry the range, the step and `kOfxImageEffectPropSequentialRenderStatus`,
since the host renders first to last on one instance and so can promise what
`kOfxImageEffectInstancePropSequentialRender` asks for; the host declares the
same capability on itself. `kOfxImageEffectFrameVarying` is read from the
clip preferences and reported: a caching host would use it to decide whether
one rendered frame can stand for another, while this host renders every frame
it was asked for, which is what a test tool should do.

A chain renders each frame through every effect before moving to the next
frame, because effect N+1's input is effect N's output at that frame.

## Open questions

- Whether the host should require C++20 or the tree should pick up the
  tcb-span dependency so it can stay at C++17 with the rest of the build.
- Keyframe interpolation is linear only: no tangents, no custom parameter
  interpolation through `kOfxParamPropCustomInterpCallbackV1`.
- Real colourspace conversion, which would let the host honour a plugin's
  preferred input colourspace rather than labelling what it has.
