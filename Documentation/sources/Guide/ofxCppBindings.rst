.. SPDX-License-Identifier: CC-BY-4.0

The C++ Bindings
================

``openfx-cpp`` is a header-only C++ library over the OpenFX C API, for plugins
and hosts. It does not replace the C API: every wrapper calls the same suites
a C plugin or host would, and you can use the raw handles at any point. It
adds:

* property access checked at compile time against the specification's
  metadata
* RAII for images, memory and other resources
* exceptions in place of status codes, where that is clearer
* a small logging facility

It is prerelease: the headers ship with the SDK, but the interfaces may still
change. See `Status`_ for what is not covered yet.

Requirements
------------

A C++17 compiler. Below C++20, the headers also need
`tcb-span <https://github.com/tcbrindle/span>`_ as a stand-in for
``std::span`` (see ``openfx/ofxSpan.h``); C++20 needs only the standard
library. The library is header-only, so there is nothing to build or link.

Using them
----------

From CMake, in the OpenFX source tree or from an installed or Conan package:

.. code-block:: cmake

   target_link_libraries(my-plugin PRIVATE OpenFX::openfx-cpp)

The target adds the include directories for ``openfx-cpp/include`` and the C
API headers in ``include/``, requires C++17, and links tcb-span if
``find_package(tcb-span)`` finds it. In the Conan package the headers are the
``openfx-cpp`` component, with the target ``openfx::openfx-cpp``. The recipe's
``build_openfx_cpp`` option (on by default) pulls in tcb-span; a C++20-only
consumer can turn it off.

Without CMake, put ``openfx-cpp/include`` and ``include/`` on the include path
and include what you need:

.. code-block:: cpp

   #include <openfx/plugin/ofxPluginBase.h>   // plugin side
   #include <openfx/host/ofxHost.h>           // host side

Layout
------

The headers are in three directories, split by which side of the API uses
them. Each directory has its own namespace.

``openfx/`` (namespace ``openfx``), for plugins and hosts
    Property metadata (``ofxPropsMetadata.h``, ``ofxPropsBySet.h``); the
    type-safe ``PropertyAccessor`` (``ofxPropsAccess.h``); ``CStringView``,
    the string type its getters return (``ofxCStringView.h``);
    ``SuiteContainer`` (``ofxSuites.h``); pixel depths and components
    (``ofxPixels.h``); colour management styles and the native config's
    colourspaces (``ofxColourspaces.h``); exceptions; logging; status strings;
    rect, point and per-clip property-name helpers (``ofxMisc.h``); and the
    span shim.

``openfx/plugin/`` (namespace ``openfx::plugin``), for plugins only
    RAII ``Image`` and ``Clip`` wrappers for the image effect suite
    (``ofxImage.h``, ``ofxClip.h``); ``ImageEffect``, ``ActionArgs`` and
    ``ImageMemory`` for an effect handle (``ofxEffect.h``); ``ParamSet`` and
    a class per parameter type (``ofxParam.h``); wrappers for the message,
    progress, memory, multithread and timeline suites (``ofxMessage.h``,
    ``ofxProgress.h``, ``ofxMemory.h``, ``ofxMultiThread.h``,
    ``ofxTimeLine.h``); ``Interact`` and the ``InteractPlugin`` dispatcher for
    overlays (``ofxInteract.h``), with ``Draw`` for the OFX 1.5 draw suite
    (``ofxDraw.h``); the ``ImageEffectPlugin`` action dispatcher, with
    ``PluginEntry`` and ``PluginEntries`` to export it (``ofxPluginBase.h``);
    and the generated accessor classes for each property set, in
    ``openfx::plugin::propsets``: getters for host-written properties, setters
    for plugin-written ones.

``openfx/host/`` (namespace ``openfx::host``), for hosts only
    ``PropertySet``, a property store driven by the metadata, with an
    ``OfxPropertySuiteV1`` over it (``ofxPropertySet.h``); ``PluginBinary``,
    which loads a plugin binary or bundle and lists its plugins, and the
    standard plugin search paths (``ofxPluginBinary.h``); default memory,
    multithread, message, progress and timeline suites
    (``ofxDefaultSuites.h``); ``Host``, the ``OfxHost`` struct with its
    property set and suites (``ofxHost.h``); ``Plugin``, which drives one
    plugin through its main entry point (``ofxPlugin.h``); the effect model
    (parameters, clips, images, descriptors, and an abstract
    ``EffectInstance`` that sends the actions) with the image effect and
    parameter suites over it (``ofxEffect.h``); the overlay model
    (``InteractDescriptor``, ``InteractInstance``, and drivers for the draw,
    pen, key and focus actions) with the interact suite over it
    (``ofxInteract.h``); the abstract ``DrawContext`` a host implements behind
    ``OfxDrawSuiteV1`` (``ofxDrawSuiteHost.h``); and the generated accessor
    classes in ``openfx::host::propsets``: setters for host-written
    properties, getters for plugin-written ones.

Code in ``openfx/`` takes the suites it needs as arguments, so it works the
same in a plugin, with the host's suites, and in a host, with its own.
``plugin/`` and ``host/`` each have an ``ofxPropSetAccessors.h`` with the same
class names; the namespace tells them apart.

Two programs in the OpenFX tree use the bindings: ``Examples/CppGain/`` (a
gain filter that uses only the plugin-side wrappers and calls no suite
directly) and ``openfx-cpp/examples/minimal-plugin/minimal.cpp``, the plugin
below.

Writing a plugin
----------------

A plugin is one class and two exported functions. Derive from
``ImageEffectPlugin`` and override the actions you need. Each action is a
virtual function with typed arguments; the ones you don't override return
``kOfxStatReplyDefault``. Then pass the class to ``PluginEntry``, which builds
the ``OfxPlugin`` struct, routes the main entry point, and converts uncaught
exceptions to status codes. The two functions need no export specifier:
``ofxCore.h`` declares them with ``OfxExport``, so they are exported even with
hidden symbol visibility.

A minimal filter needs three actions:

``describe``
    Sets up the plugin's descriptor through ``effect.descriptor()``. Its
    setters (``setLabel``, ``setSupportedContexts``,
    ``setSupportedPixelDepths``, ``setSupportsTiles`` and so on) are generated
    from the property metadata, take the property's type, and chain. Set
    optional properties, such as the plugin description, through ``soft()``
    (see `Missing properties`_).

``describeInContext``
    Defines each clip with ``effect.defineClip(name)``, and each parameter
    through ``effect.params()``, which has a ``define`` function per parameter
    type (``defineDouble``, ``defineRGBA``, ``defineChoice``, ``definePage``
    ...). These also return objects with generated setters.

``render``
    Reads its arguments with
    ``args.as<propsets::ImageEffectActionRender_InArgs>()``, gets parameter
    values with ``params.get<DoubleParam>(name).getValueAtTime(time)``, and
    gets images with ``effect.clip(name).getImage(time)``. ``Image`` releases
    itself, so an early ``return`` cannot leak one.

Here is the complete plugin, a brightness filter on float RGBA images, from
``openfx-cpp/examples/minimal-plugin/minimal.cpp``:

.. literalinclude:: ../../../openfx-cpp/examples/minimal-plugin/minimal.cpp
   :language: cpp

``Examples/CppGain/cppgain.cpp`` is a fuller version of the same plugin, with
every pixel depth, multithreading, progress reporting, abort checks and OFX 1.5
colour management. It too calls no suite directly.

Several plugins, and a main entry of your own
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For a binary with several plugins, ``PluginEntries`` implements the two
exported functions, with the plugins in the order listed. Each class gets its
own instance, ``setHost`` and main entry, as with ``PluginEntry``:

.. code-block:: cpp

   using Entries = PluginEntries<Blur, Sharpen, Gain>;
   int OfxGetNumberOfPlugins(void) { return Entries::numberOfPlugins(); }
   OfxPlugin* OfxGetPlugin(int nth) { return Entries::get(nth); }

To port a C plugin one action at a time, keep your own main entry and pass the
ported actions to ``dispatch()``. ``dispatch()`` fetches the suites in the
Load action, so if your main entry handles Load itself, call
``ImageEffectPlugin::fetchSuites(host, suites)`` there. It returns
``kOfxStatErrMissingHostFeature`` if the host lacks a required suite.

.. code-block:: cpp

   OfxStatus mainEntry(const char* action, const void* handle,
                       OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
     if (std::strcmp(action, kOfxActionLoad) == 0)
       return ImageEffectPlugin::fetchSuites(gHost, gPlugin.suites);
     if (std::strcmp(action, kOfxActionDescribe) == 0)
       return gPlugin.dispatch(action, handle, inArgs, outArgs);
     return legacyMainEntry(action, handle, inArgs, outArgs);
   }

Actions without their own virtual, such as the OpenGL context actions or a
host's own actions, go to ``otherAction(action, handle, inArgs, outArgs)``
unchanged. By default it returns ``kOfxStatReplyDefault``.

Overlays
~~~~~~~~

``InteractPlugin`` does for an overlay what ``ImageEffectPlugin`` does for an
effect. It has a virtual for each interact action (``describe``,
``createInstance``, ``destroyInstance``, ``draw``, ``penDown``, ``penMotion``,
``penUp``, ``keyDown``, ``keyUp``, ``keyRepeat``, ``gainFocus``,
``loseFocus``), and ``otherAction()`` for the rest. Each virtual gets an
``Interact``; ``draw`` and the event actions also get the action's
``ActionArgs``. ``Draw`` wraps the OFX 1.5 draw suite.

The host calls an overlay's entry point with only the interact handle, so the
overlay object keeps its own copy of the suites. ``entryPoint(suites)`` stores
them in ``instance()``, the object the entry point dispatches to, and returns
the entry point to set on the effect descriptor. If you set ``mainEntry`` on
the descriptor yourself, or dispatch to your own overlay objects, give each
object its suites with ``setSuites(suites)``. An overlay object without suites
returns ``kOfxStatErrMissingHostFeature`` for every action it has a virtual
for.

.. code-block:: cpp

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

``Interact::slaveToParam(name)`` appends a parameter to
``kOfxInteractPropSlaveToParam``, and ``slaveToParams({...})`` appends
several. The host redraws the overlay when any of them changes.
``Interact::effect()`` returns the overlay's effect, for its parameters and
clips.

What the host reports
~~~~~~~~~~~~~~~~~~~~~

The wrappers implement proper status returns for all calls. In particular:

* ``effect.clip(name)``, or constructing a ``Clip`` by name, throws
  ``ClipNotFoundException`` if there is no such clip. ``defineClip()`` throws
  ``OfxException`` if it fails. Both carry the host's status.
* ``Clip::getImage()`` returns an empty ``Image`` if the host returns
  ``kOfxStatFailed``, which means the clip has no image there; treat it as
  transparent black. Any other failure throws ``ImageNotFoundException``.
  Test ``if (image)`` before using one.
* ``params.get<DoubleParam>(name)`` and the typed parameter constructors throw
  ``OfxException`` with ``kOfxStatErrValue`` if the parameter is of another
  type.
* ``Progress::update()`` returns true, to keep going, only for ``kOfxStatOK``
  and ``kOfxStatReplyYes``. When it returns false, ``lastStatus()`` tells a
  user cancel (``kOfxStatReplyNo``) from an error.
* An action that throws returns the exception's status to the host (see
  `Exceptions and the C boundary`_).

Mixing the wrappers with C calls
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A plugin can mix the wrappers and raw C calls freely, in either direction.

**From a wrapper to C.** Every wrapper gives access to its C handle:
``handle()`` on ``ImageEffect``, ``ActionArgs``, ``Clip``, ``Image``,
``ImageMemory``, ``ParamSet``, the typed parameters, ``Interact``, ``Draw``,
``Mutex`` and ``PropertyAccessor``; ``propertySetHandle()`` on ``Clip`` and
the parameters; and ``data()`` on ``Memory``. The wrappers also give the
suites they use: ``ImageEffect::effectSuite()``, ``propertySuite()`` and
``paramSuite()``, ``ParamSet::suite()`` and ``PropertyAccessor::suite()``.

.. code-block:: cpp

   OfxStatus render(ImageEffect& effect, ActionArgs& args) override {
     const OfxTime time = args.as<propsets::ImageEffectActionRender_InArgs>().time();
     Clip source = effect.clip(kOfxImageEffectSimpleSourceClipName);
     OfxRectD rod{};
     effect.effectSuite()->clipGetRegionOfDefinition(source.handle(), time, &rod);
     return legacyRender(effect.handle(), args.handle(), &rod);  // a C function
   }

**From C to a wrapper.** The non-owning wrappers wrap a handle your C code
already has, and don't release it: ``ImageEffect``, ``ActionArgs``, ``Clip``,
``ParamSet``, the typed parameters, ``Interact``, ``Draw``,
``PropertyAccessor`` and the generated ``propsets`` classes. Each takes its
suites as a ``SuiteContainer`` or as raw suite pointers, so a plugin that
keeps its suites in globals doesn't need a container:

.. code-block:: cpp

   ImageEffect effect(handle, gEffectSuite, gPropSuite, gParamSuite);
   ParamSet params(handle, gEffectSuite, gParamSuite, gPropSuite);
   DoubleParam gain(params.handle(), "gain", gParamSuite, gPropSuite);
   Interact overlay(interactHandle, gInteractSuite, gPropSuite);
   propsets::ClipInstance clipProps(clipPropSet, gPropSuite);

You can leave out the parameter suite for ``ImageEffect`` if you don't call
``params()``, and the image effect and parameter suites for ``Interact`` if
you don't call ``effect()``.

An owning wrapper releases its resource when it is destroyed. To adopt a
resource your C code acquired, construct the wrapper from the C handle and the
raw suite pointer. ``release()`` hands it back to C code, like
``std::unique_ptr::release``: it returns the handle and leaves the wrapper
empty. ``reset()`` releases the resource early.

.. list-table::
   :header-rows: 1

   * - Wrapper
     - Adopts with
     - On destruction
     - ``release()`` returns
   * - ``Image``
     - ``Image(image, effectSuite, propertySuite)``
     - ``clipReleaseImage``
     - the image's property set
   * - ``ImageMemory``
     - ``ImageMemory(memory, effectSuite, locked)``
     - ``imageMemoryUnlock`` if locked, then ``imageMemoryFree``
     - the memory handle, still locked if it was
   * - ``Memory``
     - ``Memory(data, bytes, memorySuite)``
     - ``memoryFree``
     - the block
   * - ``Mutex``
     - ``Mutex(mutex, threadSuite)``
     - ``mutexDestroy``
     - the mutex
   * - ``Progress``
     - ``Progress::adoptStarted(effect, progressSuite)``
     - ``progressEnd``
     - the effect, or null if there is no display
   * - ``ParamSet::EditScope``
     - ``EditScope::adoptBegun(paramSet, paramSuite)``
     - ``paramEditEnd``
     - the parameter set, or null if there is no edit

.. code-block:: cpp

   OfxPropertySetHandle raw = nullptr;
   if (gEffectSuite->clipGetImage(clip, time, nullptr, &raw) == kOfxStatOK) {
     Image image(raw, gEffectSuite, gPropSuite);  // released when it goes
     if (keepForC)
       gHeldImage = image.release();              // C code releases it now
   }

**Calling the suite directly.** ``PropertyAccessor`` can reach any property by
name: ``getRaw`` and ``setRaw`` for one value, ``findRaw`` for a property that
may be missing, ``getRawN`` and ``setRawN`` for several values (like
``propGetIntN``), ``getDimensionRaw``, ``reset`` (``propReset``) and
``exists``. For anything else, ``suite()`` and ``handle()`` give the suite and
the property set:

.. code-block:: cpp

   PropertyAccessor& props = effect.props();
   double matrix[9] = {};
   props.getRawN("com.example.Matrix", 9, matrix);
   props.setRaw("com.example.Pass", 2);
   int n = 0;
   props.suite()->propGetDimension(props.handle(), kOfxImageEffectPropSupportedContexts, &n);

`Exceptions and the C boundary`_ covers what happens when a wrapper throws
inside a C call.

Writing a host
--------------

The host side is a set of building blocks you can use separately: a property
store with the property suite, plugin loading, the image effect model with the
image effect and parameter suites, overlays with the interact and draw suites,
and the generic suites. Each object is what its C handle points to, and each
suite is a plain C struct, so you can replace any block with your own (see
`Taking the host side apart`_) and call C anywhere. Policy, such as pixel
storage, format negotiation, threading and the UI, stays with the host.

A host that uses all the blocks provides five things:

1. **A** ``Host``. Derive from ``openfx::host::Host``. Fill in its property
   set through the generated ``accessor()`` (``setName``, ``setLabel``,
   ``setSupportedPixelDepths``, ``setSupportedContexts``, the GPU support
   flags, the colour management style), and register the suites it offers:
   ``PropertySet::suite()``, ``effectSuite()``, ``paramSuite()``,
   ``interactSuite()`` and ``drawSuite()`` for overlays, and
   ``addDefaultSuites(suites())`` for memory, multithread, message, progress
   and timeline. Pass ``host.ofx()`` to the plugin's ``setHost``.
2. **Plugin loading.** ``PluginBinary::load(path)`` loads a ``.ofx.bundle``
   directory, a bare ``.ofx`` binary or a directory of bundles, and lists the
   plugins in each. ``standardPluginPaths()`` returns the standard search
   paths. Wrap a plugin in ``openfx::host::Plugin`` to drive it through
   ``setHost``, Load, Describe and DescribeInContext.
3. **An** ``EffectInstance`` **subclass.** Implement three pure virtuals:
   ``clipProperties()``, which returns the format and timing the host chooses
   for a clip; ``fetchImage()``, which returns a clip's image at a time and
   over a region; and ``releaseImage()``. Override ``makeClip()`` if your
   clips carry storage, and ``abort()`` and ``clipRegionOfDefinition()`` if
   you need them. ``openfx::host`` provides the descriptors, the parameters
   and their animation, and the actions with their argument property sets.
4. **Image storage.** Derive from ``openfx::host::Image``, attach the pixel
   buffer, and set the image properties with the generated
   ``openfx::host::propsets::Image`` setters (``setData``, ``setBounds``,
   ``setRowBytes``, ``setRegionOfDefinition``, ``setRenderScale`` and so on).
   ``fetchImage()`` returns the image with its ``clip`` member set.
5. **The render sequence.** The host decides what to call and when:
   GetRegionOfDefinition, GetRegionsOfInterest, GetFramesNeeded, IsIdentity,
   BeginSequenceRender, Render per tile, EndSequenceRender. ``EffectInstance``
   has a driver function for each action, which builds the arguments, sends
   the action and reads back the results.

Driving the actions
~~~~~~~~~~~~~~~~~~~

``Plugin::load(OfxHost*)`` calls ``setHost`` and then the Load action, once.
``load(Host&)`` passes ``host.ofx()``. Call ``describe()`` and
``describeInContext()`` after it.

How the drivers handle the status the plugin returns:

* The drivers that return a result (``regionOfDefinition()``,
  ``getRegionsOfInterest()``, ``getFramesNeeded()``, ``getTimeDomain()``,
  ``getOutputColourspace()`` and ``queryClipPreferences()``) return what the
  plugin wrote on ``kOfxStatOK``, and the specification's default on
  ``kOfxStatReplyDefault``. A value the plugin leaves unset also comes back as
  the specification's default, where it gives one. Any other status throws
  ``OfxException``, whose ``code()`` is the status.
* ``isIdentity(time, window, renderScale, field)`` returns an ``Identity``
  with the plugin's ``status`` and, on ``kOfxStatOK``, the ``clip`` and
  ``time`` to copy from. Its ``isIdentity()`` is true when there is a clip to
  copy. ``kOfxStatReplyDefault`` means render; any other status is an error,
  so the host should not render either.
* ``Plugin::load()``, ``describe()``, ``describeInContext()``,
  ``EffectInstance::create()`` and ``describeOverlay()`` throw
  ``OfxException`` if the plugin fails, since nothing else can proceed.
* Every other driver returns the status. ``paramChanged()`` always sends
  BeginInstanceChanged, InstanceChanged and EndInstanceChanged, and returns
  the first failure, or else InstanceChanged's status.

``actionSucceeded(status)`` is true for ``kOfxStatOK`` and
``kOfxStatReplyDefault``. ``requireSuccess(status, what)`` throws
``OfxException`` for any other status.

``regionOfDefinition(time, renderScale)`` passes the render scale the host
will render at (``{1, 1}`` by default) and returns the region in canonical
coordinates. During that call ``regionOfDefinitionInFlight()`` is true, and the
default ``clipRegionOfDefinition()`` returns no region for the output clip
instead of asking the plugin again.

GetClipPreferences takes two steps. ``queryClipPreferences()`` returns the
plugin's preferences as a ``ClipPreferences`` (a ``ClipPreference`` per clip,
the output premultiplication if the plugin changed it, the timing, the
frame-varying flag and the raw ``outArgs``), or nothing if the plugin returned
the default. It does not apply them. ``applyClipPreferences()`` applies them to
the clips and returns whether anything changed. In between, the host can change
anything it doesn't support. ``getClipPreferences()`` does both steps.

.. code-block:: cpp

   if (auto answer = instance.queryClipPreferences()) {
     if (!supportsMultipleClipDepths)
       for (auto& [name, clip] : answer->clips)
         clip.depth = openfx::PixelDepth::Float;
     instance.applyClipPreferences(*answer);
   }

Every action an ``EffectInstance`` sends goes through ``action()``, which
calls ``beforeAction(action, inArgs, outArgs)`` first and ``afterAction(action,
inArgs, outArgs, status)`` after. Both get the argument sets the driver built,
or null if the action has none. Override ``beforeAction()`` to add properties
the driver doesn't set, such as your own or ``kOfxImageEffectPropCudaStream``
on Render, or to ``define()`` your own out-args for the plugin to write.
Override ``afterAction()`` to read out-args the driver ignores. ``action()``
also tracks successful CreateInstance and DestroyInstance actions, so the
destructor destroys only an instance the plugin still has.
``InteractInstance`` has the same two hooks.

.. code-block:: cpp

   class MyInstance : public openfx::host::EffectInstance {
     // ...
    protected:
     void beforeAction(const char* action, openfx::host::PropertySet* inArgs,
                       openfx::host::PropertySet*) override {
       if (std::string_view(action) == kOfxImageEffectActionRender)
         inArgs->set(kOfxImageEffectPropCudaStream, 0, cudaStream_);
     }
   };

``EffectBase::currentTime()`` is the time ``paramGetValue`` and
``paramSetValue`` use for the effect's parameters. It returns the default
timeline's current time; a host with a time per viewer or per instance can
override it. ``EffectBase::paramSetHandle()`` is the parameter set
``getParamSet`` returns to the plugin. The table in
`Taking the host side apart`_ says what else to change if you override either.

What the suites return
~~~~~~~~~~~~~~~~~~~~~~

The suites in ``openfx::host`` return these statuses for bad calls:

* A call with a null handle, or a null pointer for a required return value,
  returns ``kOfxStatErrBadHandle``. A ``propSet*N`` call with a null value
  array returns ``kOfxStatErrValue``.
* The property suite returns ``kOfxStatErrUnknown`` for reading a property
  the set doesn't have, and for writing one that is neither in the set (or its
  parent) nor in the metadata. A value of the wrong type returns
  ``kOfxStatErrValue``, and an index out of range ``kOfxStatErrBadIndex``. These rules don't apply to host code:
  ``PropertySet::define()`` and ``set()`` create any property they are given.
  To let a plugin write a property of the host's own, ``define()`` it first.
* ``paramDefine`` returns ``kOfxStatErrUnknown`` for an unknown parameter type
  and ``kOfxStatErrExists`` for a name already defined.
* ``multiThread`` returns ``kOfxStatErrExists`` if called from one of its own
  threads, and ``kOfxStatFailed`` if the thread function throws.
* ``clipGetImage`` and ``clipGetRegionOfDefinition`` return
  ``kOfxStatFailed`` when there is no image or region to return.
* Draw suite calls return ``kOfxStatFailed`` outside a Draw action, and
  ``kOfxStatErrValue`` for an unknown primitive.

Taking the host side apart
~~~~~~~~~~~~~~~~~~~~~~~~~~

Each piece of ``openfx/host/`` is its own header, and a host can use any of
them. The table shows what each piece needs from the others, and what a host
must provide if it replaces that piece:

.. list-table::
   :header-rows: 1
   :widths: 20 25 55

   * - Piece
     - Needs
     - A host that replaces it provides
   * - ``PropertySet`` (``ofxPropertySet.h``)
     - The generated metadata only.
     - Its own ``OfxPropertySuiteV1``. The other pieces keep their properties
       in ``PropertySet``\ s, so this suite must pass handles it didn't
       create on to ``PropertySet::suite()``.
   * - The default suites (``ofxDefaultSuites.h``)
     - ``SuiteContainer``, to register them.
     - Its own suite with the same name and version, registered instead of or
       after ``addDefaultSuites()``. Only the timeline is used elsewhere (see
       below).
   * - ``Host`` (``ofxHost.h``)
     - ``PropertySet``, ``SuiteContainer``.
     - Its own ``OfxHost``, passed to ``Plugin::load(OfxHost*)``. Its
       ``host`` must be a property set its property suite can read, and its
       ``fetchSuite`` must return its suites.
   * - ``PluginBinary`` (``ofxPluginBinary.h``)
     - Nothing.
     - An ``OfxPlugin*`` from its own loader or a linked-in plugin, passed to
       ``Plugin(OfxPlugin*, bundlePath)``.
   * - ``Plugin`` (``ofxPlugin.h``)
     - An ``OfxHost*``, for ``setHost``.
     - Its own calls to ``setHost`` and ``mainEntry``. ``EffectDescriptor``,
       ``EffectInstance`` and ``InteractDescriptor`` send actions through a
       ``Plugin&``, so it must replace those too.
   * - ``EffectDescriptor``, ``EffectInstance`` (``ofxEffect.h``)
     - ``Plugin``, ``PropertySet``, ``ParamSet``, ``Clip`` and ``Image``, and
       the timeline for ``currentTime()``.
     - Its own ``OfxImageEffectSuiteV1``, since ``effectSuite()`` treats every
       effect handle as an ``EffectBase``, and its own overlays, since
       ``InteractInstance`` needs an ``EffectInstance``. If it still uses
       ``Plugin::describe()`` and ``describeInContext()``, which create
       ``EffectDescriptor``\ s, its suite must pass their handles on to
       ``effectSuite()``.
   * - ``Param``, ``ParamSet`` (``ofxEffect.h``)
     - ``PropertySet``, and the owning effect's ``currentTime()``.
     - An override of ``EffectBase::paramSetHandle()`` that returns its own
       parameter set, and its own ``OfxParameterSuiteV1`` that passes handles
       it didn't create on to ``paramSuite()``. Descriptors still use
       ``Param``: the plugin defines parameters on the descriptor, and the
       host builds its own from their property sets.
   * - ``Clip``, ``Image`` (``ofxEffect.h``)
     - ``PropertySet`` (an ``Image`` is one), and the clip's ``owner``, an
       ``EffectInstance``.
     - Can't be replaced, since ``effectSuite()`` treats every clip and image
       handle as one of these. Derive from them instead: ``makeClip()``
       creates the host's clips, ``fetchImage()`` returns its images with
       ``clip`` set, and ``releaseImage()`` takes them back.
   * - The timeline and ``currentTime()`` (``ofxDefaultSuites.h``,
       ``ofxEffect.h``)
     - ``timeline()``, the state behind the default timeline suite.
     - Its own ``OfxTimeLineSuiteV1``, and an override of
       ``EffectBase::currentTime()`` that returns the same time, so that
       ``paramGetValue`` and ``paramSetValue`` use the time the plugin gets
       from ``getTime``.
   * - ``InteractDescriptor``, ``InteractInstance`` (``ofxInteract.h``)
     - ``Plugin``, ``EffectDescriptor`` (which holds the overlay's entry
       point), ``EffectInstance``, ``PropertySet``, and a ``DrawContext`` for
       ``draw()``.
     - Its own ``OfxInteractSuiteV1``, since ``interactSuite()`` treats every
       interact handle as an ``InteractBase``, and its own calls to the entry
       point that ``overlayEntryPoint()`` returns.
   * - ``DrawContext`` (``ofxDrawSuiteHost.h``)
     - Nothing.
     - Its own ``OfxDrawSuiteV1``. Since ``InteractInstance::draw()`` needs a
       ``DrawContext``, it sends Draw through ``InteractDescriptor::call()``
       with in-args it builds itself.

Most hosts keep the pieces and customize them by overriding virtuals:

* ``EffectInstance``: ``fetchImage()``, ``releaseImage()`` and
  ``clipProperties()``, which a host must implement; ``makeClip()``,
  ``clipRegionOfDefinition()`` and ``abort()``; ``currentTime()`` and
  ``paramSetHandle()``, from ``EffectBase``; and ``beforeAction()`` and
  ``afterAction()``, around every action.
* ``InteractInstance``: ``beforeAction()`` and ``afterAction()``, around
  every action; and ``redrawRequested()`` and ``buffersSwapped()``, called by
  ``interactRedraw`` and ``interactSwapBuffers``.
* ``DrawContext``: ``standardColour()``, for ``getColour``, and an
  ``on...()`` virtual for each other draw suite call, which a host must
  implement; and ``onOpen()``.

``DrawContext`` is the object behind ``OfxDrawContextHandle``. It is an
interface, not a renderer: it enforces the specification's rules (calls only
during a Draw action, argument checks, and the colour, line width and stipple
a plugin can read back) and passes the drawing to those virtuals.

Mixing the host side with C calls
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Every object in ``openfx::host`` is what its C handle points to. ``handle()``
returns the handle, also on a const object, and the static ``from(handle)``
returns the object. These classes have both: ``PropertySet``, ``EffectBase``
(a descriptor or instance, for ``OfxImageEffectHandle``), ``Clip``, ``Image``
(a ``PropertySet``, whose handle is the image's property set), ``ParamSet``,
``Param``, ``InteractBase`` (for ``OfxInteractHandle``) and ``DrawContext``.
``Host::ofx()`` returns the ``OfxHost*`` to give a plugin. ``from()`` is an
unchecked cast, so use it only on handles these objects gave out.

The suites are plain C structs of function pointers: ``PropertySet::suite()``,
``effectSuite()``, ``paramSuite()``, ``interactSuite()``, ``drawSuite()``,
``memorySuite()``, ``multiThreadSuite()``, ``messageSuiteV1()`` and ``V2()``,
``progressSuiteV1()`` and ``V2()``, and ``timeLineSuite()``. Host code can
call them as a plugin would, return them from its own ``fetchSuite``, or pass
calls on to them from its own suites for handles it didn't create.

Host code reads and writes a property set with ``PropertySet``'s own
functions (``set``, ``getInt``, ``getDouble``, ``getString``, ``define``), or
through the C suite with ``PropertyAccessor`` and the generated
``openfx::host::propsets`` classes over ``PropertySet::suite()``. To send a
plugin an action, call ``Plugin::call(action, handle, inArgs, outArgs)``,
which just calls the plugin's main entry, or
``EffectInstance::action(name, inArgs, outArgs)``, which adds the instance's
hooks and its CreateInstance tracking.

Host objects don't adopt or release anything: the host owns every object, and
a plugin holds only handles to them.

.. code-block:: cpp

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

Exceptions and the C boundary
-----------------------------

When a C call fails, the wrappers throw ``openfx::OfxException``, whose
``code()`` is the status, or one of its subclasses:
``PropertyNotFoundException`` for a missing property (``kOfxStatErrUnknown``),
``ClipNotFoundException``, ``ImageNotFoundException``, and
``SuiteNotFoundException`` for a missing suite
(``kOfxStatErrMissingHostFeature``).

No exception may escape a function called through a C function pointer: a
plugin's ``setHost`` and main entry, an overlay's entry point, a thread
function passed to ``multiThread``, and each suite function and
``fetchSuite`` a host provides. ``openfx/ofxExceptions.h`` has three
functions to help, none of which throws:

``callAtCBoundary(f, fallback = kOfxStatFailed)``
    Runs ``f``, which returns an ``OfxStatus``. If ``f`` throws, it logs the
    exception and returns a status for it.

``statusFromCurrentException(fallback)``
    In a ``catch`` block, returns the status for the current exception: an
    ``OfxException``'s ``code()``, ``kOfxStatErrMemory`` for
    ``std::bad_alloc``, or ``fallback`` for anything else.

``logCurrentException(context, args...)``
    In a ``catch`` block, logs ``context: what()``, with the ``{}`` in
    ``context`` filled from ``args``.

The bindings already catch exceptions at these boundaries, so your code
behind them may throw:

* **Plugin side.** If an action throws, ``PluginEntry`` and
  ``ImageEffectPlugin::dispatch()`` return the exception's code,
  ``kOfxStatErrMemory`` for ``std::bad_alloc``, or ``kOfxStatFailed``. If the
  plugin class's constructor throws in ``setHost``, every later action returns
  the exception's code, or ``kOfxStatErrFatal``.
  ``InteractPlugin::mainEntry()`` and ``dispatch()`` do the same for an
  overlay. ``multiThread()`` catches exceptions in the worker threads and,
  once all the workers finish, rethrows the first one on the calling thread.
* **Host side.** Every suite function in ``openfx::host`` runs its body
  through ``callAtCBoundary``, so your code behind them (``fetchImage()``,
  ``releaseImage()``, ``clipRegionOfDefinition()``, and the
  ``InteractInstance`` and ``DrawContext`` virtuals) may throw, and the plugin
  gets a status. An exception from ``abort()`` counts as "don't abort".
  The default ``multiThread`` returns ``kOfxStatFailed`` if a thread function
  throws.
  ``~Plugin``, which sends Unload, and ``destroyInstance()`` log an exception
  from the action instead of throwing it.

MSVC's ``/EHsc`` is fine for both. The ``c`` lets the compiler assume that a
function *declared* ``extern "C"`` never throws, and drop a ``catch`` around a
direct call to one. The host side calls plugins only through function
pointers, where MSVC keeps the ``catch``. In your own code under ``/EHsc``,
don't rely on a ``catch`` around a direct call to an ``extern "C"`` function.

Anywhere else, handle the boundary yourself. If a C entry point of your own
calls the wrappers, wrap its body:

.. code-block:: cpp

   OfxStatus myMainEntry(const char* action, const void* handle,
                         OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
     return openfx::callAtCBoundary(
         [&] { return handleAction(action, handle, inArgs, outArgs); });
   }

Code that must continue after a failure catches the exception itself:

.. code-block:: cpp

   try {
     renderTile(tile);
   } catch (...) {
     openfx::logCurrentException("tile {}", tile.index);
     status = openfx::statusFromCurrentException(kOfxStatFailed);
   }

Property access
---------------

``PropertyAccessor`` (``ofxPropsAccess.h``) reads and writes one property set
through the C property suite. ``get<id>()``, ``set<id>()``, ``getAll<id>()``,
``setAll<id>()`` and the ``getPointD<id>()``, ``getRectD<id>()`` family take a
``PropId``, which carries the property's name, type and dimension from the
metadata, so a wrong name or type is a compile error. For a property with more
than one type, give the type too:
``get<PropId::OfxParamPropDefault, double>()``. The raw calls take any
property by name (see `Mixing the wrappers with C calls`_).

``exists<id>()`` and ``exists(name)`` check at run time whether the property
set has the property. Don't confuse them with ``prop::exists<id>()``, a
compile-time constant that is always true and says nothing about a property
set.

Every call is strict. It throws ``PropertyNotFoundException`` if the set
doesn't have the property (``kOfxStatErrUnknown``), and ``OfxException`` with
the suite's status for any other failure. The message gives the property and
the status. Nothing is logged: the exception is the only report. For a
property that may be missing, see `Missing properties`_.

Strings
~~~~~~~

``get<id>()``, ``getAll<id>()`` and the generated getters return string and
enum properties as ``openfx::CStringView`` (``ofxCStringView.h``), or a
``std::vector`` or ``std::array`` of them. With a plain ``const char*``,
``==`` compares addresses, so ``image.pixelDepth() == kOfxBitDepthFloat``
could be false even when the text matches. ``CStringView``'s ``==`` and
``!=`` compare the text, against a ``const char*``, ``std::string_view``,
``std::string`` or another ``CStringView``. ``CStringView``\ s also order by
content, so sorting and ``std::set`` work on the text.

It converts implicitly to ``const char*`` and ``std::string_view``, so you can
pass it straight to a C call or a ``string_view`` parameter.
``std::string(s)`` copies it, and ``<<`` streams it, so ``openfx::format()``
and the ``Logger`` accept it. It is never null: a null from the host reads as
``""``. Use ``c_str()`` for printf and other varargs functions, which can't
take a class type. The raw calls and the multi-type getter return the type you
ask for, so ``getRaw<const char*>(name)`` and ``get<id, const char*>()``
return a ``const char*``.

.. code-block:: cpp

   CStringView depth = image.pixelDepth();
   if (depth == kOfxBitDepthFloat)           // compares the text
     std::printf("%s\n", depth.c_str());     // varargs take the pointer
   std::string kept(depth);                  // a copy that outlives the image

``CStringView`` owns nothing. The host owns the characters, which last until
the property is next written or its set is destroyed. Copy them into a
``std::string`` to keep them longer.

Missing properties
~~~~~~~~~~~~~~~~~~

Every call throws ``PropertyNotFoundException`` for a missing property, even
an optional one that the specification lets a host or plugin leave out. The
generated headers mark optional properties in the comment above their methods:

.. code-block:: cpp

   // kOfxImageEffectPropCPURenderSupported (optional)

There are two ways to access a property that may be missing.

``soft()`` returns a copy of a ``PropertyAccessor`` or generated ``propsets``
object that tolerates a missing property, and nothing else. Writing or
resetting a missing property does nothing, and reading one returns the
fallback for its type:

.. list-table::
   :header-rows: 1

   * - Type
     - Fallback
   * - ``int``, ``bool``
     - ``0``, ``false``
   * - ``double``
     - ``0.0``
   * - a string
     - ``""``, never null
   * - ``void*``
     - ``nullptr``
   * - a dimension
     - ``0``, so a soft ``getAll()`` of a variable-dimension property returns
       no values

The copy chains like the original:

.. code-block:: cpp

   bool overlays = host.soft().supportsOverlays();       // false on a host without it
   desc.soft().setPluginDescription("Gain").setVersionLabel("1.0");
   CStringView space = props.soft().get<PropId::OfxImageClipPropColourspace>();
   int pass = props.soft().getRaw<int>("com.example.Pass");

A soft copy still throws for any other failure, such as a bad handle, an index
out of range or a value of the wrong type.

``PropertyAccessor``'s ``find<id>(index)`` and ``findRaw<T>(name, index)``
return a ``std::optional``, which is ``std::nullopt`` if the property is
missing. That tells a missing property apart from one that holds the fallback
value. Other failures throw, as with ``get``. The generated classes have no
``find``; use the ``PropertyAccessor`` their ``props()`` returns.

.. code-block:: cpp

   if (auto path = props.find<myhost::PropId::MyHostProjectPath>())
     openProject(*path);   // MyHost's own property; another host has none

Use the strict calls for a property the set must have, where a missing one is
a bug. Use ``soft()`` where the fallback value is good enough: a capability an
older host lacks, a property a plugin need not set, or a host-specific
property read in another host. Use ``find()`` when you need to know that the
property is missing.

Suites
------

``SuiteContainer`` (``ofxSuites.h``) holds suite pointers by name and version.
``get<SuiteType>()`` and ``has<SuiteType>()`` look up a suite by its C struct
type, and ``get<T>(name, version)`` and ``has(name, version)`` by name. To use
a suite type the headers don't know, such as a host's own or one newer than
the headers, register it once at global scope, before any code that looks it
up:

.. code-block:: cpp

   OPENFX_DEFINE_SUITE(MyHostWidgetSuiteV1, kMyHostWidgetSuite, 1);

Then ``get<MyHostWidgetSuiteV1>()`` and ``has<MyHostWidgetSuiteV1>()`` work
as for any other suite. ``get<T>()`` of an unregistered suite type does not
compile. ``fetchSuites()`` fills a plugin's ``ImageEffectPlugin::suites`` in
the Load action; a host's ``fetchSuite`` looks in ``Host::suites()``.

Generated metadata
------------------

``ofxPropsMetadata.h``, ``ofxPropsBySet.h`` and both
``ofxPropSetAccessors.h`` are generated from the ``@propdef``, ``@propset``
and ``@actiondef`` blocks in ``include/*.h``, which also generate the
:doc:`property reference <../Reference/ofxPropertiesReferenceGenerated>`.
Don't edit them by hand; edit the metadata in the C headers and regenerate:

.. code-block:: sh

   python scripts/gen-props.py

``Documentation/README.md`` documents the metadata block format.

Accessor method names come from the property name, without the ``Ofx``, the
``Prop`` and the object type: ``kOfxImageClipPropConnected`` becomes
``connected()``, and ``kOfxImageEffectPropSupportedPixelDepths`` becomes
``supportedPixelDepths()`` and ``setSupportedPixelDepths()``. Other
qualifiers stay, as in ``setOpenGLPixelDepth()`` for
``kOfxOpenGLPropPixelDepth``. The object type also stays where two properties
in a set would otherwise get the same method (``type()`` and
``paramType()``). The property name here is its string value, not its
``#define``, and the two differ for a few properties:
``kOfxImageEffectPropProjectPixelAspectRatio`` is
``"OfxImageEffectPropPixelAspectRatio"``, so its methods are
``pixelAspectRatio()`` and ``setPixelAspectRatio()``. In the generated
headers, a comment above each property's methods gives its ``#define``, and
its string value if different, so you can search for either. The comment ends
in "(optional)" if the property may be missing (see `Missing properties`_). A
property of variable dimension has a getter that takes an index and one that
returns all values: ``supportedContexts(i)`` and ``supportedContextsAll()``,
or, for a property with more than one type, ``defaultValue<double>(i)`` and
``defaultValueAll<double>()``.

A host with its own properties lists them in a YAML file and generates their
metadata in its own namespace with ``gen-props.py host-metadata``.
``PropertyAccessor`` then reads them by ``PropId``, like OpenFX's own:
``props.get<myhost::PropId::MyHostViewerProcess>()``. Other hosts don't have
them, so a plugin reads them through ``soft()`` or ``find()``. The generated
header also defines a C macro for each property, as the OFX headers do:

.. code-block:: cpp

   #define kMyHostViewerProcess "com.example.myhost.ViewerProcess"

Each ``#define`` is guarded by ``#ifndef``, so the host's own C header can
define it first; a ``static_assert`` checks that the two agree.
``openfx-cpp/examples/host-specific-props/`` has an example.

Logging
-------

``openfx/ofxLog.h`` is a small thread-safe logger with ``{}``-style
formatting: ``Logger::debug``, ``info``, ``warn`` and ``error``, a settable
level, and a settable handler to send messages elsewhere.
``Logger::setLevel(Logger::Level::Off)`` turns it off. Messages below the
level are not formatted. Log calls never throw, so they are safe at a C
boundary. See ``openfx-cpp/include/openfx/README-logging.md`` for details.

Testing
-------

The CMake build compiles the unit tests in ``openfx-cpp/tests`` and registers
them with CTest:

.. code-block:: sh

   scripts/build-cmake.sh -G Ninja Release      # build everything (see install.md)
   ctest --test-dir build/Release --output-on-failure

The unit tests are built twice, at C++20 and at C++17 (with tcb-span), and
each source file is a separate test at each standard: ``openfx-cpp.NAME`` and
``openfx-cpp.cxx17.NAME``. ``OFX_BUILD_OPENFX_CPP_TESTS`` (on by default)
builds the unit tests. ``OFX_BUILD_OPENFX_CPP_CHECK`` (on by default) adds an
``openfx-cpp-check`` target that compiles every header at C++17 and C++20,
along with the minimal plugin above.

Status
------

Prerelease. The headers are complete enough to write a real filter or host,
but the interfaces may still change. These parts of OpenFX have no wrappers
yet:

* fields and field rendering
* the GPU render suites (OpenGL, CUDA, Metal, OpenCL)
* parametric parameters
* OpenGL (V1) overlay interacts: the Draw-suite (V2) overlays are covered
* the dialog suite
* the OCIO and full colour management styles (basic and core are covered)

For those, call the C suites directly through ``SuiteContainer``. Register a
suite the headers don't know with ``OPENFX_DEFINE_SUITE`` (see `Suites`_).
