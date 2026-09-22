# OpenFX C++ Bindings

This is a set of C++ bindings for parts of OpenFX. It consists of a
set of prerelease-quality (for now) C++17 header files. They primarily
simplify type-safe access to properties, params, images and suites. It
includes a simple logging facility and a set of standard exceptions.

The goal is to include a version of this in the next OpenFX release.

## Layout

The headers are split by which side of the API uses them. Include them
with `openfx-cpp/include` on the include path, as `<openfx/...>`.

| Directory | Namespace | Used by | Contents |
|---|---|---|---|
| `openfx/` | `openfx` | plugins and hosts | Property metadata (`ofxPropsMetadata.h`, `ofxPropsBySet.h`), the type-safe `PropertyAccessor` (`ofxPropsAccess.h`), `SuiteContainer` (`ofxSuites.h`), the pixel depth and component vocabulary (`ofxPixels.h`), exceptions, logging, status strings, rect/point converters, the span shim. |
| `openfx/plugin/` | `openfx::plugin` | plugins only | RAII `Image` and `Clip` wrappers over the image effect suite, and the generated per-property-set accessor classes (`openfx::plugin::propsets`: getters for host-written properties, setters for plugin-written ones). |
| `openfx/host/` | `openfx::host` | hosts only | `PropertySet`, a metadata-driven property store with an `OfxPropertySuiteV1` over it (`ofxPropertySet.h`); `PluginBinary`, which loads a plugin binary or bundle and lists its plugins, plus the standard plugin search paths (`ofxPluginBinary.h`); default memory, multithread, message, progress and timeline suites (`ofxDefaultSuites.h`); `Host`, the `OfxHost` struct with its property set and suite container (`ofxHost.h`); `Plugin`, one plugin driven through its main entry point (`ofxPlugin.h`); the generic effect model -- parameters, clips, images, descriptors and an abstract `EffectInstance` that drives the actions -- with the image effect and parameter suites over it (`ofxEffect.h`); and the generated per-property-set accessor classes for the host side (`openfx::host::propsets`: setters for host-written properties, getters for plugin-written ones). |

Anything in `openfx/` takes the suites it needs as arguments, so it works
against a real host's suites from a plugin and against a host's own suite
implementations from inside that host. `ofxPropSetAccessors.h` exists in
both `plugin/` and `host/` with the same class names, so a translation unit
that needs both keeps them apart by namespace.

Two programs in the tree consume the bindings and double as their tests:
`TestHost/` (a command-line host built on the host side) and
`Examples/TestProps/` (a plugin that checks a host's property sets against
the metadata, built on the plugin side).

`ofxPropsMetadata.h`, `ofxPropsBySet.h` and both `ofxPropSetAccessors.h`
are generated from the `@propdef`, `@propset` and `@actiondef` blocks in
`include/*.h` by `scripts/gen-props.py`; do not edit them by hand.
