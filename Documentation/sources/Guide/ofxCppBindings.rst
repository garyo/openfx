.. SPDX-License-Identifier: CC-BY-4.0

The C++ Bindings
================

``openfx-cpp`` is a header-only C++ library over the OpenFX C API, for plugins
and hosts. It does not replace the C API: every wrapper calls the same suites
a C plugin or host would, and you can use the raw handles at any point. It
adds:

* property access checked at compile time against the specification's
  metadata
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

   #include <openfx/plugin/ofxPropSetAccessors.h>   // plugin side
   #include <openfx/host/ofxPropertySet.h>          // host side

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
    The generated accessor classes for each property set, in
    ``openfx::plugin::propsets``: getters for host-written properties, setters
    for plugin-written ones.

``openfx/host/`` (namespace ``openfx::host``), for hosts only
    ``PropertySet``, a property store driven by the metadata, with an
    ``OfxPropertySuiteV1`` over it (``ofxPropertySet.h``); and the generated
    accessor classes in ``openfx::host::propsets``: setters for host-written
    properties, getters for plugin-written ones.

Code in ``openfx/`` takes the suites it needs as arguments, so it works the
same in a plugin, with the host's suites, and in a host, with its own.
``plugin/`` and ``host/`` each have an ``ofxPropSetAccessors.h`` with the same
class names; the namespace tells them apart.

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

If a C entry point of your own calls the wrappers, wrap its body:

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
property by name.

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
compile.

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
``openfx-cpp-check`` target that compiles every header at C++17 and C++20.

Status
------

Prerelease. The interfaces may still change, and so far only the property
suite has wrappers. Call the other suites directly through ``SuiteContainer``.
Register a suite the headers don't know with ``OPENFX_DEFINE_SUITE`` (see
`Suites`_).
