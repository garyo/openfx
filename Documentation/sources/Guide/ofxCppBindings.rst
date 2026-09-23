.. SPDX-License-Identifier: CC-BY-4.0

The C++ Bindings
================

``openfx-cpp`` is a header-only C++ layer over the OpenFX C API, for both
sides of it. It does not replace the API: every wrapper calls the same suites
a C plugin or host would, and a program can drop to the raw handles at any
point. What it adds is type safety and lifetime safety --- property names and
types checked at compile time from the specification's own metadata, RAII over
images and memory, exceptions instead of status codes where that reads better,
and a small logging facility.

It is prerelease. The headers ship with the SDK and are exercised by
everything in the OpenFX tree, but the interfaces may still change; see
`Status`_ for what is not covered yet.

Requirements
------------

A C++17 compiler, plus `tcb-span <https://github.com/tcbrindle/span>`_ for the
``std::span`` stand-in that ``openfx/ofxSpan.h`` needs below C++20; from C++20
on, nothing but the standard library. There is nothing to build or link: the
headers are the whole library.

Using them
----------

From CMake, in the OpenFX source tree or from an installed or Conan package:

.. code-block:: cmake

   target_link_libraries(my-plugin PRIVATE OpenFX::openfx-cpp)

That target carries the include directories for both ``openfx-cpp/include``
and the C API headers in ``include/``, requires C++17, and links tcb-span when
``find_package(tcb-span)`` finds it. The Conan recipe requires tcb-span under
the ``build_openfx_cpp`` option (on by default; a C++20-only consumer can turn
it off) and exports the headers as the ``openfx-cpp`` component.

Without CMake, put ``openfx-cpp/include`` and ``include/`` on the include path
and include what you need:

.. code-block:: cpp

   #include <openfx/plugin/ofxPluginBase.h>   // plugin side
   #include <openfx/host/ofxHost.h>           // host side

Layout
------

The headers are split by which side of the API uses them, and each side has
its own namespace.

``openfx/`` (namespace ``openfx``), used by plugins and hosts alike
    Property metadata (``ofxPropsMetadata.h``, ``ofxPropsBySet.h``), the
    type-safe ``PropertyAccessor`` (``ofxPropsAccess.h``), ``SuiteContainer``
    (``ofxSuites.h``), the pixel depth and component vocabulary
    (``ofxPixels.h``), the colour management styles and the native config's
    colourspaces (``ofxColourspaces.h``), exceptions, logging, status strings,
    rect and point helpers (``ofxMisc.h``), and the span shim.

``openfx/plugin/`` (namespace ``openfx::plugin``), plugins only
    RAII ``Image`` and ``Clip`` wrappers over the image effect suite;
    ``ImageEffect``, ``ActionArgs`` and ``ImageMemory`` over an effect handle;
    ``ParamSet`` and one class per parameter type; wrappers over the message,
    progress, memory, multithread and timeline suites; the
    ``ImageEffectPlugin`` action dispatcher and its ``PluginEntry``
    boilerplate; and the generated per-property-set accessor classes in
    ``openfx::plugin::propsets`` --- getters for host-written properties,
    setters for plugin-written ones.

``openfx/host/`` (namespace ``openfx::host``), hosts only
    ``PropertySet``, a metadata-driven property store with an
    ``OfxPropertySuiteV1`` over it; ``PluginBinary``, which loads a plugin
    binary or bundle and lists its plugins, plus the standard plugin search
    paths; default memory, multithread, message, progress and timeline suites;
    ``Host``, the ``OfxHost`` struct with its property set and suite
    container; ``Plugin``, one plugin driven through its main entry point; the
    generic effect model --- parameters, clips, images, descriptors and an
    abstract ``EffectInstance`` that drives the actions --- with the image
    effect and parameter suites over it; and the generated accessor classes
    in ``openfx::host::propsets``, with the read and write permissions the
    other way round.

Anything in ``openfx/`` takes the suites it needs as arguments, so it works
against a real host's suites from inside a plugin and against a host's own
suite implementations from inside that host.

Writing a plugin
----------------

A plugin is one class and two exported functions. Derive from
``ImageEffectPlugin``, override the actions you care about --- every action is
a virtual with typed arguments, and the ones you leave alone return
``kOfxStatReplyDefault`` --- and hand the class to ``PluginEntry``, which
builds the ``OfxPlugin`` struct, routes the main entry point and turns an
escaped exception into a status code. The two functions need no export
specifier: ``ofxCore.h`` declares them with ``OfxExport``, and the definitions
inherit it, even in a build with hidden symbol visibility.

Three actions carry a minimal filter:

``describe``
    Writes the plugin's descriptor through ``effect.descriptor()``, whose
    setters are generated from the property metadata: ``setLabel``,
    ``setSupportedContexts``, ``setSupportedPixelDepths``, ``setSupportsTiles``
    and the rest, each taking the type the specification gives that property,
    and each fluent so they chain.

``describeInContext``
    Calls ``effect.defineClip(name)`` per clip and ``effect.params()`` for the
    parameter set, which has a ``define`` function per parameter type
    (``defineDouble``, ``defineRGBA``, ``defineChoice``, ``definePage`` ...),
    again with generated setters.

``render``
    Reads its arguments through
    ``args.as<propsets::ImageEffectActionRender_InArgs>()``, fetches parameter
    values with ``params.get<DoubleParam>(name).getValueAtTime(time)``, and
    gets the images from ``effect.clip(name).getImage(time)``. ``Image``
    releases itself, so an early ``return`` cannot leak one.

Here is the whole thing --- a brightness filter on float RGBA. It lives at
``openfx-cpp/examples/minimal-plugin/minimal.cpp`` and is compiled by the
``openfx-cpp-check`` CMake target, so this listing is always code that builds:

.. literalinclude:: ../../../openfx-cpp/examples/minimal-plugin/minimal.cpp
   :language: cpp

``Examples/CppGain/cppgain.cpp`` is the same plugin grown up: every pixel
depth, multithreading, progress reporting, abort checks and OFX 1.5 colour
management, still without calling a suite directly.

Writing a host
--------------

The host side is a framework, not a host: it owns the property store, the
plugin loading, the effect model and the suites, and leaves the host its own
policy --- pixel storage, the formats to negotiate, threading, the UI.
``TestHost/`` in the OpenFX tree is a complete worked example; its
``README.md`` and ``DESIGN.md`` explain its choices.

Five things a host supplies:

1. **A** ``Host``. Derive from ``openfx::host::Host``, fill its property set
   through the generated ``accessor()`` (``setName``, ``setLabel``,
   ``setSupportedPixelDepths``, ``setSupportedContexts``, the GPU-support
   flags, the colour management style), and register the suites it offers:
   ``PropertySet::suite()``, ``effectSuite()``, ``paramSuite()``, and
   ``addDefaultSuites()`` for memory, multithread, message, progress and
   timeline. ``host.ofx()`` is what a plugin is handed through ``setHost``.
2. **Plugin loading.** ``PluginBinary::load(path)`` takes a ``.ofx.bundle``
   directory, a bare ``.ofx`` binary or a directory of bundles, and lists the
   plugins each one exports; ``standardPluginPaths()`` gives the search paths
   the specification defines. Wrap one in ``openfx::host::Plugin`` to drive
   it: ``setHost``, Load, Describe, DescribeInContext.
3. **An** ``EffectInstance`` **subclass.** Three pure virtuals:
   ``clipProperties()``, the format and timing this host negotiates for one
   clip; ``fetchImage()``, an image of a clip at a time over a region; and
   ``releaseImage()``. Override ``makeClip()`` as well if the host's clips
   carry storage, and ``abort()`` and ``clipRegionOfDefinition()`` if it has
   answers for them. Everything else --- the descriptors, the parameters with
   their animation, the actions and their argument property sets --- the
   framework handles.
4. **Image storage.** Derive from ``openfx::host::Image`` and attach the pixel
   buffer; the framework fills in the properties the plugin reads (``data``,
   ``bounds``, ``rowBytes``, ``regionOfDefinition``, ``renderScale``).
5. **The render sequence.** The host decides what to call and when:
   GetRegionOfDefinition, GetRegionsOfInterest, GetFramesNeeded, IsIdentity,
   BeginSequenceRender, Render per tile, EndSequenceRender. ``EffectInstance``
   has a method per action that marshals the arguments and returns the status.

Generated metadata
------------------

``ofxPropsMetadata.h``, ``ofxPropsBySet.h`` and both
``ofxPropSetAccessors.h`` are generated from the ``@propdef``, ``@propset``
and ``@actiondef`` blocks in ``include/*.h`` --- the same blocks the
:doc:`property reference <../Reference/ofxPropertiesReferenceGenerated>`
comes from.
Do not edit them by hand; edit the metadata in the C headers and regenerate:

.. code-block:: sh

   uvx pcons run gen-props          # or: python scripts/gen-props.py

A host that defines properties of its own generates matching accessors for
them in its own namespace; ``openfx-cpp/examples/host-specific-props/`` shows
how.

An accessor method takes its name from the property's C ``#define``, less the
``kOfx``, the ``Prop`` and the kind of object the class is for:
``kOfxImageEffectPropProjectPixelAspectRatio`` is ``projectPixelAspectRatio()``
and ``setProjectPixelAspectRatio()``. Any other qualifier stays, as OpenGL does
in ``setOpenGLPixelDepth()`` for ``kOfxOpenGLPropPixelDepth``, and so does the
object when two properties of a set would otherwise share a name (``type()``
and ``paramType()``). In the generated headers, each property's methods follow
a comment giving its ``#define``, so a search for the C constant finds them.

Logging
-------

``openfx/ofxLog.h`` is a small thread-safe logger with ``{}``-style
formatting: ``Logger::debug``, ``info``, ``warn`` and ``error``, a settable
level, and a settable handler for sending messages somewhere else.
``openfx-cpp/include/openfx/README-logging.md`` has the details.

Testing
-------

The pcons build compiles the headers, the test host and the plugins written on
them, and runs the host against the plugins:

.. code-block:: sh

   uvx pcons BUILD_PLUGINS=1                # the test host and the plugin bundles
   uvx pcons -B build/pcons/release test    # run the host against them
   uvx pcons -B build/pcons/tidy CLANG_TIDY=1      # clang-tidy alongside every compile
   uvx pcons -B build/pcons/asan SANITIZE=1 test   # ASan and UBSan

CMake does not build the host or the examples, but it does compile-check the
headers: ``OFX_BUILD_OPENFX_CPP_CHECK`` (on by default) adds an
``openfx-cpp-check`` target that compiles every header, on each side, at C++17
and at C++20, along with the minimal plugin above. ``TestHost/fuzz.py`` drives
the test host over randomised host choices to shake out assumptions in a
plugin.

Status
------

Prerelease. The headers are complete enough to write a real filter and a real
host on --- the test host and ``CppGain`` are the proof --- but the interfaces
are not frozen, and these parts of OpenFX have no wrappers yet:

* fields and field rendering
* the GPU render suites (OpenGL, CUDA, Metal, OpenCL)
* parametric parameters
* interacts and the drawing suite
* the OCIO and full colour management styles (basic and core are covered)

For those, call the C suites directly through ``SuiteContainer``; nothing in
these headers prevents it.
